#ifndef OPENDIGITIZER_SERVICE_ACQWORKER_H
#define OPENDIGITIZER_SERVICE_ACQWORKER_H

#include <daq_api.hpp>
#include <majordomo/Worker.hpp>
#include <ranges>
#include <string_view>

#include <chrono>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace opendigitizer::acq {
using opencmw::Annotated;
using opencmw::NoUnit;

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...> {
    std::jthread              _notifyThread;
    std::chrono::milliseconds _rate;

public:
    using super_t = Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...>;
    template<typename BrokerType>
    explicit AcquisitionWorker(BrokerType &broker, std::chrono::milliseconds rate)
        : super_t(broker, {}), _rate(rate) {
        // this makes sure the subscriptions are filtered correctly
        opencmw::query::registerTypes(TimeDomainContext(), broker);

        _notifyThread = std::jthread([this, &rate](const std::stop_token &stoken) {
            fmt::print("acqWorker: starting notify thread\n");
            std::chrono::time_point update = std::chrono::system_clock::now();
            while (!stoken.stop_requested()) {
                fmt::print("acqWorker: active subscriptions: {}\n", super_t::activeSubscriptions() | std::ranges::views::transform([](auto &uri) { return uri.path(); }));
                for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
                    if (subTopic.path() != serviceName.c_str()) {
                        continue;
                    }

                    const auto              queryMap = subTopic.params();
                    const TimeDomainContext filterIn = opencmw::query::deserialise<TimeDomainContext>(queryMap);

                    try {
                        Acquisition                     reply;
                        si::time<nanosecond, ::int64_t> triggerStamp{ std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count() };

                        std::string                     signals = filterIn.channelNameFilter.empty() ? "sine,saw" : filterIn.channelNameFilter;
                        for (std::string_view signal : signals | std::ranges::views::split(',') | std::ranges::views::transform([](const auto &&r) { return std::string_view{ &*r.begin(), std::ranges::distance(r) }; })) {
                            auto filterInLocal              = filterIn;
                            filterInLocal.channelNameFilter = std::string{ signal };
                            handleGetRequest(triggerStamp, filterInLocal, reply);

                            TimeDomainContext filterOut = filterIn;
                            filterOut.contentType       = opencmw::MIME::JSON;
                            auto res                    = super_t::notify(std::string(serviceName.c_str()), filterOut, reply);
                            fmt::print("acqWorker: {} update sent {}\n", signal, res);
                        }
                    } catch (const std::exception &ex) {
                        fmt::print("caught exception '{}'\n", ex.what());
                    }
                }

                auto next_update  = update + 2000ms;
                auto willSleepFor = next_update - std::chrono::system_clock::now();
                ;
                if (willSleepFor > 0ms) {
                    std::this_thread::sleep_for(willSleepFor);
                }
                update = next_update;
            }
            fmt::print("acqWorker: stopped notify thread\n");
        });

        super_t::setCallback([this](RequestContext &rawCtx, const TimeDomainContext &requestContext, const Empty &, TimeDomainContext & /*replyContext*/, Acquisition &out) {
            if (rawCtx.request.command == opencmw::mdp::Command::Get) {
                // for real data, where we cannot generate the data on the fly, the get has to implement some sort of caching
                std::chrono::time_point         update = std::chrono::system_clock::now();
                si::time<nanosecond, ::int64_t> triggerStamp{ std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count() };
                handleGetRequest(triggerStamp, requestContext, out);
            }
        });
    }

    ~AcquisitionWorker() {
        _notifyThread.request_stop();
        _notifyThread.join();
    }

    void handleGetRequest(const si::time<nanosecond, ::int64_t> updateStamp, const TimeDomainContext &ctx, Acquisition &out) {
        using std::chrono::milliseconds, std::chrono::duration_cast, std::chrono::system_clock;
        using std::ranges::views::split, std::ranges::views::transform;
        constexpr std::size_t N_SAMPLES = 32;

        out.channelName                 = ctx.channelNameFilter;

        out.acqTriggerTimeStamp         = updateStamp;
        out.acqTriggerName              = "STREAMING";

        // missing move constructor... out.channelValues = {{}};
        out.channelValue.resize(N_SAMPLES);
        out.channelError.resize(N_SAMPLES);
        out.channelTimeBase.resize(N_SAMPLES);

        if (out.channelName.value() == "sine") {
            const double amplitude = 1.0;
            const int    n_period  = 20;
            for (std::size_t i = 0; i < N_SAMPLES; i++) {
                const float t       = (static_cast<float>((updateStamp.number() * N_SAMPLES / _rate.count() / 1000000 + i) % n_period)) * _rate.count() * 1e-3f / N_SAMPLES;
                out.channelValue[i] = static_cast<float>(amplitude * std::sin(static_cast<double>(t) / n_period * std::numbers::pi));
                out.channelError[i] = 0.0f;
                // out.channelTimeBase[i] = static_cast<float>(i) * _rate.count() * 1e-3f / N_SAMPLES; // time in float seconds
                out.channelTimeBase[i] = i * _rate.count() / N_SAMPLES; // time in integer nanoseconds
            }
            out.channelUnit = "V";
        } else if (out.channelName.value() == "saw") {
            const double amplitude = 9.0;
            const int    n_period  = 13;
            for (std::size_t i = 0; i < N_SAMPLES; i++) {
                const float t       = (static_cast<float>((updateStamp.number() * N_SAMPLES / _rate.count() / 1000000 + i) % n_period)) * _rate.count() * 1e-3f / N_SAMPLES;
                out.channelValue[i] = static_cast<float>(amplitude * static_cast<double>(t) / n_period);
                out.channelError[i] = 0.01f;
                // out.channelTimeBase[i] = static_cast<float>(i) * _rate.count() * 1e-3f / N_SAMPLES; // time in float seconds
                out.channelTimeBase[i] = i * _rate.count() / N_SAMPLES; // time in integer nanoseconds
            }
            out.channelUnit = "A";
        }
        out.temperature.value() = 20.0f;
    }
};
} // namespace opendigitizer::acq
#endif // OPENDIGITIZER_SERVICE_ACQWORKER_H
