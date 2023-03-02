#ifndef OPENDIGITIZER_SERVICE_ACQEXTENDEDWORKER_H
#define OPENDIGITIZER_SERVICE_ACQEXTENDEDWORKER_H

#include "daq_api.hpp"
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
class AcquisitionExtendedWorker : public Worker<serviceName, AcquisitionFilter, Empty, AcquisitionExtended_float, Meta...> {
std::jthread              _notifyThread;
std::chrono::milliseconds _rate;

public:
using super_t = Worker<serviceName, AcquisitionFilter, Empty, AcquisitionExtended_float , Meta...>;
template<typename BrokerType>
explicit AcquisitionExtendedWorker(const BrokerType &broker, std::chrono::milliseconds rate)
    : super_t(broker, {}), _rate(rate) {
    _notifyThread = std::jthread([this, &rate](const std::stop_token &stoken) {
        fmt::print("acqExtWorker: starting notify thread\n");
        std::chrono::time_point update = std::chrono::system_clock::now();
        while (!stoken.stop_requested()) {
            fmt::print("acqExtWorker: active subscriptions: {}\n", super_t::activeSubscriptions() | std::ranges::views::transform([](auto &uri) { return uri.str(); }));
            for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
                if (subTopic.path() != serviceName.c_str()) {
                    continue;
                }

                const auto queryMap = subTopic.queryParamMap();
                const auto filterIn = opencmw::query::deserialise<AcquisitionFilter>(queryMap);

                try {
                    AcquisitionExtended_float       reply;
                    si::time<nanosecond, ::int64_t> triggerStamp{ std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count() };

                    std::string                     signals = filterIn.signalNames.empty() ? "sine,saw" : filterIn.signalNames;
                    for (std::string_view signal : signals | std::ranges::views::split(',') | std::ranges::views::transform([](const auto &&r) { return std::string_view{ &*r.begin(), std::ranges::distance(r) }; })) {
                        auto filterInLocal              = filterIn;
                        filterInLocal.signalNames = std::string{ signal };
                        handleGetRequest(triggerStamp, filterInLocal, reply);

                        AcquisitionFilter filterOut = filterIn;
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
            if (willSleepFor > 0ms) {
                std::this_thread::sleep_for(willSleepFor);
            }
            update = next_update;
        }
        fmt::print("acqWorker: stopped notify thread\n");
    });

    super_t::setCallback([this](RequestContext &rawCtx, const AcquisitionFilter&requestContext, const Empty &, AcquisitionFilter& /*replyContext*/, AcquisitionExtended_float &out) {
        if (rawCtx.request.command() == Command::Get) {
            // for real data, where we cannot generate the data on the fly, the get has to implement some sort of caching
            std::chrono::time_point         update = std::chrono::system_clock::now();
            si::time<nanosecond, ::int64_t> triggerStamp{ std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count() };
            handleGetRequest(triggerStamp, requestContext, out);
        }
    });
}

~AcquisitionExtendedWorker() {
    _notifyThread.request_stop();
    _notifyThread.join();
}

void handleGetRequest(const si::time<nanosecond, ::int64_t> updateStamp, const AcquisitionFilter &ctx, AcquisitionExtended_float &out) {
    using std::chrono::milliseconds, std::chrono::duration_cast, std::chrono::system_clock;
    using std::ranges::views::split, std::ranges::views::transform;
    constexpr std::size_t N_SAMPLES_T = 16;
    constexpr std::size_t N_SAMPLES_F = 12;

    out.acqMode = ctx.acqMode;
    out.timestamp = updateStamp;
    out.beamInTimeStamp = 0.0;
    out.timingEvents = std::vector<std::string>{"99287394673:CMD_BEAM_INJECTION,C=3:S=2:P=5:T=300;1241829398294:CMD_USER_1.C=3:S=2:P=7:T=300"};

    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "status messages for signal">                       signalStatus; // todo type of map to be discussed, japc compatibility, opencmwlight serialiser does not support vec<map<>>
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "raw timing events occurred in the acq window">     timingEvents; // now only maps timestamp to event id, because more complex containers trip up the serialisers
    out.signalNames = std::vector<std::string>{ctx.signalNames};

    out.signalValues = AcquisitionExtended_float::mdtype{std::vector<float>(out.signalNames.size() * N_SAMPLES_T * N_SAMPLES_F), out.signalNames.size(), N_SAMPLES_T, N_SAMPLES_F,1,1,1,1,1}; // todo: nicer API for unused dimensions
    out.signalErrors = AcquisitionExtended_float::mdtype{std::vector<float>(out.signalNames.size() * N_SAMPLES_T * N_SAMPLES_F), out.signalNames.size(), N_SAMPLES_T, N_SAMPLES_F,1,1,1,1,1};
    out.axisValues = std::vector<float>{0.01f, 0.02f, 0.03f, 0.04f, 0.05f, 0.06f, 0.07f, 0.08f, 0.09f, 0.10f, 0.11f, 0.12f, 0.13f, 0.14f, 0.15f, 0.16f,
                                        0.01f, 0.02f, 0.03f, 0.04f, 0.05f, 0.06f, 0.07f, 0.08f, 0.09f, 0.10f, 0.11f, 0.12f};
    out.axisUnits = std::vector<std::string>{"s", "Hz"};
    out.axisNames = std::vector<std::string>{"time", "frequency"};

    std::size_t n = 0;
    for (auto &name : out.signalNames) {
        if (name == "saw") {
            for (std::size_t t = 0; t < N_SAMPLES_T; t++) {
                for (std::size_t f = 0; f < N_SAMPLES_F; f++) {
                    out.signalValues(n, t, f, 0,0,0,0,0) = float(t % 4) * float(f % 3);
                    out.signalErrors(n, t, f, 0,0,0,0,0) = 0.1f;
                }
            }
            out.signalUnits.emplace_back("particles");
            out.signalRanges.emplace_back(-0.1f);
            out.signalRanges.emplace_back(12.1f);
            out.signalStatus.emplace_back("errors=[],temperature=20.4");
        } else if (name == "chess") {
            for (std::size_t t = 0; t < N_SAMPLES_T; t++) {
                for (std::size_t f = 0; f < N_SAMPLES_F; f++) {
                    out.signalValues(n, t, f, 0,0,0,0,0) = ((t << 2) % 2 xor (f << 1) % 2) ? 0.2f : 0.8f;
                    out.signalErrors(n, t, f, 0,0,0,0,0) = 0.01f;
                }
            }
            out.signalUnits.emplace_back("V");
            out.signalRanges.emplace_back(0.19f);
            out.signalRanges.emplace_back(0.81f);
            out.signalStatus.emplace_back("warnings=[overrange],temperature=25.4");
        }
        n++;
    }
}
};
} // namespace opendigitizer::acq
#endif // OPENDIGITIZER_SERVICE_ACQEXTENDEDWORKER_H
