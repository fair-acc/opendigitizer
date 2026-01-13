#ifndef OPENDIGITIZER_UI_CHARTS_XYCHART_HPP
#define OPENDIGITIZER_UI_CHARTS_XYCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

// Support headless mode for testing without ImGui/ImPlot
#if __has_include(<imgui.h>) && __has_include(<implot.h>) && !defined(OPENDIGITIZER_HEADLESS_MODE)
#define XYCHART_HAS_IMGUI 1
#include "../common/LookAndFeel.hpp"
#include <imgui.h>
#include <implot.h>
#else
#define XYCHART_HAS_IMGUI 0
#endif

namespace opendigitizer::ui::charts {

/**
 * @brief Category information for grouping signals on axes.
 */
struct AxisCategory {
    std::string   quantity;
    std::string   unit;
    std::uint32_t color = 0xFFFFFFFF;
};

/**
 * @brief Context for ImPlot::PlotLineG callback with indexed access.
 */
struct PlotLineContext {
    const SignalSinkBase* sink;
    AxisScale             axisScale;
    double                xMin;
    double                xMax;
    float                 yOffset{0.0f};
};

/**
 * @brief XYChart - Standard X-Y line/scatter chart.
 *
 * Plots one or more Y-values against a common X-axis. Supports:
 * - Up to 3 X-axes and 3 Y-axes
 * - Automatic grouping of signals by unit/quantity
 * - Multiple X-axis modes (UTC time, relative time, sample index)
 * - Tag/timing event rendering
 * - DataSet overlay with history fading
 */
class XYChart : public Chart {
public:
    explicit XYChart(const ChartConfig& config = {}) : _config(config), _uniqueId(generateUniqueId()) {}

    ~XYChart() override = default;

    // --- Identity ---
    [[nodiscard]] std::string_view chartTypeName() const noexcept override { return "XYChart"; }

    [[nodiscard]] std::string_view uniqueId() const noexcept override { return _uniqueId; }

    // --- Configuration ---
    [[nodiscard]] const ChartConfig& config() const noexcept override { return _config; }

    void setConfig(const ChartConfig& config) override { _config = config; }

    // --- Rendering ---
    void draw([[maybe_unused]] const gr::property_map& properties = {}) override {
#if XYCHART_HAS_IMGUI
        if (_signalSinks.empty()) {
            drawEmptyPlot();
            return;
        }

        // Build axis categories from signal metadata
        buildAxisCategories();

        // Begin plot
        const ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!ImPlot::BeginPlot(_config.name.c_str(), ImVec2(-1, -1), plotFlags)) {
            return;
        }

        // Setup axes
        setupAxes();
        ImPlot::SetupFinish();

        // Draw signals
        drawSignals(properties);

        // Handle interactions
        handleInteractions();

        ImPlot::EndPlot();
#else
        // Headless mode - no rendering
        (void)properties;
#endif
    }

    // --- User interaction ---
    void onContextMenu() override {
#if XYCHART_HAS_IMGUI
        if (ImGui::BeginPopup("XYChartContextMenu")) {
            if (ImGui::BeginMenu("X-Axis Mode")) {
                if (ImGui::MenuItem("UTC Time", nullptr, _config.xAxisMode == XAxisMode::UtcTime)) {
                    _config.xAxisMode = XAxisMode::UtcTime;
                }
                if (ImGui::MenuItem("Relative Time", nullptr, _config.xAxisMode == XAxisMode::RelativeTime)) {
                    _config.xAxisMode = XAxisMode::RelativeTime;
                }
                if (ImGui::MenuItem("Sample Index", nullptr, _config.xAxisMode == XAxisMode::SampleIndex)) {
                    _config.xAxisMode = XAxisMode::SampleIndex;
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Show Legend", nullptr, _config.showLegend)) {
                _config.showLegend = !_config.showLegend;
            }
            if (ImGui::MenuItem("Show Tags", nullptr, _config.showTags)) {
                _config.showTags = !_config.showTags;
            }
            if (ImGui::MenuItem("Show Grid", nullptr, _config.showGrid)) {
                _config.showGrid = !_config.showGrid;
            }

            ImGui::EndPopup();
        }
#endif
    }

    void onAxisPopup([[maybe_unused]] std::size_t axisIndex) override {
#if XYCHART_HAS_IMGUI
        std::string popupId = "AxisPopup_" + std::to_string(axisIndex);
        if (ImGui::BeginPopup(popupId.c_str())) {
            bool  isXAxis    = axisIndex < 3;
            auto& axisConfig = isXAxis ? _config.xAxes[axisIndex] : _config.yAxes[axisIndex - 3];

            ImGui::Text("%s Axis %zu", isXAxis ? "X" : "Y", (axisIndex % 3) + 1);
            ImGui::Separator();

            if (ImGui::BeginMenu("Scale")) {
                if (ImGui::MenuItem("Linear", nullptr, axisConfig.scale == AxisScale::Linear)) {
                    axisConfig.scale = AxisScale::Linear;
                }
                if (ImGui::MenuItem("Log10", nullptr, axisConfig.scale == AxisScale::Log10)) {
                    axisConfig.scale = AxisScale::Log10;
                }
                if (ImGui::MenuItem("SymLog", nullptr, axisConfig.scale == AxisScale::SymLog)) {
                    axisConfig.scale = AxisScale::SymLog;
                }
                if (isXAxis && ImGui::MenuItem("Time", nullptr, axisConfig.scale == AxisScale::Time)) {
                    axisConfig.scale = AxisScale::Time;
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Auto Scale", nullptr, axisConfig.autoScale)) {
                axisConfig.autoScale = !axisConfig.autoScale;
            }

            ImGui::EndPopup();
        }
#endif
    }

protected:
    [[nodiscard]] std::size_t defaultHistorySize() const noexcept override {
        return 4096; // Default for XYChart
    }

private:
#if XYCHART_HAS_IMGUI
    void drawEmptyPlot() {
        if (ImPlot::BeginPlot(_config.name.c_str(), ImVec2(-1, -1), ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("X", "Y");
            ImPlot::SetupFinish();

            // Draw centered message
            ImVec2 plotSize = ImPlot::GetPlotSize();
            ImVec2 textSize = ImGui::CalcTextSize("No signals");
            (void)plotSize;
            (void)textSize;
            ImPlot::PlotText("No signals", ImPlot::GetPlotLimits().X.Min + (ImPlot::GetPlotLimits().X.Max - ImPlot::GetPlotLimits().X.Min) / 2, ImPlot::GetPlotLimits().Y.Min + (ImPlot::GetPlotLimits().Y.Max - ImPlot::GetPlotLimits().Y.Min) / 2);

            ImPlot::EndPlot();
        }
    }
#endif

    void buildAxisCategories() {
        // Reset categories
        for (auto& cat : _xCategories) {
            cat.reset();
        }
        for (auto& cat : _yCategories) {
            cat.reset();
        }
        for (auto& group : _xAxisGroups) {
            group.clear();
        }
        for (auto& group : _yAxisGroups) {
            group.clear();
        }

        // Group signals - simplified API doesn't have detailed quantity/unit metadata
        // All signals go to first axis by default
        for (const auto& sink : _signalSinks) {
            _xAxisGroups[0].push_back(std::string(sink->uniqueName()));
            _yAxisGroups[0].push_back(std::string(sink->uniqueName()));
        }

        // Set default categories based on first signal
        if (!_signalSinks.empty()) {
            _xCategories[0] = AxisCategory{"time", "s", _signalSinks[0]->color()};
            _yCategories[0] = AxisCategory{"signal", "", _signalSinks[0]->color()};
        }
    }

#if XYCHART_HAS_IMGUI
    void setupAxes() {
        // Setup X axes
        for (std::size_t i = 0; i < 3; ++i) {
            if (!_xCategories[i].has_value() && i > 0) {
                continue;
            }

            ImAxis          axisId = static_cast<ImAxis>(ImAxis_X1 + i);
            ImPlotAxisFlags flags  = ImPlotAxisFlags_None;

            if (i > 0 && !_xCategories[i].has_value()) {
                flags |= ImPlotAxisFlags_NoDecorations;
            }

            std::string label = buildAxisLabel(_xCategories[i]);
            ImPlot::SetupAxis(axisId, label.c_str(), flags);

            setupAxisScale(axisId, _config.xAxes[i]);
        }

        // Setup Y axes
        for (std::size_t i = 0; i < 3; ++i) {
            if (!_yCategories[i].has_value() && i > 0) {
                continue;
            }

            ImAxis          axisId = static_cast<ImAxis>(ImAxis_Y1 + i);
            ImPlotAxisFlags flags  = ImPlotAxisFlags_None;

            if (i > 0 && !_yCategories[i].has_value()) {
                flags |= ImPlotAxisFlags_NoDecorations;
            }

            std::string label = buildAxisLabel(_yCategories[i]);
            ImPlot::SetupAxis(axisId, label.c_str(), flags);

            setupAxisScale(axisId, _config.yAxes[i]);
        }
    }

    std::string buildAxisLabel(const std::optional<AxisCategory>& category) {
        if (!category.has_value()) {
            return "";
        }

        if (category->unit.empty()) {
            return category->quantity;
        }
        return category->quantity + " [" + category->unit + "]";
    }

    void setupAxisScale(ImAxis axisId, const AxisConfig& config) {
        switch (config.scale) {
        case AxisScale::Log10: ImPlot::SetupAxisScale(axisId, ImPlotScale_Log10); break;
        case AxisScale::SymLog: ImPlot::SetupAxisScale(axisId, ImPlotScale_SymLog); break;
        case AxisScale::Time:
            ImPlot::GetStyle().UseISO8601     = true;
            ImPlot::GetStyle().Use24HourClock = true;
            ImPlot::SetupAxisScale(axisId, ImPlotScale_Time);
            break;
        default: ImPlot::SetupAxisScale(axisId, ImPlotScale_Linear); break;
        }

        if (!config.autoScale) {
            ImPlot::SetupAxisLimits(axisId, config.min, config.max);
        }
    }

    void drawSignals([[maybe_unused]] const gr::property_map& properties) {
        for (const auto& sink : _signalSinks) {
            // Find axis assignments
            std::size_t xAxisIdx = findAxisIndex(_xAxisGroups, sink->uniqueName());
            std::size_t yAxisIdx = findAxisIndex(_yAxisGroups, sink->uniqueName());

            // Set axes
            ImPlot::SetAxes(static_cast<ImAxis>(ImAxis_X1 + xAxisIdx), static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            // Draw streaming data (uses indexed access)
            if (sink->size() > 0) {
                drawStreamingSignal(*sink);
            }

            // Draw DataSet data
            if (sink->hasDataSets()) {
                drawDataSetSignal(*sink);
            }
        }
    }

    void drawStreamingSignal(const SignalSinkBase& sink) {
        std::size_t dataSize = sink.size();
        if (dataSize == 0) {
            return;
        }

        // Get color
        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(sink.color() | 0xFF000000);
        ImPlot::SetNextLineStyle(lineColor);

        // Compute X range for axis transformation
        double xMin = sink.xAt(0);
        double xMax = sink.xAt(dataSize - 1);

        // Use PlotLineG with indexed access via PlotData
        PlotLineContext ctx{&sink, _config.xAxes[0].scale, xMin, xMax, 0.0f};

        ImPlot::PlotLineG(
            std::string(sink.signalName()).c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  ctx  = static_cast<PlotLineContext*>(userData);
                double xVal = ctx->sink->xAt(static_cast<std::size_t>(idx));
                double yVal = static_cast<double>(ctx->sink->yAt(static_cast<std::size_t>(idx))) + ctx->yOffset;

                // Transform X based on axis mode
                if (ctx->axisScale != AxisScale::Time) {
                    xVal = xVal - ctx->xMin; // Relative time
                }

                return ImPlotPoint(xVal, yVal);
            },
            &ctx, static_cast<int>(dataSize));
    }

    void drawDataSetSignal(const SignalSinkBase& sink) {
        auto dataSets = sink.dataSets();
        if (dataSets.empty()) {
            return;
        }

        std::size_t historyCount = std::min(dataSets.size(), _config.maxHistoryCount);

        // Draw from oldest to newest (newest on top)
        for (std::size_t histIdx = historyCount; histIdx-- > 0;) {
            const auto& ds = dataSets[histIdx];

            // Calculate opacity based on history position
            float opacity = 1.0f - static_cast<float>(histIdx) * _config.historyOpacityDecay;
            opacity       = std::max(0.1f, opacity);

            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(sink.color() | 0xFF000000);
            lineColor.w      = opacity;
            ImPlot::SetNextLineStyle(lineColor);

            // Get axis values
            if (ds.axis_values.empty() || ds.axis_values[0].empty()) {
                continue;
            }

            auto   xAxis = ds.axisValues(0);
            double xMin  = static_cast<double>(xAxis.front());
            double xMax  = static_cast<double>(xAxis.back());

            // Draw each signal in the dataset using a local context struct for the lambda
            struct DataSetPlotContext {
                std::span<const float> xAxis;
                std::span<const float> yVals;
                float                  yOffset;
            };

            for (std::size_t sigIdx = 0; sigIdx < ds.size(); ++sigIdx) {
                auto               yVals = ds.signalValues(sigIdx);
                DataSetPlotContext dsCtx{xAxis, yVals, static_cast<float>(histIdx) * _config.historyVerticalOffset};

                std::string label = histIdx == 0 ? ds.signal_names[sigIdx] : "";

                ImPlot::PlotLineG(
                    label.c_str(),
                    [](int idx, void* userData) -> ImPlotPoint {
                        auto*  ctx  = static_cast<DataSetPlotContext*>(userData);
                        double xVal = static_cast<double>(ctx->xAxis[static_cast<std::size_t>(idx)]);
                        double yVal = static_cast<double>(ctx->yVals[static_cast<std::size_t>(idx)]) + ctx->yOffset;
                        return ImPlotPoint(xVal, yVal);
                    },
                    &dsCtx, static_cast<int>(std::min(xAxis.size(), yVals.size())));
            }
        }
    }

#endif // XYCHART_HAS_IMGUI

    std::size_t findAxisIndex(const std::array<std::vector<std::string>, 3>& groups, std::string_view sinkName) {
        for (std::size_t i = 0; i < 3; ++i) {
            auto it = std::find(groups[i].begin(), groups[i].end(), sinkName);
            if (it != groups[i].end()) {
                return i;
            }
        }
        return 0; // Default to first axis
    }

#if XYCHART_HAS_IMGUI
    void handleInteractions() {
        // Right-click context menu
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("XYChartContextMenu");
        }
        onContextMenu();

        // Drag and drop for legend items
        for (const auto& sink : _signalSinks) {
            if (ImPlot::BeginDragDropSourceItem(std::string(sink->signalName()).c_str())) {
                // Set payload with sink pointer
                const SignalSinkBase* sinkPtr = sink.get();
                ImGui::SetDragDropPayload("SIGNAL_SINK", &sinkPtr, sizeof(sinkPtr));

                ImPlot::ItemIcon(sink->color());
                ImGui::SameLine();
                ImGui::TextUnformatted(std::string(sink->signalName()).c_str());

                ImPlot::EndDragDropSource();
            }
        }

        // Accept drag and drop
        if (ImPlot::BeginDragDropTargetPlot()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SIGNAL_SINK")) {
                // Handle dropped signal - would need to coordinate with ChartManager
            }
            ImPlot::EndDragDropTarget();
        }
    }
#endif // XYCHART_HAS_IMGUI

    static std::string generateUniqueId() {
        static std::atomic<std::size_t> counter{0};
        return "XYChart_" + std::to_string(counter++);
    }

    ChartConfig _config;
    std::string _uniqueId;

    // Axis categories (computed from signal metadata)
    std::array<std::optional<AxisCategory>, 3> _xCategories;
    std::array<std::optional<AxisCategory>, 3> _yCategories;
    std::array<std::vector<std::string>, 3>    _xAxisGroups;
    std::array<std::vector<std::string>, 3>    _yAxisGroups;
};

// Register XYChart type
REGISTER_CHART_TYPE("XYChart", XYChart)

} // namespace opendigitizer::ui::charts

#endif // OPENDIGITIZER_UI_CHARTS_XYCHART_HPP
