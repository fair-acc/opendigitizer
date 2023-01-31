#ifndef OPENDIGITIZER_SERVICE_ACQWORKER_H
#define OPENDIGITIZER_SERVICE_ACQWORKER_H

#include <majordomo/Worker.hpp>
#include "daq_api.hpp"
#include <ranges>
#include <string_view>

#include <cmath>
#include <chrono>
#include <unordered_map>
#include <utility>

namespace opendigitizer::acq {
using opencmw::Annotated;
using opencmw::NoUnit;

namespace rng = std::ranges;

namespace detail {
    // Type acts as a tag to find the correct operator| overload
    template <typename C>
    struct to_helper {
    };

    // This actually does the work
    template <typename Container, rng::range R>
    requires std::convertible_to<rng::range_value_t<R>, typename Container::value_type>
    Container operator|(R&& r, to_helper<Container>) {
        return Container{r.begin(), r.end()};
    }
}

// Couldn't find an concept for container, however a container is a range, but not a view.
template <rng::range Container>
requires (!rng::view<Container>)
auto to() {
    return detail::to_helper<Container>{};
}

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

template<units::basic_fixed_string serviceName, typename... Meta>
class AcquisitionWorker : public Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...> {
    std::jthread _notifyThread;
    std::chrono::milliseconds _rate;
public:
    using super_t = Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...>;
    template<typename BrokerType>
    explicit AcquisitionWorker(const BrokerType &broker, std::chrono::milliseconds rate) : super_t(broker, {}), _rate(rate) {
        _notifyThread = std::jthread([this, &rate](const std::stop_token& stoken) {
            fmt::print("acqWorker: starting notify thread\n");
            std::chrono::time_point update = std::chrono::system_clock::now();
            while (!stoken.stop_requested()) {
                fmt::print("acqWorker: active subscriptions: {}\n", super_t::activeSubscriptions() | std::ranges::views::transform([](auto &uri) {return uri.str();}));
                for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
                    if (subTopic.path() != serviceName.c_str()) {
                        continue;
                    }

                    const auto              queryMap = subTopic.queryParamMap();
                    const TimeDomainContext filterIn = opencmw::query::deserialise<TimeDomainContext>(queryMap);

                    try {
                        Acquisition reply;
                        si::time<nanosecond, ::int64_t> triggerStamp{std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count()};
                        handleGetRequest(triggerStamp, filterIn, reply);

                        TimeDomainContext filterOut = filterIn;
                        filterOut.contentType       = opencmw::MIME::JSON; // forces MIME just for demonstration purposes
                        auto res = super_t::notify(std::string(serviceName.c_str()), filterOut, reply);
                        fmt::print("acqWorker: update sent {}\n", res);
                    } catch (const std::exception &ex) {
                        fmt::print("caught exception '{}'\n", ex.what());
                    }
                }

                auto next_update = update + 2000ms;
                auto willSleepFor = next_update - std::chrono::system_clock::now();;
                if (willSleepFor > 0ms) {
                    std::this_thread::sleep_for(willSleepFor);
                }
                update = next_update;
            }
            fmt::print("acqWorker: stopped notify thread\n");
        });

        super_t::setCallback([this](RequestContext &rawCtx, const TimeDomainContext &requestContext, const Empty&, TimeDomainContext& /*replyContext*/, Acquisition &out) {
            if (rawCtx.request.command() == Command::Get) {
                // for real data, where we cannot generate the data on the fly, the get has to implement some sort of caching
                std::chrono::time_point update = std::chrono::system_clock::now();
                si::time<nanosecond, ::int64_t> triggerStamp{std::chrono::duration_cast<std::chrono::nanoseconds>(update.time_since_epoch()).count()};
                handleGetRequest(triggerStamp, requestContext, out);
            }
        });
    }

    ~AcquisitionWorker() {
        _notifyThread.request_stop();
        _notifyThread.join();
    }

    void handleGetRequest(const si::time<nanosecond, ::int64_t> updateStamp, const TimeDomainContext& ctx,  Acquisition &out) {
        using std::ranges::views::split, std::ranges::views::transform;
        using std::chrono::milliseconds, std::chrono::duration_cast, std::chrono::system_clock;
        constexpr std::size_t N_SAMPLES = 32;

        auto to_string = [](auto&& r) -> std::string { return {&*r.begin(), static_cast<std::size_t>(std::ranges::distance(r))}; };
        // auto to_vec = [](auto&& r) { return std::vector<std::string>{r.begin(), r.end()}; };
        out.channelNames = ctx.channelNameFilter | split(',') | transform(to_string) | to<std::vector<std::string>>();

        out.refTriggerStamp = updateStamp;
        out.refTriggerName = "PULSE_EVT";

        // missing move constructor... out.channelValues = {{}};
        out.channelValues.dimensions() = {static_cast<unsigned int>(out.channelNames.size()), N_SAMPLES};
        out.channelValues.elements().resize(out.channelNames.size() * N_SAMPLES);
        out.channelErrors.dimensions() = {static_cast<unsigned int>(out.channelNames.size()), N_SAMPLES};
        out.channelErrors.elements().resize(out.channelNames.size() * N_SAMPLES);
        out.channelTimeSinceRefTrigger.resize(N_SAMPLES);
        for (std::size_t i = 0; i < N_SAMPLES; i++) {
            out.channelTimeSinceRefTrigger[i] = static_cast<float>(i) * _rate.count() * 1e-3f / N_SAMPLES;
        }

        std::size_t n = 0;
        for (auto &name : out.channelNames) {
            if (name == "sine") {
                const double amplitude = 1.0;
                const int n_period = 20;
                for (std::size_t i = 0; i < N_SAMPLES; i++) {
                    const float t = (static_cast<float>((updateStamp.number() + i) % n_period) * 1e-9f) * _rate.count() * 1e-3f / N_SAMPLES;
                    out.channelValues.elements()[n * N_SAMPLES + i] = static_cast<float>(amplitude * std::sin(static_cast<double>(t / n_period) * std::numbers::pi));
                    out.channelErrors.elements()[n * N_SAMPLES + i] = 0.0f;
                }
                out.channelUnits.push_back("V");
                out.channelRangeMin.push_back(-1);
                out.channelRangeMax.push_back(1);
            } else if (name == "saw") {
                const double amplitude = 9.0;
                const int n_period = 13;
                for (std::size_t i = 0; i < N_SAMPLES; i++) {
                    const float t = (static_cast<float>((updateStamp.number() + i) % n_period) * 1e-9f) * _rate.count() * 1e-3f / N_SAMPLES;
                    out.channelValues.elements()[n * N_SAMPLES + i] = static_cast<float>(amplitude * static_cast<double>(t / n_period));
                    out.channelErrors.elements()[n * N_SAMPLES + i] = 0.01f;
                }
                out.channelUnits.push_back("A");
                out.channelRangeMin.push_back(0);
                out.channelRangeMax.push_back(9);
            }
            out.temperature.push_back(20);
            n++;
        }
    }
};
} // namespace opendigitizer::acq
#endif //OPENDIGITIZER_SERVICE_ACQWORKER_H
