#ifndef OPENDIGITIZER_SINESOURCE_HPP
#define OPENDIGITIZER_SINESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>

#include <PeriodicTimer.hpp>

using namespace gr::profiling;
// static inline auto sinProfiler = gr::profiling::Profiler{gr::profiling::Options{}}; // TODO: use this once GR4 is bumped, then remove
static inline auto sinProfiler = gr::profiling::null::Profiler{};

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::SineSource, [ float, double ]);
template<typename T>
requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>> {
    using Description = Doc<R""(@brief source block generating a continuous sine wave synchronized to wall-clock time.

Two operating modes controlled by update_rate:
- update_rate == 0: Generates samples on-demand in processBulk based on elapsed wall-clock time
- update_rate > 0: A timer thread periodically triggers block progress at the specified rate

The sine phase is continuous with wall-clock time. When paused and resumed, the phase jumps
to the current wall-clock position.)"">;

    using ClockSourceType = std::chrono::system_clock;
    using TimePoint       = std::chrono::time_point<ClockSourceType>;

    PortOut<T> out;

    Annotated<float, "frequency", Unit<"Hz">, Visible>                                   frequency   = 1.f;
    Annotated<float, "amplitude", Unit<"a.u.">>                                          amplitude   = 1.f;
    Annotated<float, "phase", Unit<"°">>                                                 phase       = 0.f;
    Annotated<float, "sample_rate", Unit<"Hz">, Doc<"output sample rate">, Visible>      sample_rate = 1000.f;
    Annotated<float, "update_rate", Unit<"Hz">, Doc<"timer rate, 0=on-demand">, Visible> update_rate = 0.f;

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

    void settingsChanged(const property_map& /*oldSettings*/, const property_map& newSettings) {
        if (newSettings.contains("update_rate")) {
            startTimerIfNeeded();
        }
    }

    work::Status processBulk(OutputSpanLike auto& output) {
        using namespace std::chrono_literals;
        thread_local static profiling::PeriodicTimer timer{sinProfiler.forThisThread(), "SineSource", "processBulk", 2000ms, true};
        timer.begin();

        const TimePoint now            = ClockSourceType::now();
        const double    elapsedSeconds = std::chrono::duration<double>(now - _lastUpdateTime).count();
        const auto      samplesNeeded  = static_cast<std::size_t>(elapsedSeconds * static_cast<double>(sample_rate));
        const auto      nSamples       = std::min(samplesNeeded, output.size());

        if (nSamples == 0UZ) {
            output.publish(0UZ);
            return work::Status::INSUFFICIENT_OUTPUT_ITEMS;
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

        return work::Status::OK;
    }

    void startTimerIfNeeded() {
        if (update_rate <= 0.f) {
            return;
        }

        _timerDone = false;
        thread_pool::Manager::defaultIoPool()->execute([this]() {
            thread_pool::thread::setThreadName(std::format("timer:{}", this->name));

            TimePoint nextWakeUp = ClockSourceType::now();

            while (update_rate > 0.f && lifecycle::isActive(this->state())) {
                nextWakeUp += std::chrono::microseconds(static_cast<long>(1e6f / update_rate));
                std::this_thread::sleep_until(nextWakeUp);

                if (this->state() != lifecycle::State::PAUSED) {
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
