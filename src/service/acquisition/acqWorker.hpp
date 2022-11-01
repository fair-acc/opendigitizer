#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <ranges>
#include <string_view>
#include <thread>

#include "daq_api.hpp"

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct AcqFilterContext {
    std::string             signalFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};
ENABLE_REFLECTION_FOR(AcqFilterContext, signalFilter, contentType)

using namespace opencmw::majordomo;

template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, AcqFilterContext, Empty, Acquisition, Meta...> {
    static constexpr size_t                                                     N_ELEMS = 100;
    std::mutex                                                                  mockSignalsLock;
    std::unordered_map<std::string, std::pair<int, std::array<float, N_ELEMS>>> mockSignals = { { "A", { 0, { 0.0f } } }, { "B", { 0, { 1.0f } } } }; // <signal name, <update inc, value>>
    std::jthread                                                                notifyThread;
    bool                                                                        shutdownRequested = false;

public:
    using super_t = Worker<serviceName, AcqFilterContext, Empty, Acquisition, Meta...>;

    template<typename BrokerType, typename Interval>
    explicit AcquisitionWorker(const BrokerType &broker, Interval updateInterval)
        : super_t(broker, {}) {
        notifyThread = std::jthread([this, updateInterval] {
            while (!shutdownRequested) {
                std::this_thread::sleep_for(updateInterval);
                updateData();
            }
        });

        super_t::setCallback([this](const RequestContext &rawCtx, const AcqFilterContext &filterIn, const Empty & /*in - unused*/, AcqFilterContext &filterOut, Acquisition &out) {
            if (rawCtx.request.command() == Command::Get) {
                fmt::print("worker received 'get' request\n");
                handleGetRequest(filterIn, filterOut, out);
            } else if (rawCtx.request.command() == Command::Set) {
                fmt::print("worker received 'set' request - ignoring for read-only property\n");
            }
        });
    }

    void updateData() { // some silly updating data
        static int  counter      = 0;
        const auto  notifySignal = counter % 2 == false ? "A" : "B";
        const float t0           = M_2_PIf * static_cast<float>(counter) / 10.0f;
        if (mockSignals.contains(notifySignal)) {
            fmt::print("updateData({}) - update signal '{}'\n", counter, notifySignal);
            std::lock_guard lockGuard(mockSignalsLock);
            mockSignals[notifySignal].first++;
            std::array<float, N_ELEMS> value{};
            for (std::size_t i = 0; i < N_ELEMS; i++) {
                const float t = t0 + static_cast<float>(i) * 2.0f / N_ELEMS;
                value[i]      = { counter % 2 == false ? sinf(t) : cosf(t) };
            }
            mockSignals[notifySignal].second = value;
        } else {
            fmt::print("updateData({}) - updated nothing\n", counter);
        }
        counter++;
        notifyUpdate();
    }

private:
    void handleGetRequest(const AcqFilterContext &filterIn, AcqFilterContext & /*filterOut*/, Acquisition &out) {
        // auto signals = std::string_view(filterIn.signalFilter) | std::ranges::views::split(',');
        // for (const auto &signal : signals) {
        //     out.channelNames.emplace_back(std::string_view(signal.begin(), signal.end()));
        // }
        for (const auto &[signal, data] : mockSignals) {
            out.channelNames.emplace_back(std::string_view(signal.begin(), signal.end()));
        }
        out.channelUnits.value() = { "V", "A" };
        fmt::print("handleGetRequest for '{}'\n", out.channelNames.value());
        std::lock_guard lockGuard(mockSignalsLock);
        out.channelTimeSinceRefTrigger.value().resize(N_ELEMS);
        std::iota(out.channelTimeSinceRefTrigger.value().begin(), out.channelTimeSinceRefTrigger.value().end(), 2.0f / N_ELEMS);
        out.channelValues.value() = opencmw::MultiArray<float, 2>({ out.channelNames.size(), N_ELEMS });
        out.channelErrors.value() = opencmw::MultiArray<float, 2>({ out.channelNames.size(), N_ELEMS });
        for (std::size_t i = 0; i < out.channelNames.size(); i++) {
            const auto mock = mockSignals.at(out.channelNames.value()[i]);
            const auto sig  = mock.second;
            for (std::size_t j = 0; j < N_ELEMS; j++) {
                out.channelValues.value()[{ i, j }] = sig[j];
            }
            if (i == 0) {
                out.refTriggerStamp = mock.first;
            }
        }
    }

    bool shallUpdateForTopic(const auto &filterIn) const noexcept {
        // auto                               signals = std::string_view(filterIn.signalFilter) | std::ranges::views::split(',');
        // std::set<std::string, std::less<>> requestedSignals;
        // for (const auto &signal : signals) {
        //     requestedSignals.emplace(std::string_view(signal.begin(), signal.end()));
        // }
        // if (requestedSignals.empty()) {
        //     return false;
        // }
        // int updateCount = -1;
        // for (const auto &signal : requestedSignals) {
        //     if (!mockSignals.contains(signal)) {
        //         fmt::print("requested unknown signal '{}'\n", signal);
        //         return false;
        //     }
        //     if (updateCount < 0) {
        //         updateCount = mockSignals.at(signal).first;
        //     }
        //     if (mockSignals.at(signal).first != updateCount || updateCount == 0) {
        //         // here: don't update if the update count is not identical for all signals
        //         return false;
        //     }
        // }
        return true;
    }

    void notifyUpdate() {
        for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions

            const auto             queryMap = subTopic.queryParamMap();
            const AcqFilterContext filterIn = opencmw::query::deserialise<AcqFilterContext>(queryMap);
            if (!shallUpdateForTopic(filterIn)) {
                fmt::print("active user subscription: '{}' is NOT being notified\n", subTopic.str());
                break;
            }
            AcqFilterContext filterOut = filterIn;
            Acquisition      subscriptionReply;
            try {
                handleGetRequest(filterIn, filterOut, subscriptionReply);
                super_t::notify(std::string(serviceName.c_str()), filterOut, subscriptionReply);
            } catch (const std::exception &ex) {
                fmt::print("caught specific exception '{}'\n", ex.what());
            } catch (...) {
                fmt::print("caught unknown generic exception\n");
            }
        }
    }
};
