#ifndef OPENDIGITIZER_UI_CHARTS_CHART_HPP
#define OPENDIGITIZER_UI_CHARTS_CHART_HPP

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/Tag.hpp>

#include "SignalSink.hpp"

namespace opendigitizer::ui::charts {

/**
 * @brief Axis scale modes for chart rendering.
 */
enum class AxisScale {
    Linear = 0,    // Standard linear scale [min, max]
    LinearReverse, // Reversed linear scale [max, min]
    Time,          // Datetime/timestamp scale
    Log10,         // Logarithmic base 10
    SymLog         // Symmetric log (handles negative values)
};

/**
 * @brief Label formatting options for axis values.
 */
enum class LabelFormat {
    Auto = 0,     // Automatic based on range
    Metric,       // SI prefixes (k, M, G, etc.)
    MetricInline, // SI prefixes inline with value
    Scientific,   // Scientific notation
    None,         // No labels
    Default       // Default floating-point format
};

/**
 * @brief Configuration for a single chart axis.
 */
struct AxisConfig {
    float         min    = std::numeric_limits<float>::lowest();
    float         max    = std::numeric_limits<float>::max();
    AxisScale     scale  = AxisScale::Linear;
    LabelFormat   format = LabelFormat::Auto;
    std::string   label;
    std::string   unit;
    bool          autoScale = true;
    bool          showGrid  = true;
    std::uint32_t color     = 0xFFFFFFFF;
};

/**
 * @brief Chart configuration properties.
 */
struct ChartConfig {
    std::string name;
    std::string title;

    // X-axis mode determines how time is displayed
    XAxisMode xAxisMode = XAxisMode::RelativeTime;

    // Axis configurations (up to 3 each)
    std::array<AxisConfig, 3> xAxes{};
    std::array<AxisConfig, 3> yAxes{};

    // Display options
    bool showLegend   = true;
    bool showTags     = true;
    bool showGrid     = true;
    bool antiAliasing = true;

    // History display for DataSets
    std::size_t maxHistoryCount       = 3;
    float       historyOpacityDecay   = 0.3f;
    float       historyVerticalOffset = 0.0f;
};

/**
 * @brief Abstract base class for all chart types.
 *
 * Charts are responsible for:
 * - Managing a collection of SignalSinks
 * - Rendering the chart using the underlying graphics library (ImPlot)
 * - Handling user interactions (context menus, axis popups, etc.)
 *
 * Charts do NOT own the SignalSinks - they hold shared_ptr references
 * to sinks that are managed by the SignalSinkManager.
 */
class Chart {
public:
    virtual ~Chart() = default;

    // --- Identity ---
    [[nodiscard]] virtual std::string_view chartTypeName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view uniqueId() const noexcept      = 0;

    // --- Configuration ---
    [[nodiscard]] virtual const ChartConfig& config() const noexcept              = 0;
    virtual void                             setConfig(const ChartConfig& config) = 0;

    // --- Signal sink management ---

    /**
     * @brief Add a signal sink to this chart.
     * @param sink Shared pointer to the sink.
     */
    virtual void addSignalSink(std::shared_ptr<SignalSinkBase> sink) {
        if (!sink) {
            return;
        }
        auto it = std::find(_signalSinks.begin(), _signalSinks.end(), sink);
        if (it == _signalSinks.end()) {
            _signalSinks.push_back(std::move(sink));
            onSignalSinkAdded(*_signalSinks.back());
        }
    }

    /**
     * @brief Remove a signal sink from this chart.
     * @param sink Shared pointer to the sink.
     */
    virtual void removeSignalSink(const std::shared_ptr<SignalSinkBase>& sink) {
        auto it = std::find(_signalSinks.begin(), _signalSinks.end(), sink);
        if (it != _signalSinks.end()) {
            onSignalSinkRemoved(**it);
            _signalSinks.erase(it);
        }
    }

    /**
     * @brief Remove a signal sink by unique name.
     */
    virtual void removeSignalSink(std::string_view uniqueName) {
        auto it = std::find_if(_signalSinks.begin(), _signalSinks.end(), [uniqueName](const auto& s) { return s->uniqueName() == uniqueName; });
        if (it != _signalSinks.end()) {
            onSignalSinkRemoved(**it);
            _signalSinks.erase(it);
        }
    }

    /**
     * @brief Get all signal sinks attached to this chart.
     */
    [[nodiscard]] const std::vector<std::shared_ptr<SignalSinkBase>>& signalSinks() const noexcept { return _signalSinks; }

    /**
     * @brief Get the number of signal sinks.
     */
    [[nodiscard]] std::size_t signalSinkCount() const noexcept { return _signalSinks.size(); }

    /**
     * @brief Move all signal sinks to another chart.
     * @param target The target chart to receive the sinks.
     */
    void moveSignalSinksTo(Chart& target) {
        for (auto& sink : _signalSinks) {
            target.addSignalSink(std::move(sink));
        }
        _signalSinks.clear();
    }

    /**
     * @brief Copy all signal sink references to another chart.
     * @param target The target chart to receive copies of the sink references.
     */
    void copySignalSinksTo(Chart& target) const {
        for (const auto& sink : _signalSinks) {
            target.addSignalSink(sink);
        }
    }

    // --- Rendering ---

    /**
     * @brief Main drawing method - renders the chart.
     *
     * This method should be called from the UI thread within an
     * appropriate ImGui/ImPlot context.
     *
     * @param properties Optional rendering properties.
     */
    virtual void draw(const gr::property_map& properties = {}) = 0;

    // --- User interaction ---

    /**
     * @brief Handle context menu (right-click).
     *
     * Override to provide chart-specific context menu options.
     */
    virtual void onContextMenu() {}

    /**
     * @brief Handle axis popup (click on axis).
     *
     * @param axisIndex The axis that was clicked (0-2 for X, 3-5 for Y).
     */
    virtual void onAxisPopup(std::size_t axisIndex) { (void)axisIndex; }

protected:
    /**
     * @brief Called when a signal sink is added.
     *
     * Override to perform chart-specific setup when a sink is added.
     */
    virtual void onSignalSinkAdded(SignalSinkBase& sink) { sink.requestCapacity(this, defaultHistorySize()); }

    /**
     * @brief Called when a signal sink is removed.
     *
     * Override to perform chart-specific cleanup when a sink is removed.
     */
    virtual void onSignalSinkRemoved(SignalSinkBase& sink) { sink.releaseCapacity(this); }

    /**
     * @brief Get the default history size for this chart type.
     */
    [[nodiscard]] virtual std::size_t defaultHistorySize() const noexcept { return 2048; }

    std::vector<std::shared_ptr<SignalSinkBase>> _signalSinks;
};

/**
 * @brief Manager for chart instances with listener support.
 */
class ChartManager {
public:
    using Listener = std::function<void(Chart&, bool /*isAdded*/)>;

    static ChartManager& instance() {
        static ChartManager s_instance;
        return s_instance;
    }

    /**
     * @brief Register a chart.
     */
    void registerChart(std::shared_ptr<Chart> chart) {
        if (!chart) {
            return;
        }

        auto  id       = std::string(chart->uniqueId());
        auto& existing = _charts[id];
        if (existing) {
            return;
        }

        existing = std::move(chart);
        notifyListeners(*existing, true);
    }

    /**
     * @brief Unregister a chart.
     */
    void unregisterChart(std::string_view uniqueId) {
        auto it = _charts.find(std::string(uniqueId));
        if (it != _charts.end()) {
            notifyListeners(*it->second, false);
            _charts.erase(it);
        }
    }

    /**
     * @brief Find a chart by unique ID.
     */
    Chart* findChart(std::string_view uniqueId) const {
        auto it = _charts.find(std::string(uniqueId));
        return it != _charts.end() ? it->second.get() : nullptr;
    }

    /**
     * @brief Iterate over all charts.
     */
    template<typename Fn>
    void forEach(Fn&& fn) const {
        for (const auto& [_, chart] : _charts) {
            fn(*chart);
        }
    }

    /**
     * @brief Add a listener for chart registration events.
     */
    void addListener(void* owner, Listener listener) { _listeners[owner] = std::move(listener); }

    /**
     * @brief Remove a listener.
     */
    void removeListener(void* owner) { _listeners.erase(owner); }

    /**
     * @brief Get the number of registered charts.
     */
    [[nodiscard]] std::size_t chartCount() const noexcept { return _charts.size(); }

private:
    ChartManager() = default;
    ~ChartManager() { _charts.clear(); }

    ChartManager(const ChartManager&)            = delete;
    ChartManager& operator=(const ChartManager&) = delete;

    void notifyListeners(Chart& chart, bool isAdded) {
        for (const auto& [_, listener] : _listeners) {
            listener(chart, isAdded);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<Chart>> _charts;
    std::unordered_map<void*, Listener>                     _listeners;
};

/**
 * @brief Factory function type for creating chart instances.
 */
using ChartFactory = std::function<std::unique_ptr<Chart>(const ChartConfig&)>;

/**
 * @brief Registry for chart type factories.
 *
 * Allows registration of chart types by name, enabling dynamic
 * chart creation and chart type "transmutation".
 */
class ChartTypeRegistry {
public:
    static ChartTypeRegistry& instance() {
        static ChartTypeRegistry s_instance;
        return s_instance;
    }

    /**
     * @brief Register a chart type factory.
     */
    void registerType(std::string_view typeName, ChartFactory factory) { _factories[std::string(typeName)] = std::move(factory); }

    /**
     * @brief Create a chart of the specified type.
     */
    std::unique_ptr<Chart> create(std::string_view typeName, const ChartConfig& config) const {
        auto it = _factories.find(std::string(typeName));
        if (it != _factories.end()) {
            return it->second(config);
        }
        return nullptr;
    }

    /**
     * @brief Get all registered chart type names.
     */
    std::vector<std::string> registeredTypes() const {
        std::vector<std::string> types;
        types.reserve(_factories.size());
        for (const auto& [name, _] : _factories) {
            types.push_back(name);
        }
        return types;
    }

    /**
     * @brief Check if a chart type is registered.
     */
    bool hasType(std::string_view typeName) const { return _factories.contains(std::string(typeName)); }

private:
    ChartTypeRegistry() = default;

    std::unordered_map<std::string, ChartFactory> _factories;
};

/**
 * @brief Helper macro to register a chart type.
 */
#define REGISTER_CHART_TYPE(TypeName, ClassName)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    namespace {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                \
    [[maybe_unused]] static bool _registered_##ClassName = []() {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              \
        ::opendigitizer::ui::charts::ChartTypeRegistry::instance().registerType(TypeName, [](const ::opendigitizer::ui::charts::ChartConfig& config) { return std::make_unique<ClassName>(config); });                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
        return true;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
    }();                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       \
    }

} // namespace opendigitizer::ui::charts

#endif // OPENDIGITIZER_UI_CHARTS_CHART_HPP