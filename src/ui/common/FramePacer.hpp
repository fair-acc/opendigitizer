#ifndef OPENDIGITIZER_FRAME_PACER_HPP
#define OPENDIGITIZER_FRAME_PACER_HPP

#ifndef SDL_EVENTS_MOCK
#include <SDL3/SDL_events.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace DigitizerUi {

/**
 * @brief FramePacer: Event-driven rendering with min/max rate limiting.
 *
 * Replaces busy-wait vsync loops with true sleep via SDL_WaitEventTimeout.
 * Uses SDL_PushEvent to wake the main thread when requestFrame() is called
 * from worker threads (e.g., data arrival callbacks).
 *
 * Supports three rendering triggers:
 *   1. Event-driven: render when requestFrame() is called (input, data arrival)
 *   2. Minimum rate: guaranteed refresh even without events (e.g., 1 Hz for clock updates)
 *   3. Maximum rate: throttling to cap CPU/GPU usage (e.g., 60 Hz)
 *
 * Usage:
 *   // Main loop (native):
 *   auto& pacer = globalFramePacer();
 *   pacer.setMinRate(1.0);   // refresh at least 1 Hz
 *   pacer.setMaxRate(60.0);  // cap at 60 Hz
 *
 *   while (running) {
 *       SDL_WaitEventTimeout(nullptr, pacer.getWaitTimeoutMs());  // true sleep
 *       processEvents();  // input handlers call pacer.requestFrame()
 *       if (pacer.shouldRender()) {
 *           render();
 *           pacer.rendered();
 *       }
 *   }
 *
 *   // Data callback (worker thread):
 *   void onDataArrived(std::span<float> samples) {
 *       buffer.push(samples);
 *       globalFramePacer().requestFrame();  // wakes main thread via SDL event
 *   }
 */
struct FramePacer {
    using clock      = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using duration   = clock::duration;

    static inline std::uint32_t _sdlEventType{0};

    std::chrono::nanoseconds _maxPeriod;
    std::chrono::nanoseconds _minPeriod;
    time_point               _lastRender{clock::now() - _maxPeriod};
    std::atomic<bool>        _dirty{true};

    time_point                 _statsStart{clock::now()};
    std::atomic<std::uint64_t> _requestCount{0};
    std::atomic<std::uint64_t> _renderCount{0};

    constexpr FramePacer(std::chrono::nanoseconds maxPeriod = std::chrono::seconds{1}, std::chrono::nanoseconds minPeriod = std::chrono::milliseconds{16}) noexcept : _maxPeriod(maxPeriod), _minPeriod(minPeriod) {}

    static std::uint32_t sdlEventType() noexcept { return _sdlEventType; }

    void requestFrame() noexcept {
        static auto initSdlEvent = [] {
            _sdlEventType = SDL_RegisterEvents(1);
            return true;
        }();

        if (initSdlEvent && !_dirty.exchange(true, std::memory_order_acq_rel)) {
            SDL_Event event{.type = _sdlEventType};
            SDL_PushEvent(&event);
        }
        ++_requestCount;
    }

    [[nodiscard]] bool shouldRender() const noexcept {
        const auto sinceLast = clock::now() - _lastRender;
        return sinceLast >= _maxPeriod || (_dirty.load(std::memory_order_acquire) && sinceLast >= _minPeriod);
    }

    void rendered() noexcept {
        _lastRender = clock::now();
        _dirty.store(false, std::memory_order_release);
        ++_renderCount;
    }

    [[nodiscard]] int getWaitTimeoutMs() const noexcept {
        const auto sinceLast = clock::now() - _lastRender;
        const auto waitUntil = (_dirty.load(std::memory_order_acquire) ? _minPeriod : _maxPeriod) - sinceLast;

        if (waitUntil <= duration::zero()) {
            return 0;
        }

        const auto maxMs = std::chrono::duration_cast<std::chrono::milliseconds>(_maxPeriod).count();
        return static_cast<int>(std::clamp(std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil).count(), std::chrono::milliseconds::rep{1}, maxMs));
    }

    void                                   setMaxPeriod(std::chrono::nanoseconds period) noexcept { _maxPeriod = period; }
    void                                   setMinPeriod(std::chrono::nanoseconds period) noexcept { _minPeriod = period; }
    [[nodiscard]] std::chrono::nanoseconds maxPeriod() const noexcept { return _maxPeriod; }
    [[nodiscard]] std::chrono::nanoseconds minPeriod() const noexcept { return _minPeriod; }

    void                 setMinRate(double hz) noexcept { _maxPeriod = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / hz)); }
    void                 setMaxRate(double hz) noexcept { _minPeriod = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / hz)); }
    [[nodiscard]] double minRateHz() const noexcept { return 1e9 / static_cast<double>(_maxPeriod.count()); }
    [[nodiscard]] double maxRateHz() const noexcept { return 1e9 / static_cast<double>(_minPeriod.count()); }

    [[nodiscard]] bool          isDirty() const noexcept { return _dirty.load(std::memory_order_acquire); }
    [[nodiscard]] std::uint64_t requestCount() const noexcept { return _requestCount.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t renderCount() const noexcept { return _renderCount.load(std::memory_order_relaxed); }

    [[nodiscard]] double measuredFps() const noexcept {
        const auto elapsed = std::chrono::duration<double>(clock::now() - _statsStart).count();
        return elapsed < 0.001 ? 0.0 : static_cast<double>(_renderCount.load()) / elapsed;
    }

    void resetMeasurement() noexcept {
        _statsStart = clock::now();
        _renderCount.store(0, std::memory_order_relaxed);
        _requestCount.store(0, std::memory_order_relaxed);
    }
};

inline FramePacer& globalFramePacer() {
    static FramePacer instance{std::chrono::seconds{1}, std::chrono::milliseconds{16}};
    return instance;
}

} // namespace DigitizerUi

#endif // OPENDIGITIZER_FRAME_PACER_HPP
