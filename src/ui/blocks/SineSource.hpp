#ifndef OPENDIGITIZER_SINESOURCE_HPP
#define OPENDIGITIZER_SINESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>

#include <PeriodicTimer.hpp>

static inline auto sinProfiler = gr::profiling::null::Profiler{};

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::SineSource, [ float, double ]);
template<typename T>
requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>> {
    using Description = gr::Doc<R""(@brief source block generating a continuous sine wave synchronized to wall-clock time.

Two operating modes controlled by update_rate:
- update_rate == 0: Generates samples on-demand in processBulk based on elapsed wall-clock time
- update_rate > 0: A timer thread periodically triggers block progress at the specified rate

The sine phase is continuous with wall-clock time. When paused and resumed, the phase jumps
to the current wall-clock position.)"">;

    using ClockSourceType = std::chrono::system_clock;
    using TimePoint       = std::chrono::time_point<ClockSourceType>;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortOut<T> out;

    A<float, "frequency", gr::Unit<"Hz">, gr::Visible>                                       frequency   = 1.f;
    A<float, "amplitude", gr::Unit<"a.u.">>                                                  amplitude   = 1.f;
    A<float, "phase", gr::Unit<"°">>                                                         phase       = 0.f;
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"output sample rate">, gr::Visible>      sample_rate = 1000.f;
    A<float, "update_rate", gr::Unit<"Hz">, gr::Doc<"timer rate, 0=on-demand">, gr::Visible> update_rate = 0.f;

    GR_MAKE_REFLECTABLE(SineSource, out, frequency, amplitude, phase, sample_rate, update_rate);

    TimePoint         _startTime{};
    TimePoint         _lastUpdateTime{};
    std::atomic<bool> _timerDone{true};

    void start() {
        _startTime      = ClockSourceType::now();
        _lastUpdateTime = _startTime;
        startTimerIfNeeded();
    }

    void stop() { _timerDone.wait(false); }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (newSettings.contains("update_rate")) {
            startTimerIfNeeded();
        }
    }

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) {
        using namespace std::chrono_literals;
        thread_local static gr::profiling::PeriodicTimer timer{sinProfiler.forThisThread(), "SineSource", "processBulk", 2000ms, true};
        timer.begin();

        const TimePoint now            = ClockSourceType::now();
        const double    elapsedSeconds = std::chrono::duration<double>(now - _lastUpdateTime).count();
        const auto      samplesNeeded  = static_cast<std::size_t>(elapsedSeconds * static_cast<double>(sample_rate));
        const auto      nSamples       = std::min(samplesNeeded, output.size());

        if (nSamples == 0UZ) {
            output.publish(0UZ);
            return gr::work::Status::INSUFFICIENT_OUTPUT_ITEMS;
        }

        const double baseTime     = std::chrono::duration<double>(_lastUpdateTime - _startTime).count();
        const double phaseRad     = static_cast<double>(phase) * std::numbers::pi / 180.0;
        const double omega        = 2.0 * std::numbers::pi * static_cast<double>(frequency);
        const double samplePeriod = 1.0 / static_cast<double>(sample_rate);
        const double amp          = static_cast<double>(amplitude);

        for (std::size_t i = 0; i < nSamples; ++i) {
            const double t = baseTime + static_cast<double>(i) * samplePeriod;
            output[i]      = static_cast<T>(amp * std::sin(omega * t + phaseRad));
        }

        _lastUpdateTime = now;
        output.publish(nSamples);
        timer.snapshot("generate");

        return gr::work::Status::OK;
    }

    void startTimerIfNeeded() {
        if (update_rate <= 0.f) {
            return;
        }

        _timerDone = false;
        gr::thread_pool::Manager::defaultIoPool()->execute([this]() {
            gr::thread_pool::thread::setThreadName(std::format("timer:{}", this->name));

            TimePoint nextWakeUp = ClockSourceType::now();

            while (update_rate > 0.f && gr::lifecycle::isActive(this->state())) {
                nextWakeUp += std::chrono::microseconds(static_cast<long>(1e6f / update_rate));
                std::this_thread::sleep_until(nextWakeUp);

                if (this->state() != gr::lifecycle::State::PAUSED) {
                    this->progress->incrementAndGet();
                    this->progress->notify_all();
                }
            }

            _timerDone = true;
            _timerDone.notify_all();
        });
    }
};

} // namespace opendigitizer

auto registerSineSource = gr::registerBlock<opendigitizer::SineSource, float>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_SINESOURCE_HPP
