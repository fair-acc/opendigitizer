#ifndef OPENDIGITIZER_UI_CHARTS_SIGNALSINK_HPP
#define OPENDIGITIZER_UI_CHARTS_SIGNALSINK_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/Tag.hpp>

namespace opendigitizer::ui::charts {

enum class XAxisMode : std::uint8_t { UtcTime, RelativeTime, SampleIndex };

struct TagData {
    double           timestamp;
    gr::property_map map;
};

struct HistoryRequirement {
    std::size_t                           requiredSize;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::milliseconds             timeout;
};

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

    // Helper: returns true if there's data to plot
    [[nodiscard]] bool empty() const noexcept { return count <= 0 || getter == nullptr; }
};

// Type-erased base for signal data access
class SignalSinkBase {
public:
    virtual ~SignalSinkBase() = default;

    // Identity
    [[nodiscard]] virtual std::string_view uniqueName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view signalName() const noexcept = 0;

    // Minimal metadata
    [[nodiscard]] virtual std::uint32_t color() const noexcept      = 0;
    [[nodiscard]] virtual float         sampleRate() const noexcept = 0;

    // Indexed data access (primary API for rendering)
    [[nodiscard]] virtual std::size_t size() const noexcept    = 0;
    [[nodiscard]] virtual double      xAt(std::size_t i) const = 0;
    [[nodiscard]] virtual float       yAt(std::size_t i) const = 0;

    // ImPlot-compatible data accessor
    [[nodiscard]] virtual PlotData plotData() const = 0;

    // DataSet support (for non-streaming signals)
    [[nodiscard]] virtual bool                                hasDataSets() const noexcept  = 0;
    [[nodiscard]] virtual std::size_t                         dataSetCount() const noexcept = 0;
    [[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const              = 0;

    // Time range
    [[nodiscard]] virtual double timeFirst() const noexcept = 0;
    [[nodiscard]] virtual double timeLast() const noexcept  = 0;

    // Buffer control
    [[nodiscard]] virtual std::size_t bufferCapacity() const noexcept                      = 0;
    virtual void                      requestCapacity(void* owner, std::size_t minSamples) = 0;
    virtual void                      releaseCapacity(void* owner)                         = 0;
};

// Singleton registry for signal sinks
class SignalSinkManager {
    std::unordered_map<std::string, SignalSinkBase*>                      _sinks;
    std::unordered_map<void*, std::function<void(SignalSinkBase&, bool)>> _listeners;

    SignalSinkManager() = default;

public:
    static SignalSinkManager& instance() {
        static SignalSinkManager mgr;
        return mgr;
    }

    void registerSink(SignalSinkBase* sink) {
        if (!sink) {
            return;
        }
        std::string name(sink->uniqueName());
        if (_sinks.contains(name)) {
            return;
        }
        _sinks[name] = sink;
        for (auto& [_, fn] : _listeners) {
            fn(*sink, true);
        }
    }

    template<typename T>
    void registerSink(const std::shared_ptr<T>& sink) {
        registerSink(sink.get());
    }

    void unregisterSink(SignalSinkBase* sink) {
        if (!sink) {
            return;
        }
        auto it = _sinks.find(std::string(sink->uniqueName()));
        if (it != _sinks.end()) {
            for (auto& [_, fn] : _listeners) {
                fn(*sink, false);
            }
            _sinks.erase(it);
        }
    }

    void unregisterSink(std::string_view name) {
        auto it = _sinks.find(std::string(name));
        if (it != _sinks.end()) {
            for (auto& [_, fn] : _listeners) {
                fn(*it->second, false);
            }
            _sinks.erase(it);
        }
    }

    template<typename Pred>
    SignalSinkBase* find(Pred pred) const {
        for (auto& [_, sink] : _sinks) {
            if (pred(*sink)) {
                return sink;
            }
        }
        return nullptr;
    }

    SignalSinkBase* findByName(std::string_view name) const {
        auto it = _sinks.find(std::string(name));
        return it != _sinks.end() ? it->second : nullptr;
    }

    SignalSinkBase* findSinkByName(std::string_view name) const { return findByName(name); }

    template<typename Fn>
    void forEach(Fn fn) {
        for (auto& [_, sink] : _sinks) {
            fn(*sink);
        }
    }

    [[nodiscard]] std::size_t count() const noexcept { return _sinks.size(); }
    [[nodiscard]] std::size_t sinkCount() const noexcept { return _sinks.size(); }

    void addListener(void* owner, std::function<void(SignalSinkBase&, bool)> fn) { _listeners[owner] = std::move(fn); }

    void removeListener(void* owner) { _listeners.erase(owner); }
};

} // namespace opendigitizer::ui::charts

#endif // OPENDIGITIZER_UI_CHARTS_SIGNALSINK_HPP
