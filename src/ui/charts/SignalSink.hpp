#ifndef OPENDIGITIZER_SIGNALSINK_HPP
#define OPENDIGITIZER_SIGNALSINK_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

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
    void*      userData;
    int        count;

    [[nodiscard]] bool empty() const noexcept { return count <= 0 || getter == nullptr; }
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
class SignalSink {
public:
    virtual ~SignalSink() = default;

    // --- Block identity (delegated to underlying block) ---

    [[nodiscard]] virtual std::string_view name() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view uniqueName() const noexcept = 0;

    // --- Sink-specific identity ---

    [[nodiscard]] virtual std::string_view signalName() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t    color() const noexcept      = 0;
    [[nodiscard]] virtual float            sampleRate() const noexcept = 0;

    // --- Indexed data access (primary API for rendering) ---

    [[nodiscard]] virtual std::size_t size() const noexcept        = 0;
    [[nodiscard]] virtual double      xAt(std::size_t index) const = 0;
    [[nodiscard]] virtual float       yAt(std::size_t index) const = 0;

    // --- ImPlot-compatible data accessor ---

    [[nodiscard]] virtual PlotData plotData() const = 0;

    // --- DataSet support ---

    [[nodiscard]] virtual bool                                hasDataSets() const noexcept  = 0;
    [[nodiscard]] virtual std::size_t                         dataSetCount() const noexcept = 0;
    [[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const              = 0;

    // --- Tag/timing event support ---

    [[nodiscard]] virtual bool                      hasStreamingTags() const noexcept                                                                    = 0;
    [[nodiscard]] virtual std::pair<double, double> tagTimeRange() const noexcept                                                                        = 0;
    virtual void forEachTag(std::function<void(double timestamp, const gr::property_map& properties)> callback) const = 0;

    // --- Time range ---

    [[nodiscard]] virtual double timeFirst() const noexcept = 0;
    [[nodiscard]] virtual double timeLast() const noexcept  = 0;

    // --- Buffer control ---

    [[nodiscard]] virtual std::size_t bufferCapacity() const noexcept                       = 0;
    virtual void                      requestCapacity(void* consumer, std::size_t capacity) = 0;
    virtual void                      releaseCapacity(void* consumer)                       = 0;

    // --- Thread-safe data access ---

    [[nodiscard]] virtual std::unique_lock<std::mutex> acquireDataLock() const = 0;

    // --- Work processing (for UI to trigger data consumption) ---

    virtual gr::work::Status invokeWork() = 0;

    // --- Drawing (for UI rendering) ---

    virtual gr::work::Status draw(const gr::property_map& config = {}) = 0;

    // --- Visibility control ---

    /**
     * @brief Check if signal should be drawn in charts.
     *
     * When false, charts should skip rendering this signal but still
     * consume data via invokeWork() to keep buffers flowing.
     */
    [[nodiscard]] virtual bool drawEnabled() const noexcept = 0;

    /**
     * @brief Enable or disable drawing of this signal in charts.
     *
     * @param enabled true to draw signal, false to hide (data still consumed)
     */
    virtual void setDrawEnabled(bool enabled) = 0;

    // --- Axis metadata ---

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
template<typename T>
class SinkAdapter : public SignalSink {
    T*   _block;              // Non-owning pointer to the underlying block
    bool _drawEnabled = true; // UI visibility state (independent of block)

public:
    explicit SinkAdapter(T& block) : _block(&block) {}

    ~SinkAdapter() override = default;

    // Non-copyable, non-movable (pointer to external block)
    SinkAdapter(const SinkAdapter&)            = delete;
    SinkAdapter& operator=(const SinkAdapter&) = delete;
    SinkAdapter(SinkAdapter&&)                 = delete;
    SinkAdapter& operator=(SinkAdapter&&)      = delete;

    // --- Block identity ---

    [[nodiscard]] std::string_view name() const noexcept override { return _block->name.value; }

    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _block->unique_name; }

    // --- SignalSink implementation ---

    [[nodiscard]] std::string_view signalName() const noexcept override {
        if constexpr (requires { _block->signalName(); }) {
            return _block->signalName();
        } else {
            return _block->name.value;
        }
    }

    [[nodiscard]] std::uint32_t color() const noexcept override {
        // ImPlotSink uses color_() method to avoid conflict with color member
        if constexpr (requires { _block->color_(); }) {
            return _block->color_();
        } else if constexpr (requires { _block->color.value; }) {
            // GR4 Annotated<uint32_t> member
            return _block->color.value;
        }
        return 0xFFFFFF; // Default white
    }

    [[nodiscard]] float sampleRate() const noexcept override {
        // ImPlotSink uses sampleRate_() method to avoid conflict with sample_rate member
        if constexpr (requires { _block->sampleRate_(); }) {
            return _block->sampleRate_();
        } else if constexpr (requires { _block->sample_rate.value; }) {
            // GR4 Annotated<float> member
            return static_cast<float>(_block->sample_rate.value);
        }
        return 1.0f;
    }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override {
        if constexpr (requires { _block->signalQuantity(); }) {
            return _block->signalQuantity();
        } else if constexpr (requires { _block->signal_quantity; }) {
            return _block->signal_quantity.value;
        }
        return "";
    }

    [[nodiscard]] std::string_view signalUnit() const noexcept override {
        if constexpr (requires { _block->signalUnit(); }) {
            return _block->signalUnit();
        } else if constexpr (requires { _block->signal_unit; }) {
            return _block->signal_unit.value;
        }
        return "";
    }

    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override {
        if constexpr (requires { _block->abscissaQuantity(); }) {
            return _block->abscissaQuantity();
        } else if constexpr (requires { _block->abscissa_quantity; }) {
            return _block->abscissa_quantity.value;
        }
        return "time";
    }

    [[nodiscard]] std::string_view abscissaUnit() const noexcept override {
        if constexpr (requires { _block->abscissaUnit(); }) {
            return _block->abscissaUnit();
        } else if constexpr (requires { _block->abscissa_unit; }) {
            return _block->abscissa_unit.value;
        }
        return "s";
    }

    [[nodiscard]] float signalMin() const noexcept override {
        if constexpr (requires { _block->signalMin(); }) {
            return _block->signalMin();
        } else if constexpr (requires { _block->signal_min; }) {
            return static_cast<float>(_block->signal_min);
        }
        return std::numeric_limits<float>::lowest();
    }

    [[nodiscard]] float signalMax() const noexcept override {
        if constexpr (requires { _block->signalMax(); }) {
            return _block->signalMax();
        } else if constexpr (requires { _block->signal_max; }) {
            return static_cast<float>(_block->signal_max);
        }
        return std::numeric_limits<float>::max();
    }

    [[nodiscard]] std::size_t size() const noexcept override {
        if constexpr (requires { _block->size(); }) {
            return _block->size();
        }
        return 0;
    }

    [[nodiscard]] double xAt(std::size_t index) const override {
        if constexpr (requires { _block->xAt(index); }) {
            return _block->xAt(index);
        }
        return 0.0;
    }

    [[nodiscard]] float yAt(std::size_t index) const override {
        if constexpr (requires { _block->yAt(index); }) {
            return _block->yAt(index);
        }
        return 0.0f;
    }

    [[nodiscard]] PlotData plotData() const override {
        if constexpr (requires { _block->plotData(); }) {
            return _block->plotData();
        }
        return {nullptr, nullptr, 0};
    }

    [[nodiscard]] bool hasDataSets() const noexcept override {
        if constexpr (requires { _block->hasDataSets(); }) {
            return _block->hasDataSets();
        }
        return false;
    }

    [[nodiscard]] std::size_t dataSetCount() const noexcept override {
        if constexpr (requires { _block->dataSetCount(); }) {
            return _block->dataSetCount();
        }
        return 0;
    }

    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override {
        if constexpr (requires { _block->dataSets(); }) {
            return _block->dataSets();
        }
        return {};
    }

    [[nodiscard]] bool hasStreamingTags() const noexcept override {
        if constexpr (requires { _block->hasStreamingTags(); }) {
            return _block->hasStreamingTags();
        }
        return false;
    }

    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept override {
        if constexpr (requires { _block->tagTimeRange(); }) {
            return _block->tagTimeRange();
        }
        return {0.0, 0.0};
    }

    void forEachTag(std::function<void(double, const gr::property_map&)> callback) const override {
        if constexpr (requires { _block->forEachTag(callback); }) {
            _block->forEachTag(callback);
        }
    }

    [[nodiscard]] double timeFirst() const noexcept override {
        if constexpr (requires { _block->timeFirst(); }) {
            return _block->timeFirst();
        }
        return 0.0;
    }

    [[nodiscard]] double timeLast() const noexcept override {
        if constexpr (requires { _block->timeLast(); }) {
            return _block->timeLast();
        }
        return 0.0;
    }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override {
        if constexpr (requires { _block->bufferCapacity(); }) {
            return _block->bufferCapacity();
        }
        return 0;
    }

    void requestCapacity(void* consumer, std::size_t capacity) override {
        if constexpr (requires { _block->requestCapacity(consumer, capacity); }) {
            _block->requestCapacity(consumer, capacity);
        }
    }

    void releaseCapacity(void* consumer) override {
        if constexpr (requires { _block->releaseCapacity(consumer); }) {
            _block->releaseCapacity(consumer);
        }
    }

    [[nodiscard]] std::unique_lock<std::mutex> acquireDataLock() const override {
        if constexpr (requires { _block->dataMutex(); }) {
            return std::unique_lock<std::mutex>(_block->dataMutex());
        }
        static std::mutex dummyMutex;
        return std::unique_lock<std::mutex>(dummyMutex);
    }

    gr::work::Status invokeWork() override {
        if constexpr (requires { _block->invokeWork(); }) {
            return _block->invokeWork();
        }
        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& config = {}) override {
        if constexpr (requires { _block->draw(config); }) {
            return _block->draw(config);
        }
        return gr::work::Status::OK;
    }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }

    void setDrawEnabled(bool enabled) override { _drawEnabled = enabled; }
};

} // namespace opendigitizer

// Backward compatibility aliases
namespace opendigitizer::charts {
using SignalSink = opendigitizer::SignalSink;
using PlotPoint  = opendigitizer::PlotPoint;
using PlotData   = opendigitizer::PlotData;
using PlotGetter = opendigitizer::PlotGetter;

template<typename T>
using SinkAdapter = opendigitizer::SinkAdapter<T>;
} // namespace opendigitizer::charts

// Legacy namespace alias
namespace opendigitizer::sinks {
using SignalSink = opendigitizer::SignalSink;
using PlotPoint  = opendigitizer::PlotPoint;
using PlotData   = opendigitizer::PlotData;
using PlotGetter = opendigitizer::PlotGetter;

template<typename T>
using SinkAdapter = opendigitizer::SinkAdapter<T>;
} // namespace opendigitizer::sinks

#endif // OPENDIGITIZER_SIGNALSINK_HPP
