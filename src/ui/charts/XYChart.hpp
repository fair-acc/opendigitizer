#ifndef OPENDIGITIZER_CHARTS_XYCHART_HPP
#define OPENDIGITIZER_CHARTS_XYCHART_HPP

#include "ChartInterface.hpp"
#include "ChartUtils.hpp"
#include "DataSinks.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

/**
 * @brief Context for ImPlot::PlotLineG callback with indexed access.
 */
struct PlotLineContext {
    const SignalSink* sink;
    AxisScale         axisScale;
    double            xMin;
    double            xMax;
    float             yOffset{0.0f};
};

/**
 * @brief Context for DataSet plotting with direct DataSet access.
 */
struct DataSetPlotContext {
    const gr::DataSet<float>* dataSet;
    std::size_t               signalIndex{0}; // Which signal within the DataSet to plot
};

/**
 * @brief XYChart - Standard X-Y line/scatter chart as a GR4 block.
 *
 * Plots one or more Y-values against a common X-axis. Supports:
 * - Up to 3 X-axes and 3 Y-axes
 * - Automatic grouping of signals by unit/quantity
 * - Multiple X-axis modes (UTC time, relative time, sample index)
 * - Tag/timing event rendering
 * - DataSet overlay with history fading
 */
struct XYChart : gr::Block<XYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>, ChartInterface {
    // Annotated type alias for cleaner declarations
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // --- Reflectable settings (GR_MAKE_REFLECTABLE) ---
    A<std::string, "chart name", gr::Visible> chart_name;
    A<std::string, "chart title">             chart_title;
    A<int, "X-axis mode", gr::Visible>        x_axis_mode             = static_cast<int>(XAxisMode::RelativeTime);
    A<bool, "show legend", gr::Visible>       show_legend             = true;
    A<bool, "show tags", gr::Visible>         show_tags               = true;
    A<bool, "show grid", gr::Visible>         show_grid               = true;
    A<bool, "anti-aliasing">                  anti_aliasing           = true;
    A<gr::Size_t, "max history count">        max_history_count       = 3U;
    A<float, "history opacity decay">         history_opacity_decay   = 0.3f;
    A<float, "history vertical offset">       history_vertical_offset = 0.0f;

    GR_MAKE_REFLECTABLE(XYChart, chart_name, chart_title, x_axis_mode, show_legend, show_tags, show_grid, anti_aliasing, max_history_count, history_opacity_decay, history_vertical_offset);

    // Constructor (required for gr::Block)
    explicit XYChart(gr::property_map initParameters = {}) : gr::Block<XYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>(std::move(initParameters)) {}

    // --- Non-reflectable runtime state ---
    std::vector<std::shared_ptr<SignalSink>> _signalSinks;
    std::vector<DashboardAxisConfig>         _dashboardAxisConfig;

    // Axis category storage (up to 3 per direction)
    std::array<std::optional<AxisCategory>, 3> _xCategories{};
    std::array<std::optional<AxisCategory>, 3> _yCategories{};
    std::array<std::vector<std::string>, 3>    _xAxisGroups{};
    std::array<std::vector<std::string>, 3>    _yAxisGroups{};
    mutable std::string                        _unitStringStorage;

    // Unique ID for this chart instance
    std::string _uniqueId = generateUniqueId();

    // Drag & drop callbacks
    DropCallback       _dropCallback;
    std::string        _dropPayloadType;
    DragSourceCallback _dragSourceCallback;

    // --- Signal sink management ---

    void addSignalSink(std::shared_ptr<SignalSink> sink) override {
        if (!sink) {
            return;
        }
        auto it = std::find(_signalSinks.begin(), _signalSinks.end(), sink);
        if (it == _signalSinks.end()) {
            sink->requestCapacity(this, defaultHistorySize());
            _signalSinks.push_back(std::move(sink));
        }
    }

    void removeSignalSink(std::string_view uniqueName) override {
        auto it = std::find_if(_signalSinks.begin(), _signalSinks.end(), [uniqueName](const auto& s) { return s->uniqueName() == uniqueName; });
        if (it != _signalSinks.end()) {
            (*it)->releaseCapacity(this);
            _signalSinks.erase(it);
        }
    }

    void clearSignalSinks() override {
        for (auto& sink : _signalSinks) {
            sink->releaseCapacity(this);
        }
        _signalSinks.clear();
    }

    [[nodiscard]] const std::vector<std::shared_ptr<SignalSink>>& signalSinks() const noexcept override { return _signalSinks; }
    [[nodiscard]] std::size_t                                     signalSinkCount() const noexcept override { return _signalSinks.size(); }

    void setDashboardAxisConfig(std::vector<DashboardAxisConfig> configs) override { _dashboardAxisConfig = std::move(configs); }
    void clearDashboardAxisConfig() override { _dashboardAxisConfig.clear(); }

    // --- Drag & Drop ---
    void setDropCallback(DropCallback callback, const char* payloadType) override {
        _dropCallback    = std::move(callback);
        _dropPayloadType = payloadType ? payloadType : "";
    }
    void clearDropCallback() override {
        _dropCallback = nullptr;
        _dropPayloadType.clear();
    }
    void setDragSourceCallback(DragSourceCallback callback) override { _dragSourceCallback = std::move(callback); }
    void clearDragSourceCallback() override { _dragSourceCallback = nullptr; }

    // --- Public axis management (for DashboardPage integration) ---

    /**
     * @brief Build axis categories from signal sinks.
     * Call this before setupAllAxes() when using external axis setup.
     */
    void buildAxisCategories() override { buildAxisCategoriesWithFallback(); }

    /**
     * @brief Setup all ImPlot axes.
     * Call this after BeginPlot() and before SetupFinish().
     */
    void setupAllAxes() override { setupAxes(); }

    // --- Drawable interface ---

    gr::work::Status draw(const gr::property_map& /*config*/ = {}) {
        if (_signalSinks.empty()) {
            drawEmptyPlot();
            return gr::work::Status::OK;
        }

        buildAxisCategoriesWithFallback();

        if (!ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), ImPlotFlags_NoTitle)) {
            return gr::work::Status::OK;
        }

        setupAxes();
        ImPlot::SetupFinish();
        drawSignals();
        handleInteractions();
        ImPlot::EndPlot();

        return gr::work::Status::OK;
    }

    // --- ChartInterface rendering ---

    /**
     * @brief Draw chart content within an already-opened ImPlot context.
     * Caller must have called BeginPlot() and SetupFinish() before this.
     */
    void drawContent() override {
        drawSignals();
        handleInteractions();
    }

    // --- Identity ---
    [[nodiscard]] std::string_view chartTypeName() const noexcept override { return "XYChart"; }
    [[nodiscard]] std::string_view uniqueId() const noexcept override { return _uniqueId; }

private:
    static constexpr std::size_t defaultHistorySize() noexcept { return 4096; }

    static std::string generateUniqueId() {
        static std::atomic<std::size_t> counter{0};
        return "XYChart_" + std::to_string(counter++);
    }

    [[nodiscard]] const DashboardAxisConfig* getDashboardAxisConfig(bool isX, std::size_t index) const {
        using AxisKind            = DashboardAxisConfig::AxisKind;
        const AxisKind targetKind = isX ? AxisKind::X : AxisKind::Y;

        std::size_t count = 0;
        for (const auto& cfg : _dashboardAxisConfig) {
            if (cfg.axis == targetKind) {
                if (count == index) {
                    return &cfg;
                }
                ++count;
            }
        }
        return nullptr;
    }

    void drawEmptyPlot() {
        if (ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("X", "Y");
            ImPlot::SetupFinish();
            auto limits = ImPlot::GetPlotLimits();
            ImPlot::PlotText("No signals", (limits.X.Min + limits.X.Max) / 2, (limits.Y.Min + limits.Y.Max) / 2);
            ImPlot::EndPlot();
        }
    }

    void buildAxisCategoriesWithFallback() {
        axis::buildAxisCategories(_signalSinks, _xCategories, _yCategories, _xAxisGroups, _yAxisGroups);

        // Set default categories if none were created (fallback for XYChart)
        if (!_signalSinks.empty() && !_xCategories[0].has_value()) {
            _xCategories[0] = AxisCategory{"time", "s", _signalSinks[0]->color()};
            for (const auto& sink : _signalSinks) {
                _xAxisGroups[0].push_back(std::string(sink->uniqueName()));
            }
        }
        if (!_signalSinks.empty() && !_yCategories[0].has_value()) {
            _yCategories[0] = AxisCategory{"signal", "", _signalSinks[0]->color()};
            for (const auto& sink : _signalSinks) {
                _yAxisGroups[0].push_back(std::string(sink->uniqueName()));
            }
        }
    }

    void setupAxes() {
        constexpr float   xAxisWidth = 100.f;
        constexpr float   yAxisWidth = 100.f;
        const std::size_t nAxesX     = axis::activeAxisCount(_xCategories);
        const std::size_t nAxesY     = axis::activeAxisCount(_yCategories);

        for (std::size_t i = 0; i < _xCategories.size(); ++i) {
            const auto* dashCfg = getDashboardAxisConfig(true, i);

            const double      minLimit = dashCfg ? static_cast<double>(dashCfg->min) : std::numeric_limits<double>::quiet_NaN();
            const double      maxLimit = dashCfg ? static_cast<double>(dashCfg->max) : std::numeric_limits<double>::quiet_NaN();
            const LabelFormat format   = dashCfg ? dashCfg->format : LabelFormat::Auto;
            const float       width    = dashCfg && std::isfinite(dashCfg->width) ? dashCfg->width : xAxisWidth;
            const AxisScale   scale    = dashCfg ? dashCfg->scale : AxisScale::Linear;

            // Update category scale to match dashboard config (used by drawStreamingSignal)
            if (_xCategories[i].has_value()) {
                _xCategories[i]->scale = scale;
            }

            axis::setupAxis(ImAxis_X1 + static_cast<int>(i), _xCategories[i], format, width, minLimit, maxLimit, nAxesX, scale, _unitStringStorage);
        }

        for (std::size_t i = 0; i < _yCategories.size(); ++i) {
            const auto* dashCfg = getDashboardAxisConfig(false, i);

            const double      minLimit = dashCfg ? static_cast<double>(dashCfg->min) : std::numeric_limits<double>::quiet_NaN();
            const double      maxLimit = dashCfg ? static_cast<double>(dashCfg->max) : std::numeric_limits<double>::quiet_NaN();
            const LabelFormat format   = dashCfg ? dashCfg->format : LabelFormat::Auto;
            const float       width    = dashCfg && std::isfinite(dashCfg->width) ? dashCfg->width : yAxisWidth;
            const AxisScale   scale    = dashCfg ? dashCfg->scale : AxisScale::Linear;

            // Update category scale to match dashboard config
            if (_yCategories[i].has_value()) {
                _yCategories[i]->scale = scale;
            }

            axis::setupAxis(ImAxis_Y1 + static_cast<int>(i), _yCategories[i], format, width, minLimit, maxLimit, nAxesY, scale, _unitStringStorage);
        }
    }

    void drawSignals() {
        bool tagsDrawnForFirstSink = false;

        for (std::size_t i = 0; i < _signalSinks.size(); ++i) {
            const auto& sink = _signalSinks[i];

            // Skip drawing if signal is hidden (data consumption is handled elsewhere)
            if (!sink->drawEnabled()) {
                continue;
            }

            std::string_view sinkUniqueName = sink->uniqueName();
            std::size_t      xAxisIdx       = axis::findAxisForSink(sinkUniqueName, true, _xAxisGroups, _yAxisGroups);
            std::size_t      yAxisIdx       = axis::findAxisForSink(sinkUniqueName, false, _xAxisGroups, _yAxisGroups);

            ImPlot::SetAxes(static_cast<ImAxis>(ImAxis_X1 + xAxisIdx), static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            // Acquire lock for thread-safe data access during rendering
            auto dataLock = sink->acquireDataLock();

            if (sink->size() > 0) {
                ImVec4 baseColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sink->color()));

                if (sink->hasDataSets()) {
                    drawDataSetSignal(*sink);

                    // Draw timing events for DataSets (only for first sink to avoid clutter)
                    if (show_tags.value && !tagsDrawnForFirstSink) {
                        auto allDataSets = sink->dataSets();
                        if (!allDataSets.empty()) {
                            tags::drawDataSetTimingEvents(allDataSets[0], _xCategories[xAxisIdx].has_value() ? _xCategories[xAxisIdx]->scale : AxisScale::Linear, baseColor);
                        }
                        tagsDrawnForFirstSink = true;
                    }
                } else {
                    drawStreamingSignal(*sink);

                    // Draw streaming tags (only for first sink to avoid clutter)
                    if (show_tags.value && !tagsDrawnForFirstSink) {
                        double    xMin       = sink->xAt(0);
                        double    xMax       = sink->xAt(sink->size() - 1);
                        AxisScale xAxisScale = _xCategories[xAxisIdx].has_value() ? _xCategories[xAxisIdx]->scale : AxisScale::Linear;
                        tags::drawStreamingTags(*sink, xAxisScale, xMin, xMax, baseColor);
                        tagsDrawnForFirstSink = true;
                    }
                }
            }
        }
    }

    void drawStreamingSignal(const SignalSink& sink) {
        // Note: Caller must hold data lock via sink.acquireDataLock()
        std::size_t dataCount = sink.size();
        if (dataCount == 0) {
            return;
        }

        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sink.color()));
        ImPlot::SetNextLineStyle(lineColor);

        double xMin = sink.xAt(0);
        double xMax = sink.xAt(dataCount - 1);

        AxisScale       xAxisScale = _xCategories[0].has_value() ? _xCategories[0]->scale : AxisScale::Linear;
        PlotLineContext ctx{&sink, xAxisScale, xMin, xMax, 0.0f};

        ImPlot::PlotLineG(
            std::string(sink.signalName()).c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  context = static_cast<PlotLineContext*>(userData);
                double xVal    = context->sink->xAt(static_cast<std::size_t>(idx));
                double yVal    = static_cast<double>(context->sink->yAt(static_cast<std::size_t>(idx))) + static_cast<double>(context->yOffset);

                // Transform x values based on axis scale
                switch (context->axisScale) {
                case AxisScale::Time:
                    // Time scale: use absolute timestamps
                    break;
                case AxisScale::LinearReverse:
                    // LinearReverse: x relative to xMax (results in 0 to negative range)
                    xVal = xVal - context->xMax;
                    break;
                default:
                    // Linear, Log10, SymLog: x relative to xMin (results in 0 to positive range)
                    xVal = xVal - context->xMin;
                    break;
                }

                return ImPlotPoint(xVal, yVal);
            },
            &ctx, static_cast<int>(dataCount));
    }

    void drawDataSetSignal(const SignalSink& sink) {
        // DataSet signals: x values are absolute (e.g., frequency), no transformation needed
        // Supports history rendering with fading opacity for older DataSets
        auto allDataSets = sink.dataSets();
        if (allDataSets.empty()) {
            return;
        }

        ImVec4      baseColor   = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sink.color()));
        std::size_t historySize = std::min(allDataSets.size(), static_cast<std::size_t>(max_history_count.value));
        std::string baseName    = std::string(sink.signalName());

        // Draw from oldest to newest (so newest renders on top)
        // DataSets are ordered newest-first in the span, so we iterate in reverse
        for (std::size_t histIdx = historySize; histIdx > 0; --histIdx) {
            std::size_t        dsIdx = histIdx - 1;
            const auto&        ds    = allDataSets[dsIdx];
            DataSetPlotContext ctx{&ds, 0};

            // Calculate opacity: newest (dsIdx=0) = 1.0, oldest = 0.2
            float opacity = 1.0f - (static_cast<float>(dsIdx) / static_cast<float>(historySize)) * 0.8f;

            ImVec4 lineColor = baseColor;
            lineColor.w      = opacity;
            ImPlot::SetNextLineStyle(lineColor);

            // Only show label for newest DataSet to avoid legend clutter
            std::string label = (dsIdx == 0) ? baseName : ("##" + baseName + "_hist_" + std::to_string(dsIdx));

            // Get data count from DataSet's axis_values
            if (ds.axis_values.empty() || ds.axis_values[0].empty()) {
                continue;
            }
            int dataCount = static_cast<int>(ds.axis_values[0].size());

            ImPlot::PlotLineG(
                label.c_str(),
                [](int idx, void* userData) -> ImPlotPoint {
                    auto*       context = static_cast<DataSetPlotContext*>(userData);
                    const auto& dataSet = *context->dataSet;
                    double      xVal    = static_cast<double>(dataSet.axis_values[0][static_cast<std::size_t>(idx)]);
                    double      yVal    = 0.0;
                    auto        sigVals = dataSet.signalValues(context->signalIndex);
                    if (static_cast<std::size_t>(idx) < sigVals.size()) {
                        yVal = static_cast<double>(sigVals[static_cast<std::size_t>(idx)]);
                    }
                    return ImPlotPoint(xVal, yVal);
                },
                &ctx, dataCount);
        }
    }

    void handleInteractions() {
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("XYChartContextMenu");
        }

        if (ImGui::BeginPopup("XYChartContextMenu")) {
            if (ImGui::BeginMenu("X-Axis Mode")) {
                if (ImGui::MenuItem("UTC Time", nullptr, x_axis_mode == static_cast<int>(XAxisMode::UtcTime))) {
                    x_axis_mode = static_cast<int>(XAxisMode::UtcTime);
                }
                if (ImGui::MenuItem("Relative Time", nullptr, x_axis_mode == static_cast<int>(XAxisMode::RelativeTime))) {
                    x_axis_mode = static_cast<int>(XAxisMode::RelativeTime);
                }
                if (ImGui::MenuItem("Sample Index", nullptr, x_axis_mode == static_cast<int>(XAxisMode::SampleIndex))) {
                    x_axis_mode = static_cast<int>(XAxisMode::SampleIndex);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Show Legend", nullptr, show_legend.value)) {
                show_legend = !show_legend.value;
            }
            if (ImGui::MenuItem("Show Tags", nullptr, show_tags.value)) {
                show_tags = !show_tags.value;
            }
            if (ImGui::MenuItem("Show Grid", nullptr, show_grid.value)) {
                show_grid = !show_grid.value;
            }

            ImGui::EndPopup();
        }

        // Drag and drop handling - set up drag sources for legend items
        for (const auto& sink : _signalSinks) {
            std::string signalNameStr(sink->signalName());
            if (ImPlot::BeginDragDropSourceItem(signalNameStr.c_str())) {
                if (_dragSourceCallback) {
                    // Use external callback to set up payload (DashboardPage integration)
                    _dragSourceCallback(sink->uniqueName(), _uniqueId);
                } else {
                    // Internal drag source for standalone chart use
                    std::string uniqueNameStr(sink->uniqueName());
                    ImGui::SetDragDropPayload("SIGNAL_SINK", uniqueNameStr.data(), uniqueNameStr.size() + 1);
                }
                ImPlot::ItemIcon(sink->color());
                ImGui::SameLine();
                ImGui::TextUnformatted(signalNameStr.c_str());
                ImPlot::EndDragDropSource();
            }
        }

        // Drop target handling
        if (ImPlot::BeginDragDropTargetPlot()) {
            // External callback takes priority (DashboardPage integration)
            if (_dropCallback && !_dropPayloadType.empty()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(_dropPayloadType.c_str())) {
                    _dropCallback(payload->Data, static_cast<std::size_t>(payload->DataSize), _dropPayloadType.c_str());
                }
            }
            ImPlot::EndDragDropTarget();
        }
    }
};

} // namespace opendigitizer::charts

// Register XYChart with the GR4 block registry
GR_REGISTER_BLOCK("opendigitizer::charts::XYChart", opendigitizer::charts::XYChart)

#endif // OPENDIGITIZER_CHARTS_XYCHART_HPP
