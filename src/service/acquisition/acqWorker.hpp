#pragma once
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

template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...> {
private:
    static const size_t RING_BUFFER_SIZE = 256;
    std::atomic<bool>   _shutdownRequested;
    std::jthread        _pollingThread;

    //class GRSink {
    //    struct RingBufferData {
    //        std::vector<std::vector<float>> chunk;
    //        int64_t                         timestamp = 0;
    //    };
    //    using ringbuffer_t = std::shared_ptr<RingBuffer<RingBufferData, RING_BUFFER_SIZE, BusySpinWaitStrategy, SingleThreadedStrategy>>;
    //    using sequence_t   = std::shared_ptr<Sequence>;

    //    std::vector<std::string> _channelNames;      // { signalName1, signalName2, ... }
    //    std::vector<std::string> _channelUnits;      // { signalUnit1, signalUnit2, ... }
    //    std::string              _channelNameFilter; // signalName1@sampleRate,signalName2@sampleRate...
    //    float                    _sampleRate = 0;
    //    ringbuffer_t             _ringBuffer;
    //    sequence_t               _tail;

    //public:
    //    GRSink() {
    //        _ringBuffer->addGatingSequences({ _tail });

    //        for (size_t i = 0; i < _channelNames.size(); i++) {
    //            _channelNameFilter.append(fmt::format("{}@{}Hz", _channelNames[i], _sampleRate));
    //            if (i != (_channelNames.size() - 1)) {
    //                _channelNameFilter.append(",");
    //            }
    //        }
    //    };

    //    [[nodiscard]] std::string getChannelNameFilter() const {
    //        return _channelNameFilter;
    //    };

    //    ringbuffer_t getRingBuffer() const {
    //        return _ringBuffer;
    //    };

    //    void fetchData(const int64_t lastRefTrigger, Acquisition &out) {
    //        std::vector<float> stridedValues;
    //        int64_t            tail       = _tail->value();
    //        int64_t            head       = _ringBuffer->cursor();

    //        bool               firstChunk = true;
    //        for (size_t i = 0; i < _channelNames.size(); i++) {
    //            for (int64_t sequence = tail; sequence <= head; sequence++) {
    //                const RingBufferData &bufData = (*_ringBuffer)[sequence];

    //                if (bufData.timestamp > lastRefTrigger) {
    //                    if (firstChunk) {
    //                        for (const auto &channelName : _channelNames) {
    //                            out.channelNames.push_back(fmt::format("{}@{}Hz", channelName, _sampleRate));
    //                        }
    //                        out.channelUnits    = _channelUnits;
    //                        out.refTriggerStamp = bufData.timestamp;
    //                        firstChunk          = false;
    //                    }

    //                    stridedValues.insert(stridedValues.end(), bufData.chunk[i].begin(), bufData.chunk[i].end());
    //                }
    //            }
    //        }

    //        if (!stridedValues.empty()) {
    //            //  generate multiarray values from strided array
    //            size_t channelValuesSize = stridedValues.size() / _channelNames.size();
    //            out.channelValues        = opencmw::MultiArray<float, 2>(std::move(stridedValues), { static_cast<uint32_t>(_channelNames.size()), static_cast<uint32_t>(channelValuesSize) });
    //            //  generate relative timestamps
    //            out.channelTimeSinceRefTrigger.reserve(channelValuesSize);
    //            for (size_t i = 0; i < channelValuesSize; ++i) {
    //                float relativeTimestamp = static_cast<float>(i) / _sampleRate;
    //                out.channelTimeSinceRefTrigger.push_back(relativeTimestamp);
    //            }
    //        } else {
    //            // throw std::invalid_argument(fmt::format("No new data available for signals: '{}'", _channelNames));
    //        }
    //    };

    //    void copySinkData(std::vector<const void *> &input_items, int &noutput_items, const std::vector<std::string> &signal_names, float /* sample_rate */, int64_t timestamp_ns) {
    //        if (signal_names == _channelNames) {
    //            bool result = _ringBuffer->tryPublishEvent([&input_items, noutput_items, timestamp_ns](RingBufferData &&bufferData, std::int64_t /*sequence*/) noexcept {
    //                bufferData.timestamp = timestamp_ns;
    //                bufferData.chunk.clear();
    //                for (auto & input_item : input_items) {
    //                    const auto *in = static_cast<const float *>(input_item);
    //                    bufferData.chunk.emplace_back(std::vector<float>(in, in + noutput_items));
    //                }
    //            });

    //            if (result) {
    //                auto       headValue       = _ringBuffer->cursor();
    //                auto       tailValue       = _tail->value();

    //                const auto tailOffsetValue = static_cast<int64_t>(RING_BUFFER_SIZE) * 50 / 100; // 50 %

    //                if (headValue > (tailValue + tailOffsetValue)) {
    //                    _tail->setValue(headValue - tailOffsetValue);
    //                }

    //            } else {
    //                // error writing into RingBuffer
    //                noutput_items = 0;
    //            }
    //        }
    //    }
    //};

public:
    using super_t = Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...>;
    using streambuffer = gr::circular_buffer<double, RING_BUFFER_SIZE>;
    using padded_tag_map = struct {alignas(nextPowerOfTwo(sizeof(gr::tag_map))) gr::tag_map map;}; // ringbuffer needs sizeof(T) to be pow of 2
    using tagbuffer = gr::circular_buffer<padded_tag_map, RING_BUFFER_SIZE>;
    struct sink_buffer{
        std::string name;
        std::string unit;
        float sample_rate = 0.0f;
        streambuffer &stream;
        tagbuffer &tag;
        streambuffer::template buffer_reader<double> stream_reader;
        tagbuffer::template buffer_reader<padded_tag_map> tag_reader;

        sink_buffer(std::string n, std::string u, streambuffer &s, tagbuffer &t) : name(std::move(n)), unit(std::move(u)), stream(s), tag(t), tag_reader(t.new_reader()),
                                                                                   stream_reader(s.new_reader()) { }
    };

private:
        // subscriptions
        std::map<std::string, std::reference_wrapper<sink_buffer>> sinks;
        // todo: how to handle possible race condition between stream and tag buffer? always wait for corresponding tag and enforce that every chunk sends at least one tag?
        //       or: tags are always committed first
        // todo: how to handle multiple sources?
        //       vector of shared pointers to buffers?
        //       move buffers into worker and destroy by sending special tag?
        //       for now only pass references at construction time
public:

    template<typename BrokerType>
    explicit AcquisitionWorker(const BrokerType &broker, std::vector<sink_buffer> s) : super_t(broker, {}) {
        for (auto sink : s) {
            sinks.insert({sink.name, sink});
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
                sink_buffer sink = sinks.at(chan);
                if (first) {
                    first = false;
                    sample_rate = sink.sample_rate;
                    n_samples = get_samples_since(sink, t0);
                } else { // not first channel
                    if (sample_rate != sink.sample_rate) {
                        continue;
                    }
                    // for now, we assume that all streams are continuous
                    // later there could also be checks for continuity and alignment
                    n_samples = get_samples_since(sink, t0);
                }
                out.channelNames.emplace_back(sink.name);
                out.channelUnits.emplace_back(sink.unit);
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
            sink_buffer sink = sinks.at(chan);
            for (std::size_t i = 0; i < n_samples; i++) {
                out.channelValues.get({channel_index, static_cast<unsigned int>(i)}) = get_sample(sink, i);
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
        std::size_t get_samples_since(sink_buffer sink, auto since) {
            return 0; // todo: implement
        }
        float get_sample(sink_buffer, std::size_t index) {
            return 0.0f;
        }

};

// using namespace opencmw::majordomo;
// template<units::basic_fixed_string serviceName, typename... Meta>
// class FrequencyDomainWorker
//         : public Worker<serviceName, FreqDomainContext, Empty, AcquisitionSpectra, Meta...> {
// private:
//     static const size_t RING_BUFFER_SIZE = 4096;
//     const std::string   _deviceName;
//     std::atomic<bool>   _shutdownRequested;
//     std::jthread        _pollingThread;
//     AcquisitionSpectra  _reply;
//
//     struct RingBufferData {
//         std::vector<float> chunk;
//         int64_t            timestamp = 0;
//     };
//     using ringbuffer_t  = std::shared_ptr<RingBuffer<RingBufferData, RING_BUFFER_SIZE, BusySpinWaitStrategy, SingleThreadedStrategy>>;
//     using eventpoller_t = std::shared_ptr<EventPoller<RingBufferData, RING_BUFFER_SIZE, BusySpinWaitStrategy, SingleThreadedStrategy>>;
//     struct SignalData {
//         ringbuffer_t                         ringBuffer;
//         eventpoller_t                        eventPoller;
//     };
//
//     std::unordered_map<std::string, SignalData> _signalsMap; // <completeSignalName, signalData>
//
// public:
//     using super_t = Worker<serviceName, FreqDomainContext, Empty, AcquisitionSpectra, Meta...>;
//
//     template<typename BrokerType>
//     explicit FrequencyDomainWorker(const BrokerType &broker)
//             : super_t(broker, {}) {
//         // polling thread
//         _pollingThread = std::jthread([this] {
//             std::chrono::duration<double, std::milli> pollingDuration{};
//             while (!_shutdownRequested) {
//                 std::chrono::time_point time_start = std::chrono::system_clock::now();
//
//                 for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
//                     if (subTopic.path() != "/AcquisitionSpectra") {
//                         break;
//                     }
//                     const auto                         queryMap = subTopic.queryParamMap();
//                     const FreqDomainContext            filterIn = opencmw::query::deserialise<FreqDomainContext>(queryMap);
//                     std::set<std::string, std::less<>> requestedSignals;
//                     if (!checkRequestedSignals(filterIn, requestedSignals)) {
//                         break;
//                     }
//
//                     int64_t maxChunksToPoll = chunksToPoll(requestedSignals);
//
//                     if (maxChunksToPoll == 0) {
//                         break;
//                     }
//
//                     pollMultipleSignals(*(requestedSignals.begin()), maxChunksToPoll, _reply);
//                     FreqDomainContext filterOut = filterIn;
//                     filterOut.contentType       = opencmw::MIME::JSON;
//                     super_t::notify("/AcquisitionSpectra", filterOut, _reply);
//                 }
//                 pollingDuration   = std::chrono::system_clock::now() - time_start;
//
//                 auto willSleepFor = std::chrono::milliseconds(40) - pollingDuration;
//                 std::this_thread::sleep_for(willSleepFor);
//             }
//         });
//
//         super_t::setCallback([this](RequestContext &rawCtx, const FreqDomainContext &requestContext, const Empty &, FreqDomainContext& /*replyContext*/, AcquisitionSpectra &out) {
//             if (rawCtx.request.command() == Command::Get) {
//                 handleGetRequest(requestContext, out);
//             }
//         });
//     }
//
//     ~FrequencyDomainWorker() {
//         _shutdownRequested = true;
//         _pollingThread.join();
//     }
//
//     void callbackCopySinkData(std::vector<const void *> &input_items, int &nitems, size_t vector_size, const std::vector<std::string> &signal_name, float sample_rate, int64_t timestamp) {
//         const auto *in                = static_cast<const float *>(input_items[0]);
//         const auto completeSignalName = fmt::format("{}@{}Hz", signal_name[0], sample_rate);
//         if (_signalsMap.contains(completeSignalName)) {
//             const SignalData &signalData = _signalsMap.at(completeSignalName);
//
//             for (int i = 0; i < nitems; i++) {
//                 // publish data
//                 bool result = signalData.ringBuffer->tryPublishEvent([i, in, vector_size, timestamp](RingBufferData &&bufferData, std::int64_t /*sequence*/) noexcept {
//                     bufferData.timestamp = timestamp;
//                     size_t offset        = static_cast<size_t>(i) * vector_size;
//                     bufferData.chunk.assign(in + offset + vector_size / 2, in + offset + vector_size);
//                 });
//
//                 if (!result) {
//                     // fmt::print("freqDomainWorker: error writing into RingBuffer, signal_name: {}\n", signal_name[0]);
//                     nitems = 0;
//                 }
//             }
//         }
//     }
//
// private:
//     bool handleGetRequest(const FreqDomainContext &requestContext, AcquisitionSpectra &out) {
//         std::set<std::string, std::less<>> requestedSignals;
//         if (!checkRequestedSignals(requestContext, requestedSignals)) {
//             return false;
//         }
//
//         int64_t maxChunksToPoll = chunksToPoll(requestedSignals);
//
//         if (maxChunksToPoll == 0) {
//             return false;
//         }
//
//         pollMultipleSignals(*(requestedSignals.begin()), maxChunksToPoll, out);
//         return true;
//     }
//
//     // find how many chunks should be parallely polled
//     int64_t chunksToPoll(std::set<std::string, std::less<>> &requestedSignals) {
//         std::vector<int64_t> chunksAvailable;
//         for (const auto &requestedSignal : requestedSignals) {
//             auto    signalData = _signalsMap.at(requestedSignal);
//             int64_t diff       = signalData.ringBuffer->cursor() - signalData.eventPoller->sequence()->value();
//             chunksAvailable.push_back(diff);
//         }
//         assert(!chunksAvailable.empty());
//         auto maxChunksToPollIterator = std::min_element(chunksAvailable.begin(), chunksAvailable.end());
//         if (maxChunksToPollIterator == chunksAvailable.end()) {
//             return 0;
//         }
//
//         return *maxChunksToPollIterator;
//     }
//
//     void pollMultipleSignals(const std::string &requestedSignal, int64_t chunksToPoll, AcquisitionSpectra &out) {
//         assert(chunksToPoll > 0);
//         auto signalData     = _signalsMap.at(requestedSignal);
//
//         out.refTriggerStamp = 0;
//         out.channelName     = requestedSignal;
//
//         PollState result    = PollState::Idle;
//         for (int64_t i = 0; i < chunksToPoll; i++) {
//             result = signalData.eventPoller->poll([&out](RingBufferData &event, std::int64_t /*sequence*/, bool /*nomoreEvts*/) noexcept {
//                 out.refTriggerStamp = event.timestamp;
//                 out.channelMagnitudeValues.assign(event.chunk.begin(), event.chunk.end());
//                 return false;
//             });
//         }
//         assert(result == PollState::Processing);
//
//         //  generate frequency values
//         size_t vectorSize = out.channelMagnitudeValues.size();
//         float  bandwidth  = signalData.sink->get_bandwidth();
//         out.channelFrequencyValues.clear();
//         out.channelFrequencyValues.reserve(vectorSize);
//         float freqStartValue = 0; //-(bandwidth / 2);
//         float freqStepValue  = 0.5f * bandwidth / static_cast<float>(vectorSize);
//         for (size_t i = 0; i < vectorSize; i++) {
//             out.channelFrequencyValues.push_back(freqStartValue + static_cast<float>(i) * freqStepValue);
//         }
//     }
//
//     bool checkRequestedSignals(const FreqDomainContext &filterIn, std::set<std::string, std::less<>> &requestedSignals) {
//         auto signals = std::string_view(filterIn.channelNameFilter) | std::ranges::views::split(',');
//         for (const auto &signal : signals) {
//             requestedSignals.emplace(std::string_view(signal.begin(), signal.end()));
//         }
//         if (requestedSignals.empty()) {
//             respondWithEmptyResponse(filterIn, "no signals requested, sending empty response\n");
//             return false;
//         }
//
//         // check if signals exist
//         std::vector<std::string> unknownSignals;
//         for (const auto &requestedSignal : requestedSignals) {
//             if (!_signalsMap.contains(requestedSignal)) {
//                 unknownSignals.push_back(requestedSignal);
//             }
//         }
//         if (!unknownSignals.empty()) {
//             respondWithEmptyResponse(filterIn, fmt::format("requested unknown signals: {}\n", unknownSignals));
//             return false;
//         }
//
//         return true;
//     }
//
//     void respondWithEmptyResponse(const FreqDomainContext &filter, const std::string_view errorText) {
//         fmt::print("{}\n", errorText);
//         super_t::notify("/AcquisitionSpectra", filter, AcquisitionSpectra());
//     }
// };
} // namespace opendigitizer::acq
