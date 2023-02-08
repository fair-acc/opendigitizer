#ifndef OPENDIGITIZER_SERVICE_ACQWORKER_H
#define OPENDIGITIZER_SERVICE_ACQWORKER_H

#include <majordomo/Worker.hpp>
#include "daq_api.hpp"
#include <gnuradio/circular_buffer.hpp>
#include <gnuradio/tag.h>
#include <ranges>
#include <string_view>

#include <chrono>
#include <unordered_map>
#include <utility>

using opencmw::Annotated;
using opencmw::NoUnit;

struct TimeDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    int64_t                 lastRefTrigger                 = 0;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, lastRefTrigger, contentType)

struct FreqDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(FreqDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)

namespace opendigitizer::acq {

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

// TODO: check if we have a function for getting the next power of 2 already in opencmw
template <typename T> requires (std::is_integral_v<T> && std:: is_unsigned_v<T>)
constexpr T nextPowerOfTwo(T value, size_t pow = 0) { // NOLINT(misc-no-recursion) allow compile time recursion
    return (value >> pow) ? nextPowerOfTwo(value, pow + 1) : T(1) << pow;
}

/**
 * todo: how to handle possible race condition between stream and tag buffer? always wait for corresponding tag and enforce that every chunk sends at least one tag?
 *       or: tags are always committed first
 * todo: how to handle multiple sources?
 *       vector of shared pointers to buffers?
 *       move buffers into worker and destroy by sending special tag?
 *       for now only pass references at construction time
 **/
template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...> {
public:
    static const size_t RING_BUFFER_SIZE = 256;
    using super_t = Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...>;
    using streambuffer = gr::circular_buffer<float, RING_BUFFER_SIZE>;
    struct padded_tag_map { // ringbuffer needs sizeof(T) to be pow of 2
        alignas(nextPowerOfTwo(sizeof(gr::tag_map) + sizeof(std::int64_t))) gr::tag_map map;
        std::int64_t seq{0};
    };
    using tagbuffer = gr::circular_buffer<padded_tag_map, RING_BUFFER_SIZE>;
    struct sink_buffer{
        std::string name;
        std::string unit;
        float sample_rate = 0.0f;
        streambuffer stream;
        tagbuffer tag;

        sink_buffer(std::string n, std::string u, streambuffer &&s, tagbuffer &&t) : name(std::move(n)), unit(std::move(u)), stream(std::move(s)), tag(std::move(t)) { }
    };
private:
    struct sinks_with_readers{
        sink_buffer &sink;
        tagbuffer::template buffer_reader<padded_tag_map> tag_reader;
        streambuffer::template buffer_reader<float> stream_reader;

        explicit sinks_with_readers(sink_buffer &s) : sink{s}, tag_reader{s.tag.new_reader()}, stream_reader{s.stream.new_reader()} { }
    };
    std::atomic<bool>   _shutdownRequested;
    std::jthread        _pollingThread;

    std::map<std::string, sinks_with_readers> sinks; // subscriptions
public:
    template<typename BrokerType>
    explicit AcquisitionWorker(const BrokerType &broker, std::vector<sink_buffer> s) : super_t(broker, {}) {
        for (sink_buffer &sink : s) {
            sinks.insert({sink.name, sinks_with_readers{sink}});
        }

        _pollingThread = std::jthread([this] {
            while (!_shutdownRequested) {
                std::chrono::time_point time_start = std::chrono::system_clock::now();
                for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
                    if (subTopic.path() != "/Acquisition") {
                        break;
                    }

                    const auto              queryMap = subTopic.queryParamMap();
                    const TimeDomainContext filterIn = opencmw::query::deserialise<TimeDomainContext>(queryMap);

                    try {
                        Acquisition reply;
                        handleGetRequest(filterIn, reply);

                        TimeDomainContext filterOut = filterIn;
                        filterOut.contentType       = opencmw::MIME::JSON;
                        super_t::notify("/Acquisition", filterOut, reply);
                    } catch (const std::exception &ex) {
                        fmt::print("caught exception '{}'\n", ex.what());
                    }
                }

                auto willSleepFor = time_start + std::chrono::milliseconds(40) - std::chrono::system_clock::now();
                if (willSleepFor > 0ms) {
                    std::this_thread::sleep_for(willSleepFor);
                }
            }
        });

        super_t::setCallback([this](RequestContext &rawCtx, const TimeDomainContext &requestContext, const Empty&, TimeDomainContext& /*replyContext*/, Acquisition &out) {
            if (rawCtx.request.command() == Command::Get) {
                handleGetRequest(requestContext, out);
            }
        });
    }

    ~AcquisitionWorker() {
        _shutdownRequested = true;
        _pollingThread.join();
    }

private:
    bool handleGetRequest(const TimeDomainContext &requestContext, Acquisition &out) {
        auto channels = requestContext.channelNameFilter | std::ranges::views::split(',')
                      | std::ranges::views::transform([](auto && s) -> std::string_view { return {&*s.begin(), static_cast<size_t>(std::ranges::distance(s))};});
        // todo: trim channel name whitespace and validate for illegal characters
        std::size_t n_samples;
        float sample_rate;
        for (const std::string_view &channel : channels ) {
            const std::string chan(channel);
            bool first = true;
            auto t0 = std::chrono::system_clock::now() - std::chrono::milliseconds(300); // todo: this should be set by the request or be a default value
            if (sinks.contains(chan)) {
                sinks_with_readers &sink = sinks.at(chan);
                if (first) {
                    first = false;
                    sample_rate = sink.sink.sample_rate;
                    n_samples = get_samples_since(sink, t0);
                } else { // not first channel
                    if (sample_rate != sink.sink.sample_rate) {
                        continue;
                    }
                    // for now, we assume that all streams are continuous
                    // later there could also be checks for continuity and alignment
                    n_samples = get_samples_since(sink, t0);
                }
                out.channelNames.emplace_back(sink.sink.name);
                out.channelUnits.emplace_back(sink.sink.unit);
            }
            if (out.channelNames.empty()) {
                throw std::invalid_argument(fmt::format("Requested subscription for '{}' not found", requestContext.channelNameFilter));
            }
        }
        // populate values
        out.channelValues.dimensions() = { static_cast<unsigned int>(out.channelNames.size()), static_cast<unsigned int>(n_samples) };
        out.channelErrors.dimensions() = { static_cast<unsigned int>(out.channelNames.size()), static_cast<unsigned int>(n_samples) };
        out.channelTimeSinceRefTrigger.resize(static_cast<unsigned int>(n_samples));
        out.channelRangeMin.resize(static_cast<unsigned int>(out.channelNames.size()));
        out.channelRangeMax.resize(static_cast<unsigned int>(out.channelNames.size()));
        out.temperature.resize(static_cast<unsigned int>(out.channelNames.size()));
        out.status.resize(static_cast<unsigned int>(out.channelNames.size()));
        for (std::size_t i = 0; i < n_samples; i++) {
            out.channelTimeSinceRefTrigger[i] = static_cast<float>(i) * sample_rate;
        }
        unsigned int channel_index = 0;
        for (auto chan : out.channelNames) {
            sinks_with_readers &sink = sinks.at(chan);
            auto samples = get_samples(sink);
            for (std::size_t i = 0; i < n_samples; i++) {
                out.channelValues.get({channel_index, static_cast<unsigned int>(i)}) = samples[i];
                out.channelErrors.get({channel_index, static_cast<unsigned int>(i)}) = 0; // todo: implement errors
            }
            out.channelRangeMin[channel_index] = -std::numeric_limits<float>::infinity(); // todo: set meaningful limits / make configurable
            out.channelRangeMax[channel_index] = +std::numeric_limits<float>::infinity();
            out.temperature[channel_index] = 0.0;
            out.status[channel_index] = 0;
            channel_index++;
        }

        out.refTriggerName = { "NO_REF_TRIGGER" };
        out.refTriggerStamp    = 0;
        out.channelUserDelay   = 0.0f;
        out.channelActualDelay = 0.0f;
        return true;
    }

    private:
        std::size_t get_samples_since(sinks_with_readers sink, auto /*since*/) {
            // move reader to 50% to allow for publishers to get new data
            auto avail = sink.stream_reader.available();
            if (avail > sink.sink.stream.size() / 2) {
                auto consume_success = sink.stream_reader.consume(avail - sink.sink.stream.size() / 2);
                if (!consume_success) { fmt::print("Error consumeing stream samples"); }
            }
            return sink.stream_reader.available();
        }
        std::span<const float> get_samples(sinks_with_readers sink) {
            auto stream = sink.stream_reader.get(sink.stream_reader.available());
            //auto pos = sink.stream_reader.position();
            auto tags = sink.tag_reader.get(sink.tag_reader.available());
            std::size_t i;
            for (i = 0; tags[i].seq < sink.stream_reader.position(); i++) {
            }
            std::ignore = sink.tag_reader.consume(i-1);
            // process tags
            for (; tags[i].seq < (sink.stream_reader.position() + static_cast<long>(sink.stream_reader.available())) && i < tags.size(); i++) {
                const auto& [tag, seq] = tags[i];
                for (auto& [key, value] : tag) {
                    if (key == "timestamp") {
                        fmt::print("published timestamp: t = {}", pmtv::cast<double>(value));
                        // set/check against sample time/rate etc
                    }
                    // handle other tags
                }
            }
            return stream;
        }
};
} // namespace opendigitizer::acq
#endif //OPENDIGITIZER_SERVICE_ACQWORKER_H
