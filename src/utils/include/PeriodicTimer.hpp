#ifndef GNURADIO_PERIODIC_TIMER_HPP
#define GNURADIO_PERIODIC_TIMER_HPP

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

#include <gnuradio-4.0/Profiler.hpp>

namespace gr::profiling {

inline constexpr std::size_t kMaxSegments   = 8UZ;
inline constexpr std::size_t kMaxMetrics    = 8UZ;
inline constexpr std::size_t kMaxTimestamps = kMaxSegments + 2UZ; // begin + segments + slack

inline constexpr std::size_t kBegin    = 0UZ;
inline constexpr std::size_t kPrevious = std::numeric_limits<std::size_t>::max();

namespace detail {

struct Stats {
    std::uint64_t            count{0};
    std::chrono::nanoseconds sum{0};
    double                   sum_sq{0.0}; // sum of squares in ms² for RMS
    std::chrono::nanoseconds min{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max{std::chrono::nanoseconds::min()};

    constexpr void add(std::chrono::nanoseconds d) noexcept {
        ++count;
        sum += d;
        const double ms = std::chrono::duration<double, std::milli>(d).count();
        sum_sq += ms * ms;
        if (d < min) {
            min = d;
        }
        if (d > max) {
            max = d;
        }
    }

    constexpr void reset() noexcept { *this = Stats{}; }

    [[nodiscard]] double avg_ms() const noexcept { return count > 0 ? std::chrono::duration<double, std::milli>(sum / count).count() : 0.0; }

    [[nodiscard]] double rms_ms() const noexcept {
        if (count < 2) {
            return 0.0;
        }
        const double avg      = avg_ms();
        const double variance = (sum_sq / static_cast<double>(count)) - (avg * avg);
        return variance > 0.0 ? std::sqrt(variance) : 0.0;
    }

    [[nodiscard]] double min_ms() const noexcept { return count > 0 ? std::chrono::duration<double, std::milli>(min).count() : 0.0; }

    [[nodiscard]] double max_ms() const noexcept { return count > 0 ? std::chrono::duration<double, std::milli>(max).count() : 0.0; }
};

template<typename T>
struct MetricStats {
    std::uint64_t count{0};
    T             sum{};
    double        sum_sq{0.0};
    T             min{std::numeric_limits<T>::max()};
    T             max{std::numeric_limits<T>::lowest()};

    constexpr void add(T v) noexcept {
        ++count;
        sum += v;
        const auto vd = static_cast<double>(v);
        sum_sq += vd * vd;
        if (v < min) {
            min = v;
        }
        if (v > max) {
            max = v;
        }
    }

    constexpr void reset() noexcept { *this = MetricStats{}; }

    [[nodiscard]] double avg() const noexcept { return count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0; }

    [[nodiscard]] double rms() const noexcept {
        if (count < 2) {
            return 0.0;
        }
        const double a        = avg();
        const double variance = (sum_sq / static_cast<double>(count)) - (a * a);
        return variance > 0.0 ? std::sqrt(variance) : 0.0;
    }
};

} // namespace detail

/**
 * @brief PeriodicTimer: Lightweight timing probe with periodic statistics reporting.
 *
 * Accumulates min/max/avg/rms statistics for named code segments and user-defined
 * metrics, emitting Chrome-trace counter events via Profiler.hpp at a configurable
 * interval. Compiles to no-ops under NDEBUG for zero-overhead release builds.
 *
 * Features:
 *   - Period tracking: time between successive begin() calls
 *   - Named segments: snapshot() with labels, relative to begin or previous snapshot
 *   - Custom metrics: arbitrary numeric values (CPU usage, queue depth, etc.)
 *   - Threshold alerts: instant events when period exceeds predicate
 *   - Chrome-trace integration: counter events with full statistics
 *
 * Usage (thread_local static for per-thread, per-site uniqueness):
 *
 *   void processLoop() {
 *       thread_local static PeriodicTimer tim{
 *           profiler.forThisThread(), "render", "diag", 2s, true
 *       };
 *
 *       tim.begin();
 *       waitForData();
 *       tim.snapshot("wait");           // time since begin
 *       processData();
 *       tim.snapshot("process");        // time since "wait"
 *       render();
 *       tim.snapshot("total", kBegin);  // time since begin (not since "process")
 *       tim.metric("queueDepth", queue.size());
 *   }
 *
 *   // With threshold alert (fires instant event if period > 100ms):
 *   tim.setPeriodThreshold([](auto d) { return d > 100ms; });
 *
 * Output format (every 2s):
 *   [render#12345] period: 33.33±0.21ms [32.8,34.1] (60) | wait: 16.2±0.1ms | process: 12.1±0.3ms
 */
struct PeriodicTimer {
    using Clock      = std::chrono::steady_clock;
    using time_point = Clock::time_point;
    using duration   = Clock::duration;

    static constexpr bool enabled =
#if defined(NDEBUG) && !defined(FORCE_PERIODIC_TIMERS)
        false;
#else
        true;
#endif

    using counter_fn_t = void (*)(void*, std::string_view, std::string_view, std::initializer_list<arg_value>);
    using instant_fn_t = void (*)(void*, std::string_view, std::string_view, std::initializer_list<arg_value>);

    struct Segment {
        detail::Stats stats{};
        std::string   label{};
        std::size_t   refIdx{0};
        bool          used{false};
    };

    struct Metric {
        detail::MetricStats<double> stats{};
        std::string                 label{};
        bool                        used{false};
    };

    void*        _handler{nullptr};
    counter_fn_t _counter_fn{nullptr};
    instant_fn_t _instant_fn{nullptr};

    std::string              _name{};
    std::string_view         _categories{};
    std::chrono::nanoseconds _interval{};
    bool                     _printToConsole{false};
    std::source_location     _loc{};

    bool       _began{false};
    time_point _last_report{};

    // timestamp tracking within iteration
    std::array<time_point, kMaxTimestamps> _timestamps{};
    std::size_t                            _timestamp_idx{0};
    std::size_t                            _next_seg_idx{0};

    // statistics
    detail::Stats                     _period{};
    std::array<Segment, kMaxSegments> _segments{};
    std::array<Metric, kMaxMetrics>   _metrics{};

    // period threshold predicate (type-erased)
    std::function<bool(std::chrono::nanoseconds)> _period_predicate{};

    PeriodicTimer() = default;

    template<typename Handler>
    requires ProfilerHandlerLike<Handler*>
    PeriodicTimer(Handler* handler, std::string_view name, std::string_view categories, std::chrono::nanoseconds interval, bool printToStderr = false, std::source_location loc = std::source_location::current()) noexcept //
        : _name(name), _categories(categories), _interval(interval), _printToConsole(printToStderr), _loc(loc) {
        if constexpr (enabled) {
            _handler     = handler;
            _counter_fn  = +[](void* h, std::string_view n, std::string_view c, std::initializer_list<arg_value> a) { static_cast<Handler*>(h)->counterEvent(n, c, a); };
            _instant_fn  = +[](void* h, std::string_view n, std::string_view c, std::initializer_list<arg_value> a) { static_cast<Handler*>(h)->instantEvent(n, c, a); };
            _last_report = Clock::now();
        }
    }

    void begin() noexcept {
        if constexpr (enabled) {
            if (!_handler) {
                return;
            }

            const auto now = Clock::now();

            if (_began) {
                _period.add(now - _timestamps[0]);
            }
            _began         = true;
            _timestamps[0] = now;
            _timestamp_idx = 1;
            _next_seg_idx  = 0;

            maybeFlush(now);
        }
    }

    // Snapshot with auto-incrementing segment index, relative to previous timestamp
    void snapshot() noexcept {
        if constexpr (enabled) {
            snapshotImpl("", _next_seg_idx, kPrevious, nullptr);
        }
    }

    // Snapshot with explicit reference index
    void snapshot(std::size_t refIdx) noexcept {
        if constexpr (enabled) {
            snapshotImpl("", _next_seg_idx, refIdx, nullptr);
        }
    }

    // Snapshot with label, relative to previous timestamp
    void snapshot(std::string_view label) noexcept {
        if constexpr (enabled) {
            snapshotImpl(label, _next_seg_idx, kPrevious, nullptr);
        }
    }

    // Snapshot with label and explicit reference index
    void snapshot(std::string_view label, std::size_t refIdx) noexcept {
        if constexpr (enabled) {
            snapshotImpl(label, _next_seg_idx, refIdx, nullptr);
        }
    }

    // Snapshot with label, reference index, and predicate for threshold alerts
    template<typename Pred>
    requires std::is_invocable_r_v<bool, Pred, std::chrono::nanoseconds>
    void snapshot(std::string_view label, std::size_t refIdx, Pred&& predicate) noexcept {
        if constexpr (enabled) {
            snapshotImpl(label, _next_seg_idx, refIdx, std::forward<Pred>(predicate));
        }
    }

    // Record a user-defined metric (point-in-time, added to trace only)
    template<typename T>
    requires std::is_arithmetic_v<T>
    void metric(std::string_view label, T value) noexcept {
        if constexpr (enabled) {
            metricImpl(label, value, nullptr);
        }
    }

    // Record a user-defined metric with predicate for threshold alerts
    template<typename T, typename Pred>
    requires std::is_arithmetic_v<T> && std::is_invocable_r_v<bool, Pred, T>
    void metric(std::string_view label, T value, Pred&& predicate) noexcept {
        if constexpr (enabled) {
            metricImpl(label, value, std::forward<Pred>(predicate));
        }
    }

    // Force flush of accumulated statistics
    void flush() noexcept {
        if constexpr (enabled) {
            if (_handler && _period.count > 0) {
                doFlush(Clock::now());
            }
        }
    }

    // Set threshold for period with callback
    template<typename Pred>
    requires std::is_invocable_r_v<bool, Pred, std::chrono::nanoseconds>
    void setPeriodThreshold(Pred&& predicate) noexcept {
        if constexpr (enabled) {
            _period_predicate = [p = std::forward<Pred>(predicate)](std::chrono::nanoseconds d) mutable { return p(d); };
        }
    }

private:
    template<typename Pred>
    void snapshotImpl(std::string_view label, std::size_t segIdx, std::size_t refIdx, Pred&& predicate) noexcept {
        if (!_handler || !_began) {
            return;
        }
        if (segIdx >= kMaxSegments) {
            return;
        }

        const auto now = Clock::now();

        // Determine reference timestamp
        std::size_t actualRef = (refIdx == kPrevious) ? (_timestamp_idx > 0 ? _timestamp_idx - 1 : 0) : refIdx;
        if (actualRef >= _timestamp_idx) {
            actualRef = 0;
        }

        const auto delta = now - _timestamps[actualRef];

        // Store current timestamp
        if (_timestamp_idx < kMaxTimestamps) {
            _timestamps[_timestamp_idx++] = now;
        }

        // Update segment stats
        auto& seg = _segments[segIdx];
        seg.stats.add(delta);
        if (!label.empty() && seg.label.empty()) {
            seg.label = std::string(label);
        }
        seg.refIdx = actualRef;
        seg.used   = true;

        ++_next_seg_idx;

        // Check predicate and emit alert if triggered
        if constexpr (!std::is_null_pointer_v<Pred>) {
            if (predicate(delta)) {
                emitThresholdAlert(seg.label.empty() ? std::format("s{}", segIdx) : seg.label, std::chrono::duration<double, std::milli>(delta).count());
            }
        }

        maybeFlush(now);
    }

    template<typename T, typename Pred>
    void metricImpl(std::string_view label, T value, Pred&& predicate) noexcept {
        if (!_handler) {
            return;
        }

        // Find or allocate metric slot
        std::size_t idx = kMaxMetrics;
        for (std::size_t i = 0; i < kMaxMetrics; ++i) {
            if (_metrics[i].label == label) {
                idx = i;
                break;
            }
            if (idx == kMaxMetrics && _metrics[i].label.empty()) {
                idx = i;
            }
        }
        if (idx >= kMaxMetrics) {
            return;
        }

        auto& m = _metrics[idx];
        if (m.label.empty()) {
            m.label = std::string(label);
        }
        m.stats.add(static_cast<double>(value));
        m.used = true;

        // Check predicate and emit alert if triggered
        if constexpr (!std::is_null_pointer_v<Pred>) {
            if (predicate(value)) {
                emitThresholdAlert(label, static_cast<double>(value));
            }
        }
    }

    void emitThresholdAlert(std::string_view label, double value) noexcept {
        const std::string alertName = std::format("{}::{}_ALERT", _name, label);
        _instant_fn(_handler, alertName, _categories, {{"value", value}, {"file", std::string(_loc.file_name())}, {"line", static_cast<int>(_loc.line())}});

        if (_printToConsole) {
            std::print(stderr, "[{}] ALERT: {}={:.3f} threshold exceeded\n", _name, label, value);
        }
    }

    void maybeFlush(time_point now) noexcept {
        if (_interval.count() > 0 && now - _last_report >= _interval) {
            doFlush(now);
        }
    }

    void doFlush(time_point now) noexcept {
        // Build trace event args dynamically (only used segments/metrics)
        std::vector<arg_value> args;
        args.reserve(32);

        const std::string file{_loc.file_name()};
        args.emplace_back("file", file);
        args.emplace_back("line", static_cast<int>(_loc.line()));

        // Period stats
        args.emplace_back("p_avg_ms", _period.avg_ms());
        args.emplace_back("p_rms_ms", _period.rms_ms());
        args.emplace_back("p_min_ms", _period.min_ms());
        args.emplace_back("p_max_ms", _period.max_ms());
        args.emplace_back("p_n", static_cast<int>(_period.count));

        // Segment stats (only used ones)
        for (std::size_t i = 0; i < kMaxSegments; ++i) {
            const auto& seg = _segments[i];
            if (!seg.used) {
                continue;
            }

            const std::string prefix = seg.label.empty() ? std::format("s{}", i) : seg.label;
            args.emplace_back(std::format("{}_avg_ms", prefix), seg.stats.avg_ms());
            args.emplace_back(std::format("{}_rms_ms", prefix), seg.stats.rms_ms());
            args.emplace_back(std::format("{}_min_ms", prefix), seg.stats.min_ms());
            args.emplace_back(std::format("{}_max_ms", prefix), seg.stats.max_ms());
            args.emplace_back(std::format("{}_n", prefix), static_cast<int>(seg.stats.count));
        }

        // Metric stats (only used ones)
        for (std::size_t i = 0; i < kMaxMetrics; ++i) {
            const auto& m = _metrics[i];
            if (!m.used) {
                continue;
            }

            args.emplace_back(std::format("{}_avg", m.label), m.stats.avg());
            args.emplace_back(std::format("{}_rms", m.label), m.stats.rms());
            args.emplace_back(std::format("{}_min", m.label), static_cast<double>(m.stats.min));
            args.emplace_back(std::format("{}_max", m.label), static_cast<double>(m.stats.max));
            args.emplace_back(std::format("{}_n", m.label), static_cast<int>(m.stats.count));
        }

        // Emit counter event (need to convert vector to initializer_list workaround)
        emitCounterEvent(args);

        // Stderr output
        if (_printToConsole) {
            printStats();
        }

        // Reset for next window
        _period.reset();
        for (auto& seg : _segments) {
            seg.stats.reset();
        }
        for (auto& m : _metrics) {
            m.stats.reset();
        }
        _last_report = now;
    }

    void emitCounterEvent(const std::vector<arg_value>& /*args*/) noexcept {
        const std::string file{_loc.file_name()};

        auto getSegArg = [&](std::size_t i, const char* suffix) -> double {
            const auto& seg = _segments[i];
            if (!seg.used) {
                return 0.0;
            }
            if (std::string_view(suffix) == "avg") {
                return seg.stats.avg_ms();
            }
            if (std::string_view(suffix) == "rms") {
                return seg.stats.rms_ms();
            }
            if (std::string_view(suffix) == "min") {
                return seg.stats.min_ms();
            }
            if (std::string_view(suffix) == "max") {
                return seg.stats.max_ms();
            }
            return 0.0;
        };

        auto getSegN = [&](std::size_t i) -> int { return _segments[i].used ? static_cast<int>(_segments[i].stats.count) : 0; };

        auto getSegLabel = [&](std::size_t i) -> std::string {
            if (!_segments[i].used) {
                return "";
            }
            return _segments[i].label.empty() ? std::format("s{}", i) : _segments[i].label;
        };

        _counter_fn(_handler, _name, _categories,
            {
                {"file", file},
                {"line", static_cast<int>(_loc.line())},

                {"p_avg_ms", _period.avg_ms()},
                {"p_rms_ms", _period.rms_ms()},
                {"p_min_ms", _period.min_ms()},
                {"p_max_ms", _period.max_ms()},
                {"p_n", static_cast<int>(_period.count)},

                {"s0_label", getSegLabel(0)},
                {"s0_avg_ms", getSegArg(0, "avg")},
                {"s0_rms_ms", getSegArg(0, "rms")},
                {"s0_min_ms", getSegArg(0, "min")},
                {"s0_max_ms", getSegArg(0, "max")},
                {"s0_n", getSegN(0)},
                {"s1_label", getSegLabel(1)},
                {"s1_avg_ms", getSegArg(1, "avg")},
                {"s1_rms_ms", getSegArg(1, "rms")},
                {"s1_min_ms", getSegArg(1, "min")},
                {"s1_max_ms", getSegArg(1, "max")},
                {"s1_n", getSegN(1)},
                {"s2_label", getSegLabel(2)},
                {"s2_avg_ms", getSegArg(2, "avg")},
                {"s2_rms_ms", getSegArg(2, "rms")},
                {"s2_min_ms", getSegArg(2, "min")},
                {"s2_max_ms", getSegArg(2, "max")},
                {"s2_n", getSegN(2)},
                {"s3_label", getSegLabel(3)},
                {"s3_avg_ms", getSegArg(3, "avg")},
                {"s3_rms_ms", getSegArg(3, "rms")},
                {"s3_min_ms", getSegArg(3, "min")},
                {"s3_max_ms", getSegArg(3, "max")},
                {"s3_n", getSegN(3)},
                {"s4_label", getSegLabel(4)},
                {"s4_avg_ms", getSegArg(4, "avg")},
                {"s4_rms_ms", getSegArg(4, "rms")},
                {"s4_min_ms", getSegArg(4, "min")},
                {"s4_max_ms", getSegArg(4, "max")},
                {"s4_n", getSegN(4)},
                {"s5_label", getSegLabel(5)},
                {"s5_avg_ms", getSegArg(5, "avg")},
                {"s5_rms_ms", getSegArg(5, "rms")},
                {"s5_min_ms", getSegArg(5, "min")},
                {"s5_max_ms", getSegArg(5, "max")},
                {"s5_n", getSegN(5)},
                {"s6_label", getSegLabel(6)},
                {"s6_avg_ms", getSegArg(6, "avg")},
                {"s6_rms_ms", getSegArg(6, "rms")},
                {"s6_min_ms", getSegArg(6, "min")},
                {"s6_max_ms", getSegArg(6, "max")},
                {"s6_n", getSegN(6)},
                {"s7_label", getSegLabel(7)},
                {"s7_avg_ms", getSegArg(7, "avg")},
                {"s7_rms_ms", getSegArg(7, "rms")},
                {"s7_min_ms", getSegArg(7, "min")},
                {"s7_max_ms", getSegArg(7, "max")},
                {"s7_n", getSegN(7)},
            });
    }

    void printStats() const noexcept {
        // format: [name] period: avg±rms [min,max] (N) | label: avg±rms [min,max] | ...
        std::print("[{}] period: {:.2f}±{:.2f}ms [{:.2f},{:.2f}] ({})", _name, _period.avg_ms(), _period.rms_ms(), _period.min_ms(), _period.max_ms(), _period.count);

        for (std::size_t i = 0; i < kMaxSegments; ++i) {
            const auto& seg = _segments[i];
            if (!seg.used || seg.stats.count == 0) {
                continue;
            }

            const std::string label = seg.label.empty() ? std::format("s{}", i) : seg.label;

            std::print(" | {}: {:.2f}±{:.2f}ms [{:.2f},{:.2f}]", label, seg.stats.avg_ms(), seg.stats.rms_ms(), seg.stats.min_ms(), seg.stats.max_ms());
        }

        for (std::size_t i = 0; i < kMaxMetrics; ++i) {
            const auto& m = _metrics[i];
            if (!m.used || m.stats.count == 0) {
                continue;
            }

            std::print(" | {}: {:.2f}±{:.2f} [{:.2f},{:.2f}]", m.label, m.stats.avg(), m.stats.rms(), static_cast<double>(m.stats.min), static_cast<double>(m.stats.max));
        }

        std::print("\n");
    }
};

} // namespace gr::profiling

#endif // GNURADIO_PERIODIC_TIMER_HPP
