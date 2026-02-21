#ifndef OPENDIGITIZER_SIGNALSINK_HPP
#define OPENDIGITIZER_SIGNALSINK_HPP

#include "gnuradio-4.0/basic/ConverterBlocks.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace opendigitizer {

// ImPlot-compatible point struct (mirrors ImPlotPoint but library-independent)
struct PlotPoint {
    double x;
    double y;
};

// Function pointer type compatible with ImPlotGetter
using PlotGetter = PlotPoint (*)(int idx, void* userData);

// Data accessor struct that can be directly passed to ImPlot's PlotLineG
struct PlotData {
    PlotGetter getter;
    void*      user_data;
    int        count;

    [[nodiscard]] bool empty() const noexcept { return count <= 0 || getter == nullptr; }
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

    /// Release the lock early (before destruction)
    void release() { _lock.unlock(); }

    /// Check if lock is still held
    [[nodiscard]] bool ownsLock() const noexcept { return _lock.owns_lock(); }
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

    [[nodiscard]] virtual LineStyle lineStyle() const noexcept = 0;

    [[nodiscard]] virtual float lineWidth() const noexcept = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept        = 0;
    [[nodiscard]] virtual double      xAt(std::size_t index) const = 0;
    [[nodiscard]] virtual float       yAt(std::size_t index) const = 0;

    [[nodiscard]] virtual PlotData plotData() const = 0;

    [[nodiscard]] virtual bool                                hasDataSets() const noexcept  = 0;
    [[nodiscard]] virtual std::size_t                         dataSetCount() const noexcept = 0;
    [[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const              = 0;

    [[nodiscard]] virtual bool                      hasStreamingTags() const noexcept                                                                    = 0;
    [[nodiscard]] virtual std::pair<double, double> tagTimeRange() const noexcept                                                                        = 0;
    virtual void                                    forEachTag(std::function<void(double timestamp, const gr::property_map& properties)> callback) const = 0;

    [[nodiscard]] virtual double timeFirst() const noexcept = 0;
    [[nodiscard]] virtual double timeLast() const noexcept  = 0;

    /// total number of samples received since creation (not just those in current buffer)
    [[nodiscard]] virtual std::size_t totalSampleCount() const noexcept = 0;

    [[nodiscard]] virtual std::size_t bufferCapacity() const noexcept = 0;

    /// request minimum history capacity from a named source (auto-expires after timeout)
    virtual void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}) = 0;

    /// expire old capacity requests and resize buffer if needed
    virtual void expireCapacityRequests() = 0;

    struct DataRange {
        std::size_t start_index; // index of first element in range
        std::size_t count;       // number of elements in range

        [[nodiscard]] bool empty() const noexcept { return count == 0; }
    };

    [[nodiscard]] virtual DataRange getXRange(double tMin, double tMax) const = 0;

    [[nodiscard]] virtual DataRange getTagRange(double tMin, double tMax) const = 0;

    struct XRangeResult {
        std::span<const double> data;         // X values in the requested range
        double                  actual_t_min; // actual start time of returned data
        double                  actual_t_max; // actual end time of returned data

        [[nodiscard]] bool empty() const noexcept { return data.empty(); }
    };

    struct YRangeResult {
        std::span<const float>                    data;         // Y values in the requested range
        double                                    actual_t_min; // actual start time of returned data
        double                                    actual_t_max; // actual end time of returned data
        std::shared_ptr<const std::vector<float>> _storage;     // keeps converted data alive when T != float

        [[nodiscard]] bool empty() const noexcept { return data.empty(); }
    };

    struct TagEntry {
        double           timestamp;  // time of the tag in seconds (UTC)
        gr::property_map properties; // tag properties
    };

    struct TagRangeResult {
        std::vector<TagEntry> tags;         // tags in the requested range
        double                actual_t_min; // actual start time of returned data
        double                actual_t_max; // actual end time of returned data

        [[nodiscard]] bool empty() const noexcept { return tags.empty(); }
    };

    /// get X values within a time range as a span (caller must hold dataGuard())
    [[nodiscard]] virtual XRangeResult getX(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const = 0;

    /// get Y values within a time range as a span (caller must hold dataGuard())
    [[nodiscard]] virtual YRangeResult getY(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const = 0;

    /// get tags within a time range (tags are copied for safe access without data guard)
    [[nodiscard]] virtual TagRangeResult getTags(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const = 0;

    struct SampleWithTags {
        double                            x;    // X value (time)
        float                             y;    // Y value
        std::span<const gr::property_map> tags; // tags at this sample (empty if none)
    };

    /// forward iterator for lazy (x, y, tags) range traversal
    struct XYTagIterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type        = SampleWithTags;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const SampleWithTags*;
        using reference         = SampleWithTags;

        const SignalSink* _sink  = nullptr;
        std::size_t       _index = 0;
        std::size_t       _end   = 0;

        // Tag lookup cache (for tags at current index)
        mutable std::vector<gr::property_map> _tagCache;
        mutable std::size_t                   _cachedIndex = std::numeric_limits<std::size_t>::max();

        void updateTagCache() const {
            if (!_sink || _cachedIndex == _index) {
                return;
            }
            _tagCache.clear();
            _cachedIndex = _index;

            // Get the X value at current index to find matching tags
            double xVal = _sink->xAt(_index);

            // Use forEachTag to find tags near this X value (within tolerance)
            constexpr double kTagTolerance = 1e-9; // nanosecond precision
            _sink->forEachTag([this, xVal](double timestamp, const gr::property_map& props) {
                if (std::abs(timestamp - xVal) < kTagTolerance) {
                    _tagCache.push_back(props);
                }
            });
        }

        XYTagIterator() = default;

        XYTagIterator(const SignalSink* sink, std::size_t index, std::size_t end) : _sink(sink), _index(index), _end(end) {}

        reference operator*() const {
            if (_index >= _end) {
                return SampleWithTags{0.0, 0.0f, {}};
            }
            updateTagCache();
            return SampleWithTags{_sink->xAt(_index), _sink->yAt(_index), std::span<const gr::property_map>(_tagCache)};
        }

        XYTagIterator& operator++() {
            ++_index;
            return *this;
        }

        XYTagIterator operator++(int) {
            XYTagIterator tmp = *this;
            ++(*this);
            return tmp;
        }

        bool operator==(const XYTagIterator& other) const { return _index == other._index; }
        bool operator!=(const XYTagIterator& other) const { return _index != other._index; }
    };

    /// range wrapper for XYTagIterator (satisfies std::ranges::range)
    struct XYTagRange {
        XYTagIterator _begin;
        XYTagIterator _end;

        XYTagRange() = default;
        XYTagRange(XYTagIterator begin, XYTagIterator end) : _begin(begin), _end(end) {}

        [[nodiscard]] XYTagIterator begin() const { return _begin; }
        [[nodiscard]] XYTagIterator end() const { return _end; }
        [[nodiscard]] bool          empty() const { return _begin == _end; }
    };

    /// get a lazy range over (x, y, tags) tuples (caller must hold dataGuard())
    [[nodiscard]] virtual XYTagRange xyTagRange(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const = 0;

    /// remove tags with timestamp < minX (called after rendering to prevent unbounded growth)
    virtual void pruneTags(double minX) = 0;

    /// acquire a guard for thread-safe data access
    [[nodiscard]] virtual DataGuard dataGuard() const = 0;

    virtual gr::work::Status draw(const gr::property_map& config = {}) = 0;

    [[nodiscard]] virtual bool drawEnabled() const noexcept = 0;

    virtual void setDrawEnabled(bool enabled) = 0;

    virtual void setColor(std::uint32_t color)      = 0;
    virtual void setLineStyle(LineStyle style)      = 0;
    virtual void setLineWidth(float width)          = 0;
    virtual void setSignalName(std::string_view nm) = 0;

    [[nodiscard]] virtual std::string_view signalQuantity() const noexcept   = 0;
    [[nodiscard]] virtual std::string_view signalUnit() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view abscissaQuantity() const noexcept = 0;
    [[nodiscard]] virtual std::string_view abscissaUnit() const noexcept     = 0;
    [[nodiscard]] virtual float            signalMin() const noexcept        = 0;
    [[nodiscard]] virtual float            signalMax() const noexcept        = 0;
};

/**
 * @brief Non-owning adapter that implements SignalSink for a concrete sink Block<T>.
 *
 * This adapter holds a pointer to an existing block (owned by the graph) and
 * delegates all SignalSink calls to the block's methods.
 *
 * The underlying block T must provide the following methods for full functionality:
 * - signalName(), color(), sampleRate()
 * - signalQuantity(), signalUnit(), abscissaQuantity(), abscissaUnit()
 * - signalMin(), signalMax()
 * - size(), xAt(index), yAt(index)
 * - dataMutex() returning std::mutex& for thread-safe access
 * - requestCapacity(consumer, capacity), releaseCapacity(consumer), bufferCapacity()
 */
template<gr::BlockLike T>
struct SinkAdapter : public SignalSink {
    std::atomic<T*>   _block;             // non-owning pointer to the underlying block (nulled on invalidation)
    std::atomic<bool> _drawEnabled{true}; // UI visibility state (independent of block)

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

    [[nodiscard]] LineStyle lineStyle() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return LineStyle::Solid;
        }
        if constexpr (requires { b->lineStyle(); }) {
            return b->lineStyle();
        } else if constexpr (requires { b->signal_line_style.value; }) {
            return static_cast<LineStyle>(b->signal_line_style.value);
        } else if constexpr (requires { b->line_style.value; }) {
            return static_cast<LineStyle>(b->line_style.value);
        }
        return LineStyle::Solid;
    }

    [[nodiscard]] float lineWidth() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 1.0f;
        }
        if constexpr (requires { b->lineWidth(); }) {
            return b->lineWidth();
        } else if constexpr (requires { b->signal_line_width.value; }) {
            return static_cast<float>(b->signal_line_width.value);
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

    [[nodiscard]] float signalMin() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return std::numeric_limits<float>::lowest();
        }
        if constexpr (requires { b->signalMin(); }) {
            return b->signalMin();
        } else if constexpr (requires { b->signal_min; }) {
            return static_cast<float>(b->signal_min);
        }
        return std::numeric_limits<float>::lowest();
    }

    [[nodiscard]] float signalMax() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return std::numeric_limits<float>::max();
        }
        if constexpr (requires { b->signalMax(); }) {
            return b->signalMax();
        } else if constexpr (requires { b->signal_max; }) {
            return static_cast<float>(b->signal_max);
        }
        return std::numeric_limits<float>::max();
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

    [[nodiscard]] PlotData plotData() const override {
        auto* b = blockPtr();
        if (!b) {
            return {nullptr, nullptr, 0};
        }
        if constexpr (requires { b->plotData(); }) {
            return b->plotData();
        }
        return {nullptr, nullptr, 0};
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

    [[nodiscard]] bool hasStreamingTags() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return false;
        }
        if constexpr (requires { b->hasStreamingTags(); }) {
            return b->hasStreamingTags();
        }
        return false;
    }

    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return {0.0, 0.0};
        }
        if constexpr (requires { b->tagTimeRange(); }) {
            return b->tagTimeRange();
        }
        return {0.0, 0.0};
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

    [[nodiscard]] double timeFirst() const noexcept override {
        auto* b = blockPtr();
        if (!b) {
            return 0.0;
        }
        if constexpr (requires { b->timeFirst(); }) {
            return b->timeFirst();
        }
        return 0.0;
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

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->requestCapacity(source, capacity, timeout); }) {
            b->requestCapacity(source, capacity, timeout);
        }
    }

    void expireCapacityRequests() override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->expireCapacityRequests(); }) {
            b->expireCapacityRequests();
        }
    }

    [[nodiscard]] DataRange getXRange(double tMin, double tMax) const override {
        auto* b = blockPtr();
        if (!b) {
            return {0, 0};
        }
        if constexpr (requires { b->getXRange(tMin, tMax); }) {
            return b->getXRange(tMin, tMax);
        }
        return {0, 0};
    }

    [[nodiscard]] DataRange getTagRange(double tMin, double tMax) const override {
        auto* b = blockPtr();
        if (!b) {
            return {0, 0};
        }
        if constexpr (requires { b->getTagRange(tMin, tMax); }) {
            return b->getTagRange(tMin, tMax);
        }
        return {0, 0};
    }

    [[nodiscard]] XRangeResult getX(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const override {
        auto* b = blockPtr();
        if (!b) {
            return {{}, 0.0, 0.0};
        }
        if constexpr (requires { b->getX(tMin, tMax); }) {
            return b->getX(tMin, tMax);
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] YRangeResult getY(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const override {
        auto* b = blockPtr();
        if (!b) {
            return {{}, 0.0, 0.0};
        }
        if constexpr (requires { b->getY(tMin, tMax); }) {
            return b->getY(tMin, tMax);
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] TagRangeResult getTags(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const override {
        auto* b = blockPtr();
        if (!b) {
            return {{}, 0.0, 0.0};
        }
        if constexpr (requires { b->getTags(tMin, tMax); }) {
            return b->getTags(tMin, tMax);
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] XYTagRange xyTagRange(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const override {
        auto range = getXRange(tMin, tMax);
        if (range.empty()) {
            return XYTagRange{};
        }
        return XYTagRange{XYTagIterator{this, range.start_index, range.start_index + range.count}, XYTagIterator{this, range.start_index + range.count, range.start_index + range.count}};
    }

    void pruneTags(double minX) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->pruneTags(minX); }) {
            b->pruneTags(minX);
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

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled.load(std::memory_order_relaxed); }

    void setDrawEnabled(bool enabled) override { _drawEnabled.store(enabled, std::memory_order_relaxed); }

    void setColor(std::uint32_t c) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->color.value; }) {
            b->color    = c;
            std::ignore = b->settings().set({{"color", c}});
            std::ignore = b->settings().applyStagedParameters();
        }
    }

    void setLineStyle(LineStyle style) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->signal_line_style.value; }) {
            b->signal_line_style = static_cast<std::uint8_t>(style);
            std::ignore          = b->settings().set({{"signal_line_style", static_cast<std::uint8_t>(style)}});
            std::ignore          = b->settings().applyStagedParameters();
        } else if constexpr (requires { b->line_style.value; }) {
            b->line_style = static_cast<std::uint8_t>(style);
            std::ignore   = b->settings().set({{"line_style", static_cast<std::uint8_t>(style)}});
            std::ignore   = b->settings().applyStagedParameters();
        }
    }

    void setLineWidth(float width) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->signal_line_width.value; }) {
            b->signal_line_width = width;
            std::ignore          = b->settings().set({{"signal_line_width", width}});
            std::ignore          = b->settings().applyStagedParameters();
        } else if constexpr (requires { b->line_width.value; }) {
            b->line_width = width;
            std::ignore   = b->settings().set({{"line_width", width}});
            std::ignore   = b->settings().applyStagedParameters();
        }
    }

    void setSignalName(std::string_view nm) override {
        auto* b = blockPtr();
        if (!b) {
            return;
        }
        if constexpr (requires { b->signal_name.value; }) {
            b->signal_name = std::string(nm);
            std::ignore    = b->settings().set({{"signal_name", std::string(nm)}});
            std::ignore    = b->settings().applyStagedParameters();
        }
    }

private:
    mutable std::mutex _fallbackMutex; // per-instance fallback (replaces former static dummy)
};

} // namespace opendigitizer

// Backward compatibility aliases
namespace opendigitizer::charts {
using SignalSink = opendigitizer::SignalSink;
using DataGuard  = opendigitizer::DataGuard;
using PlotPoint  = opendigitizer::PlotPoint;
using PlotData   = opendigitizer::PlotData;
using PlotGetter = opendigitizer::PlotGetter;
using LineStyle  = opendigitizer::LineStyle;

template<gr::BlockLike T>
using SinkAdapter = opendigitizer::SinkAdapter<T>;
} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_SIGNALSINK_HPP
