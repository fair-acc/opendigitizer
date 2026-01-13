#ifndef OPENDIGITIZER_CHARTS_YYCHART_HPP
#define OPENDIGITIZER_CHARTS_YYCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

/**
 * @brief YYChart - Correlation plot (Y1 vs Y2) as a GR4 block.
 *
 * Plots Y-values from one signal against Y-values from another signal.
 * Use cases: Lissajous figures, phase plots, I-Q diagrams.
 *
 * Signal interpretation:
 *   - 1 signal: Falls back to XY mode (time vs Y)
 *   - 2 signals: Y1 vs Y2 correlation
 *   - 3+ signals: First signal is X axis, remaining signals share Y axis
 */
struct YYChart : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>, Chart {
    // Annotated type alias for cleaner declarations
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // --- Reflectable settings (GR_MAKE_REFLECTABLE) ---
    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks    = {};
    A<bool, "show legend", gr::Visible>                    show_legend   = false;
    A<bool, "show grid", gr::Visible>                      show_grid     = true;
    A<bool, "anti-aliasing">                               anti_aliasing = true;
    A<int, "X-axis scale">                                 x_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<int, "Y-axis scale">                                 y_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<bool, "X auto-scale">                                x_auto_scale  = true;
    A<bool, "Y auto-scale">                                y_auto_scale  = true;
    A<float, "X-axis min">                                 x_min         = std::numeric_limits<float>::lowest();
    A<float, "X-axis max">                                 x_max         = std::numeric_limits<float>::max();
    A<float, "Y-axis min">                                 y_min         = std::numeric_limits<float>::lowest();
    A<float, "Y-axis max">                                 y_max         = std::numeric_limits<float>::max();

    GR_MAKE_REFLECTABLE(YYChart, chart_name, chart_title, data_sinks, show_legend, show_grid, anti_aliasing, x_axis_scale, y_axis_scale, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    // Constructor (required for gr::Block)
    explicit YYChart(gr::property_map initParameters = {}) : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>(std::move(initParameters)) {}

    // --- Settings change handler ---

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (newSettings.contains("data_sinks")) {
            syncSinksFromNames(data_sinks.value);
            // Request capacity for each sink
            for (auto& sink : _signalSinks) {
                if (sink) {
                    sink->requestCapacity(std::string(uniqueId()), defaultHistorySize());
                }
            }
        }
    }

    // --- Signal sink management ---

    void addSignalSink(std::shared_ptr<SignalSink> sink) {
        if (!sink) {
            return;
        }
        auto it = std::find(_signalSinks.begin(), _signalSinks.end(), sink);
        if (it == _signalSinks.end()) {
            sink->requestCapacity(std::string(uniqueId()), defaultHistorySize());
            _signalSinks.push_back(std::move(sink));
        }
    }

    void removeSignalSink(std::string_view sinkName) {
        // Capacity request will auto-expire when not refreshed
        Chart::removeSignalSink(sinkName);
    }

    // --- Public axis management (for DashboardPage integration) ---

    /**
     * @brief Build axis categories (no-op - YYChart handles axes internally).
     */
    void buildAxisCategories() { /* YYChart manages its own axes based on signal roles */ }

    /**
     * @brief Setup all ImPlot axes (no-op - YYChart handles axes in draw()).
     */
    void setupAllAxes() { /* Handled internally in draw methods */ }

    // --- Drawable interface ---

    gr::work::Status draw(const gr::property_map& config = {}) {
        // UI blocks aren't scheduled, so settingsChanged() won't be called after setSinkNames().
        // Sync sinks from data_sinks.value if counts don't match (lazy sync on draw).
        if (_signalSinks.size() != data_sinks.value.size()) {
            syncSinksFromNames(data_sinks.value);
        }

        // TODO(Step 7): Remove invokeWork() calls after ImPlotSink refactoring.
        // Currently required because ImPlotSink::work() skips processing when tab is visible,
        // expecting draw() to call invokeWork() instead.
        for (const auto& sink : _signalSinks) {
            if (sink) {
                std::ignore = sink->invokeWork();
            }
        }

        // Extract layoutMode from config (default false)
        bool layoutMode = false;
        if (auto it = config.find("layoutMode"); it != config.end()) {
            if (const auto* val = std::get_if<bool>(&it->second)) {
                layoutMode = *val;
            }
        }

        // Show legend if show_legend property is true OR if in layout mode
        const bool effectiveShowLegend = show_legend.value || layoutMode;

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", effectiveShowLegend);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 1) {
            drawXYFallback(effectiveShowLegend);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 2) {
            drawCorrelation(effectiveShowLegend);
            return gr::work::Status::OK;
        }

        drawMultiCorrelation(effectiveShowLegend);
        return gr::work::Status::OK;
    }

    // --- Identity ---
    [[nodiscard]] std::string_view chartTypeName() const noexcept override { return "YYChart"; }
    [[nodiscard]] std::string_view uniqueId() const noexcept override { return this->unique_name; }

private:
    static constexpr std::size_t defaultHistorySize() noexcept { return 4096; }

    void setupAxesWithAutoFit(const char* xLabel, const char* yLabel) {
        // Set up X axis with auto-fit if enabled
        ImPlotAxisFlags xFlags = x_auto_scale ? (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit) : ImPlotAxisFlags_None;
        ImPlot::SetupAxis(ImAxis_X1, xLabel, xFlags);

        // Set up Y axis with auto-fit if enabled
        ImPlotAxisFlags yFlags = y_auto_scale ? (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit) : ImPlotAxisFlags_None;
        ImPlot::SetupAxis(ImAxis_Y1, yLabel, yFlags);
    }

    /**
     * @brief Build axis label from signal name and unit.
     * @return "SignalName [unit]" or just "SignalName" if no unit
     */
    static std::string buildAxisLabel(const SignalSink* sink) {
        if (!sink) {
            return "";
        }
        std::string      label(sink->signalName());
        std::string_view unit = sink->signalUnit();
        if (!unit.empty()) {
            label += " [";
            label += unit;
            label += "]";
        }
        return label;
    }

    void drawEmptyPlot(const char* message, bool effectiveShowLegend) {
        ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!effectiveShowLegend) {
            plotFlags |= ImPlotFlags_NoLegend;
        }
        if (DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, ImGui::GetContentRegionAvail(), plotFlags)) {
            // Use fixed limits for empty plots - auto-fit without data causes oscillation
            ImPlot::SetupAxis(ImAxis_X1, "X", ImPlotAxisFlags_None);
            ImPlot::SetupAxis(ImAxis_Y1, "Y", ImPlotAxisFlags_None);
            ImPlot::SetupAxisLimits(ImAxis_X1, -1.0, 1.0, ImPlotCond_Once);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -1.0, 1.0, ImPlotCond_Once);
            ImPlot::SetupFinish();
            auto limits = ImPlot::GetPlotLimits();
            ImPlot::PlotText(message, (limits.X.Min + limits.X.Max) / 2, (limits.Y.Min + limits.Y.Max) / 2);
            tooltip::showPlotMouseTooltip();
            handleInteractions(); // Enable D&D even when empty
            DigitizerUi::TouchHandler<>::EndZoomablePlot();
        }
    }

    void drawXYFallback(bool effectiveShowLegend) {
        const auto& sinkPtr = _signalSinks[0];
        if (!sinkPtr) {
            drawEmptyPlot("No data", effectiveShowLegend);
            return;
        }

        // Skip if signal is hidden
        if (!sinkPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", effectiveShowLegend);
            return;
        }

        // Acquire lock for thread-safe data access
        auto dataLock = sinkPtr->acquireDataLock();
        if (sinkPtr->size() == 0) {
            drawEmptyPlot("No data", effectiveShowLegend);
            return;
        }

        ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!effectiveShowLegend) {
            plotFlags |= ImPlotFlags_NoLegend;
        }
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, ImGui::GetContentRegionAvail(), plotFlags)) {
            return;
        }

        // X-axis: Time, Y-axis: signal name with unit
        setupAxesWithAutoFit("Time", buildAxisLabel(sinkPtr.get()).c_str());
        setupAxisScales();
        ImPlot::SetupFinish();

        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkPtr->color()));
        ImPlot::SetNextLineStyle(lineColor);

        struct XYContext {
            const SignalSink* sink;
        };
        XYContext ctx{sinkPtr.get()};

        // Use signalName for legend entry (for D&D)
        ImPlot::PlotLineG(
            std::string(sinkPtr->signalName()).c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  c = static_cast<XYContext*>(userData);
                double x = c->sink->xAt(static_cast<std::size_t>(idx));
                double y = static_cast<double>(c->sink->yAt(static_cast<std::size_t>(idx)));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(sinkPtr->size()));

        tooltip::showPlotMouseTooltip();
        handleInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }

    void drawCorrelation(bool effectiveShowLegend) {
        const auto& sinkXPtr = _signalSinks[0];
        const auto& sinkYPtr = _signalSinks[1];

        if (!sinkXPtr || !sinkYPtr) {
            drawEmptyPlot("Invalid sinks", effectiveShowLegend);
            return;
        }

        // Skip if any signal is hidden
        if (!sinkXPtr->drawEnabled() || !sinkYPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", effectiveShowLegend);
            return;
        }

        // Acquire locks for thread-safe data access
        auto lockX = sinkXPtr->acquireDataLock();
        auto lockY = sinkYPtr->acquireDataLock();

        std::size_t count = std::min(sinkXPtr->size(), sinkYPtr->size());
        if (count == 0) {
            drawEmptyPlot("No data", effectiveShowLegend);
            return;
        }

        ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!effectiveShowLegend) {
            plotFlags |= ImPlotFlags_NoLegend;
        }
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, ImGui::GetContentRegionAvail(), plotFlags)) {
            return;
        }

        // Build axis labels with signal name and unit
        std::string xLabel = buildAxisLabel(sinkXPtr.get());
        std::string yLabel = buildAxisLabel(sinkYPtr.get());
        setupAxesWithAutoFit(xLabel.c_str(), yLabel.c_str());
        setupAxisScales();
        ImPlot::SetupFinish();

        // Plot X signal as invisible dummy to create legend entry for D&D
        {
            ImVec4 xColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkXPtr->color()));
            ImPlot::SetNextLineStyle(xColor);
            double dummyX = static_cast<double>(sinkXPtr->yAt(0));
            double dummyY = static_cast<double>(sinkYPtr->yAt(0));
            ImPlot::PlotLine(std::string(sinkXPtr->signalName()).c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        // Plot correlation line with Y signal's name as legend entry
        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkYPtr->color()));
        ImPlot::SetNextLineStyle(lineColor);

        struct CorrelationContext {
            const SignalSink* sinkX;
            const SignalSink* sinkY;
        };
        CorrelationContext ctx{sinkXPtr.get(), sinkYPtr.get()};

        ImPlot::PlotLineG(
            std::string(sinkYPtr->signalName()).c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  c = static_cast<CorrelationContext*>(userData);
                double x = static_cast<double>(c->sinkX->yAt(static_cast<std::size_t>(idx)));
                double y = static_cast<double>(c->sinkY->yAt(static_cast<std::size_t>(idx)));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(count));

        tooltip::showPlotMouseTooltip();
        handleInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }

    void drawMultiCorrelation(bool effectiveShowLegend) {
        const auto& sinkXPtr = _signalSinks[0];
        if (!sinkXPtr) {
            drawEmptyPlot("No X data", effectiveShowLegend);
            return;
        }

        // Skip if X signal is hidden (required for correlation)
        if (!sinkXPtr->drawEnabled()) {
            drawEmptyPlot("X signal hidden", effectiveShowLegend);
            return;
        }

        // Acquire lock for X sink
        auto lockX = sinkXPtr->acquireDataLock();
        if (sinkXPtr->size() == 0) {
            drawEmptyPlot("No X data", effectiveShowLegend);
            return;
        }

        ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!effectiveShowLegend) {
            plotFlags |= ImPlotFlags_NoLegend;
        }
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, ImGui::GetContentRegionAvail(), plotFlags)) {
            return;
        }

        // X-axis label from first signal, Y-axis generic (multiple Y signals)
        std::string xLabel = buildAxisLabel(sinkXPtr.get());
        setupAxesWithAutoFit(xLabel.c_str(), "Y");
        setupAxisScales();
        ImPlot::SetupFinish();

        // Plot X signal as invisible dummy to create legend entry for D&D
        {
            ImVec4 xColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkXPtr->color()));
            ImPlot::SetNextLineStyle(xColor);
            double dummyX = static_cast<double>(sinkXPtr->yAt(0));
            double dummyY = 0.0; // Just need a point for the legend entry
            ImPlot::PlotLine(std::string(sinkXPtr->signalName()).c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        for (std::size_t i = 1; i < _signalSinks.size(); ++i) {
            const auto& sinkYPtr = _signalSinks[i];
            if (!sinkYPtr || !sinkYPtr->drawEnabled()) {
                continue;
            }

            // Acquire lock for Y sink
            auto lockY = sinkYPtr->acquireDataLock();

            std::size_t count = std::min(sinkXPtr->size(), sinkYPtr->size());
            if (count == 0) {
                continue;
            }

            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkYPtr->color()));
            ImPlot::SetNextLineStyle(lineColor);

            struct MultiContext {
                const SignalSink* sinkX;
                const SignalSink* sinkY;
            };
            MultiContext ctx{sinkXPtr.get(), sinkYPtr.get()};

            ImPlot::PlotLineG(
                std::string(sinkYPtr->signalName()).c_str(),
                [](int idx, void* userData) -> ImPlotPoint {
                    auto*  c = static_cast<MultiContext*>(userData);
                    double x = static_cast<double>(c->sinkX->yAt(static_cast<std::size_t>(idx)));
                    double y = static_cast<double>(c->sinkY->yAt(static_cast<std::size_t>(idx)));
                    return ImPlotPoint(x, y);
                },
                &ctx, static_cast<int>(count));
        }

        tooltip::showPlotMouseTooltip();
        handleInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }

    void setupAxisScales() {
        // Set axis scale (log10 if configured)
        if (static_cast<AxisScale>(x_axis_scale.value) == AxisScale::Log10) {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        }
        if (static_cast<AxisScale>(y_axis_scale.value) == AxisScale::Log10) {
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        }

        // Set axis limits (only when auto-scale is disabled)
        if (!x_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(x_min.value), static_cast<double>(x_max.value));
        }
        if (!y_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(y_min.value), static_cast<double>(y_max.value));
        }
    }

    void handleInteractions() {
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("YYChartContextMenu");
        }

        if (ImGui::BeginPopup("YYChartContextMenu")) {
            if (ImGui::MenuItem("Show Legend", nullptr, show_legend.value)) {
                show_legend = !show_legend.value;
            }
            if (ImGui::MenuItem("Show Grid", nullptr, show_grid.value)) {
                show_grid = !show_grid.value;
            }

            ImGui::Separator();

            // Chart type transmutation menu
            if (ImGui::BeginMenu("Change Chart Type")) {
                // Known chart types - in the future, could query block registry
                constexpr std::array<const char*, 2> chartTypes = {"XYChart", "YYChart"};
                for (const auto* type : chartTypes) {
                    bool isCurrent = (std::string_view(type) == chartTypeName());
                    if (ImGui::MenuItem(type, nullptr, isCurrent)) {
                        if (!isCurrent && g_requestChartTransmutation) {
                            g_requestChartTransmutation(uniqueId(), type);
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }

        // Drag source handling - set up drag sources for legend items
        for (const auto& sink : _signalSinks) {
            std::string signalNameStr(sink->signalName());
            if (ImPlot::BeginDragDropSourceItem(signalNameStr.c_str())) {
                DndHelper::setupDragPayload(sink, uniqueId());
                DndHelper::renderDragTooltip(sink);
                ImPlot::EndDragDropSource();
            }
        }

        // Drop target handling - use DndHelper for self-contained D&D
        DndHelper::handleDropTarget(this);
    }
};

} // namespace opendigitizer::charts

// Register YYChart with the GR4 block registry
GR_REGISTER_BLOCK("opendigitizer::charts::YYChart", opendigitizer::charts::YYChart)

#endif // OPENDIGITIZER_CHARTS_YYCHART_HPP
