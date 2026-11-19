#ifndef OPENDIGITIZER_SIGNALSINK_HPP
#define OPENDIGITIZER_SIGNALSINK_HPP

#include "BlockTypes.hpp"
#include "gnuradio-4.0/basic/ConverterBlocks.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace opendigitizer {

/// minimum history capacities requested by named consumers, each until its expiry
struct CapacityRequests {
    using clock = std::chrono::steady_clock;
    struct Request {
        std::size_t       capacity;
        clock::time_point expiry;
    };

    std::size_t                              minimum = 0UZ;
    std::unordered_map<std::string, Request> _requests{};

    std::size_t request(std::string_view source, std::size_t capacity, clock::time_point expiry) {
        _requests[std::string(source)] = Request{capacity, expiry};
        return largest();
    }

    std::size_t expire(clock::time_point now) {
        std::erase_if(_requests, [now](const auto& entry) { return entry.second.expiry < now; });
        return largest();
    }

    [[nodiscard]] std::size_t largest() const noexcept {
        std::size_t result = minimum;
        for (const auto& [_, request] : _requests) {
            result = std::max(result, request.capacity);
        }
        return result;
    }
};

/**
 * @brief Line drawing style for signal rendering.
 *
 * Controls how the signal line is rendered in charts.
 */
enum class LineStyle : std::uint8_t {
    Solid,   ///< Continuous solid line (default)
    Dashed,  ///< Dashed line pattern (e.g., "- - - -")
    Dotted,  ///< Dotted line pattern (e.g., ". . . .")
    DashDot, ///< Alternating dash-dot pattern (e.g., "- . - .")
    None     ///< No line drawn (markers only)
};

/**
 * @brief RAII guard for thread-safe data access to signal sink buffers.
 *
 * Holds a lock on the sink's data mutex for the duration of its lifetime.
 * Data access methods on SignalSink are only safe to call while holding this guard.
 *
 * Usage:
 * @code
 * {
 *     auto guard = sink->dataGuard();
 *     // Safe to access sink data here
 *     for (std::size_t i = 0; i < sink->size(); ++i) {
 *         double x = sink->xAt(i);
 *         float y = sink->yAt(i);
 *     }
 * } // Lock released when guard goes out of scope
 * @endcode
 */
class DataGuard {
    std::unique_lock<std::mutex> _lock;

public:
    DataGuard() = default; // empty guard (no mutex locked)
    explicit DataGuard(std::mutex& mutex) : _lock(mutex) {}

    // Move-only (like unique_lock)
    DataGuard(DataGuard&&) noexcept            = default;
    DataGuard& operator=(DataGuard&&) noexcept = default;
    DataGuard(const DataGuard&)                = delete;
    DataGuard& operator=(const DataGuard&)     = delete;
};

/// one tensor dimension's coordinates, from the producer's gr:axis_* tags; without them, the cell index
struct ImageAxis {
    double      origin = 0.0; // of cell 0
    double      step   = 1.0;
    std::string name;
    std::string unit;
};

[[nodiscard]] constexpr double axisCoordinate(const ImageAxis& axis, std::size_t cell) noexcept { return axis.origin + axis.step * static_cast<double>(cell); }

/// latest 2D frame of a Tensor<T> sink, row-major {rows, columns}; uint32 pixels are packed 0xAARRGGBB
struct ImageFrame {
    std::variant<std::monostate, std::span<const std::uint32_t>, std::span<const float>, std::span<const double>> pixels;
    std::size_t                                                                                                   rows       = 0UZ;
    std::size_t                                                                                                   columns    = 0UZ;
    std::uint64_t                                                                                                 generation = 0U; // changes with every new frame
    std::array<ImageAxis, 2>                                                                                      axes{};          // {rows, columns}
};

/**
 * @brief Abstract interface for signal sink data access.
 *
 * This is a pure interface - it does NOT extend BlockModel.
 * Concrete implementations (like SinkAdapter<T>) hold a non-owning
 * pointer to the underlying block and delegate calls.
 *
 * Signal sinks are flowgraph endpoints that:
 * - Receive data via processBulk() (implemented in concrete Block<T>)
 * - Store data in internal buffers with mutex protection
 * - Provide thread-safe data access for rendering
 */
struct SignalSink {
    virtual ~SignalSink() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view uniqueName() const noexcept = 0;

    [[nodiscard]] virtual std::string_view signalName() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t    color() const noexcept      = 0;
    [[nodiscard]] virtual float            sampleRate() const noexcept = 0;

    [[nodiscard]] virtual float lineWidth() const noexcept = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept        = 0;
    [[nodiscard]] virtual double      xAt(std::size_t index) const = 0;
    [[nodiscard]] virtual float       yAt(std::size_t index) const = 0;

    [[nodiscard]] virtual bool                                hasDataSets() const noexcept  = 0;
    [[nodiscard]] virtual std::size_t                         dataSetCount() const noexcept = 0;
    [[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const              = 0;
    [[nodiscard]] virtual SignalKind                          signalKind() const noexcept   = 0;
    /// the queued frame with generation `next`, else the oldest queued after it; 0 = the newest. Valid while holding dataGuard()
    [[nodiscard]] virtual ImageFrame imageFrame([[maybe_unused]] std::uint64_t next = 0U) const { return {}; }

    /// an event sink's newest events, oldest first; empty for other sinks. Valid while holding dataGuard()
    [[nodiscard]] virtual std::span<const gr::property_map> events() const { return {}; }
    [[nodiscard]] virtual std::uint64_t                     eventGeneration() const noexcept { return 0U; }

    virtual void forEachTag(std::function<void(double timestamp, const gr::property_map& properties)> callback) const = 0;

    [[nodiscard]] virtual double timeLast() const noexcept = 0;

    /// total number of samples received since creation (not just those in current buffer)
    [[nodiscard]] virtual std::size_t totalSampleCount() const noexcept = 0;

    [[nodiscard]] virtual std::size_t bufferCapacity() const noexcept = 0;

    /// request minimum history capacity from a named source (auto-expires after timeout)
    virtual void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}, CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) = 0;

    /// expire old capacity requests and resize buffer if needed
    virtual void expireCapacityRequests(CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) = 0;

    /// acquire a guard for thread-safe data access
    [[nodiscard]] virtual DataGuard dataGuard() const = 0;

    virtual gr::work::Status draw(const gr::property_map& config = {}) = 0;

    [[nodiscard]] virtual bool drawEnabled() const noexcept = 0;

    [[nodiscard]] virtual std::string_view signalQuantity() const noexcept   = 0;
    [[nodiscard]] virtual std::string_view signalUnit() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view abscissaQuantity() const noexcept = 0;
    [[nodiscard]] virtual std::string_view abscissaUnit() const noexcept     = 0;
};

template<std::ranges::range TSinks>
[[nodiscard]] std::vector<DataGuard> lockAll(const TSinks& sinks) {
    std::vector<const SignalSink*> distinct = sinks | std::views::transform([](const auto& sink) -> const SignalSink* { return std::to_address(sink); }) | std::ranges::to<std::vector>();
    std::ranges::sort(distinct); // each sink's mutex once, in one order
    distinct.erase(std::ranges::unique(distinct).begin(), distinct.end());
    std::vector<DataGuard> guards;
    guards.reserve(distinct.size());
    for (const SignalSink* sink : distinct) {
        guards.push_back(sink->dataGuard());
    }
    return guards;
}

/**
 * @brief Non-owning adapter that implements SignalSink for a concrete sink Block<T>.
 *
 * This adapter holds a pointer to an existing block (owned by the graph) and
 * delegates all SignalSink calls to the block's methods.
 *
 * The underlying block T must provide the following methods for full functionality:
 * - signalName(), color(), sampleRate()
 * - signalQuantity(), signalUnit(), abscissaQuantity(), abscissaUnit()
 * - size(), xAt(index), yAt(index)
 * - dataMutex() returning std::mutex& for thread-safe access
 * - requestCapacity(consumer, capacity), releaseCapacity(consumer), bufferCapacity()
 */
template<gr::BlockLike T>
struct SinkAdapter : public SignalSink {
    std::atomic<T*> _block; // non-owning pointer to the underlying block (nulled on invalidation)

    // cached identity (stable after invalidation)
    std::string _cachedName;
    std::string _cachedUniqueName;

    // shared mutex that outlives the block — prevents TOCTOU between blockPtr() and dataMutex()
    std::shared_ptr<std::mutex> _sharedMutex;

    explicit SinkAdapter(T& block) : _block(&block), _cachedName(block.name.value), _cachedUniqueName(block.unique_name) {
        if constexpr (requires { block.sharedDataMutex(); }) {
            _sharedMutex = block.sharedDataMutex();
        }
    }

    ~SinkAdapter() override = default;

    // Non-copyable, non-movable (pointer to external block)
    SinkAdapter(const SinkAdapter&)            = delete;
    SinkAdapter& operator=(const SinkAdapter&) = delete;
    SinkAdapter(SinkAdapter&&)                 = delete;
    SinkAdapter& operator=(SinkAdapter&&)      = delete;

    /// called by the owning block's destructor to prevent dangling access
    void invalidate() noexcept { _block.store(nullptr, std::memory_order_release); }

    [[nodiscard]] T* blockPtr() const noexcept { return _block.load(std::memory_order_acquire); }

    [[nodiscard]] std::string_view name() const noexcept override { return _cachedName; }

    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _cachedUniqueName; }

    [[nodiscard]] std::string_view signalName() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return _cachedName;
        }
        if constexpr (requires { b->signalName(); }) {
            return b->signalName();
        } else {
            return b->name.value;
        }
    }

    [[nodiscard]] std::uint32_t color() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0xFFFFFF;
        }
        if constexpr (requires { b->color.value; }) {
            return b->color.value;
        }
        return 0xFFFFFF;
    }

    [[nodiscard]] float sampleRate() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 1.0f;
        }
        if constexpr (requires { b->sampleRate(); }) {
            return b->sampleRate();
        } else if constexpr (requires { b->sample_rate.value; }) {
            return static_cast<float>(b->sample_rate.value);
        }
        return 1.0f;
    }

    [[nodiscard]] float lineWidth() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 1.0f;
        }
        if constexpr (requires { b->lineWidth(); }) {
            return b->lineWidth();
        } else if constexpr (requires { b->line_width.value; }) {
            return static_cast<float>(b->line_width.value);
        }
        return 1.0f;
    }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return "";
        }
        if constexpr (requires { b->signalQuantity(); }) {
            return b->signalQuantity();
        } else if constexpr (requires { b->signal_quantity; }) {
            return b->signal_quantity.value;
        }
        return "";
    }

    [[nodiscard]] std::string_view signalUnit() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return "";
        }
        if constexpr (requires { b->signalUnit(); }) {
            return b->signalUnit();
        } else if constexpr (requires { b->signal_unit; }) {
            return b->signal_unit.value;
        }
        return "";
    }

    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return "time";
        }
        if constexpr (requires { b->abscissaQuantity(); }) {
            return b->abscissaQuantity();
        } else if constexpr (requires { b->abscissa_quantity; }) {
            return b->abscissa_quantity.value;
        }
        return "time";
    }

    [[nodiscard]] std::string_view abscissaUnit() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return "s";
        }
        if constexpr (requires { b->abscissaUnit(); }) {
            return b->abscissaUnit();
        } else if constexpr (requires { b->abscissa_unit; }) {
            return b->abscissa_unit.value;
        }
        return "s";
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0;
        }
        if constexpr (requires { b->size(); }) {
            return b->size();
        }
        return 0;
    }

    [[nodiscard]] double xAt(std::size_t index) const override {
        auto* b = blockPtr();
        if (!b) {
            return 0.0;
        }
        if constexpr (requires { b->xAt(index); }) {
            return b->xAt(index);
        }
        return 0.0;
    }

    [[nodiscard]] float yAt(std::size_t index) const override {
        auto* b = blockPtr();
        if (!b) {
            return 0.0f;
        }
        if constexpr (requires { b->yAt(index); }) {
            return b->yAt(index);
        }
        return 0.0f;
    }

    [[nodiscard]] bool hasDataSets() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return false;
        }
        if constexpr (requires { b->hasDataSets(); }) {
            return b->hasDataSets();
        }
        return false;
    }

    [[nodiscard]] std::size_t dataSetCount() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0;
        }
        if constexpr (requires { b->dataSetCount(); }) {
            return b->dataSetCount();
        }
        return 0;
    }

    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override {
        auto* b = blockPtr();
        if (!b) {
            return {};
        }
        if constexpr (requires { b->dataSets(); }) {
            return b->dataSets();
        }
        return {};
    }

    [[nodiscard]] SignalKind signalKind() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return {};
        }
        if constexpr (requires { b->signalKind(); }) {
            return b->signalKind();
        }
        return SignalKind::None;
    }

    [[nodiscard]] ImageFrame imageFrame(std::uint64_t next = 0U) const override {
        auto* b = blockPtr();
        if constexpr (requires { b->imageFrame(next); }) {
            if (b) {
                return b->imageFrame(next);
            }
        }
        return {};
    }

    [[nodiscard]] std::span<const gr::property_map> events() const override {
        auto* b = blockPtr();
        if constexpr (requires { b->events(); }) {
            if (b) {
                return b->events();
            }
        }
        return {};
    }

    [[nodiscard]] std::uint64_t eventGeneration() const noexcept override {
        auto* b = blockPtr();
        if constexpr (requires { b->eventGeneration(); }) {
            if (b) {
                return b->eventGeneration();
            }
        }
        return 0U;
    }

    void forEachTag(std::function<void(double, const gr::property_map&)> callback) const override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->forEachTag(callback); }) {
            b->forEachTag(callback);
        }
    }

    [[nodiscard]] double timeLast() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0.0;
        }
        if constexpr (requires { b->timeLast(); }) {
            return b->timeLast();
        }
        return 0.0;
    }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0;
        }
        if constexpr (requires { b->totalSampleCount(); }) {
            return b->totalSampleCount();
        } else if constexpr (requires { b->_sample_count; }) {
            return b->_sample_count;
        }
        return 0;
    }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0;
        }
        if constexpr (requires { b->bufferCapacity(); }) {
            return b->bufferCapacity();
        }
        return 0;
    }

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}, CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->requestCapacity(source, capacity, timeout, now); }) {
            b->requestCapacity(source, capacity, timeout, now);
        }
    }

    void expireCapacityRequests(CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->expireCapacityRequests(now); }) {
            b->expireCapacityRequests(now);
        }
    }

    [[nodiscard]] DataGuard dataGuard() const override {
        if (_sharedMutex) {
            return DataGuard(*_sharedMutex);
        }
        return DataGuard(_fallbackMutex);
    }

    gr::work::Status draw(const gr::property_map& config = {}) override {
        auto* b = blockPtr();
        if (!b) {
            return gr::work::Status::OK;
        }
        if constexpr (requires { b->draw(config); }) {
            return b->draw(config);
        }
        return gr::work::Status::OK;
    }

    [[nodiscard]] bool drawEnabled() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return true;
        }
        if constexpr (requires { b->visible.value; }) {
            return b->visible.value;
        }
        return true;
    }

private:
    mutable std::mutex _fallbackMutex; // per-instance fallback (replaces former static dummy)
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_SIGNALSINK_HPP
