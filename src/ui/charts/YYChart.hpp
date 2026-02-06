#ifndef OPENDIGITIZER_CHARTS_YYCHART_HPP
#define OPENDIGITIZER_CHARTS_YYCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
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

/// YYChart - Correlation plot (Y1 vs Y2) as a GR4 block.
/// Plots Y-values from one signal against another for Lissajous figures, phase plots, I-Q diagrams.
/// Signal modes: 1 signal = XY fallback, 2 signals = Y1 vs Y2, 3+ signals = first as X, rest as Y.
struct YYChart : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>, Chart {
    // Annotated type alias for cleaner declarations
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

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
    A<double, "X-axis min">                                x_min         = std::numeric_limits<double>::lowest();
    A<double, "X-axis max">                                x_max         = std::numeric_limits<double>::max();
    A<double, "Y-axis min">                                y_min         = std::numeric_limits<double>::lowest();
    A<double, "Y-axis max">                                y_max         = std::numeric_limits<double>::max();
    A<gr::Size_t, "history depth", gr::Unit<"samples">>    n_history     = kDefaultHistorySize;

    GR_MAKE_REFLECTABLE(YYChart, chart_name, chart_title, data_sinks, show_legend, show_grid, anti_aliasing, x_axis_scale, y_axis_scale, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, n_history);

    mutable std::array<std::string, 6> _unitStringStorage{};

    static constexpr std::string_view kChartTypeName = "YYChart";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    explicit YYChart(gr::property_map initParameters = {}) : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>(std::move(initParameters)) {}

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 1) {
            drawXYFallback(plotFlags, plotSize, showGrid);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 2) {
            drawCorrelation(plotFlags, plotSize, showGrid);
            return gr::work::Status::OK;
        }

        drawMultiCorrelation(plotFlags, plotSize, showGrid);
        return gr::work::Status::OK;
    }

    [[nodiscard]] AxisScale getAxisScale(bool isX) const noexcept { return static_cast<AxisScale>(isX ? x_axis_scale.value : y_axis_scale.value); }

    void setAxisScale(bool isX, AxisScale scale) {
        if (isX) {
            x_axis_scale = static_cast<int>(scale);
            std::ignore  = this->settings().set({{"x_axis_scale", x_axis_scale.value}});
        } else {
            y_axis_scale = static_cast<int>(scale);
            std::ignore  = this->settings().set({{"y_axis_scale", y_axis_scale.value}});
        }
    }

    void setupAxesWithAutoFit(const char* xLabel, const char* yLabel, bool showGrid = true) {
        ImPlotAxisFlags xFlags = x_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
        ImPlotAxisFlags yFlags = y_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None;
        if (!showGrid) {
            xFlags |= ImPlotAxisFlags_NoGridLines;
            yFlags |= ImPlotAxisFlags_NoGridLines;
        }
        ImPlot::SetupAxis(ImAxis_X1, xLabel, xFlags);
        ImPlot::SetupAxis(ImAxis_Y1, yLabel, yFlags);
    }

    /// Build axis label from signal name and unit ("SignalName [unit]" or just "SignalName")
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

    void drawXYFallback(ImPlotFlags plotFlags, const ImVec2& size, bool showGrid = true) {
        const std::shared_ptr<SignalSink>& sinkPtr = _signalSinks[0];
        if (!sinkPtr) {
            drawEmptyPlot("No data", plotFlags, size);
            return;
        }

        // Skip if signal is hidden
        if (!sinkPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", plotFlags, size);
            return;
        }

        // Acquire lock for thread-safe data access
        DataGuard dataLock = sinkPtr->dataGuard();
        if (sinkPtr->size() == 0) {
            drawEmptyPlot("No data", plotFlags, size);
            return;
        }

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        // X-axis: Time, Y-axis: signal name with unit
        setupAxesWithAutoFit("Time", buildAxisLabel(sinkPtr.get()).c_str(), showGrid);
        setupAxisScales();
        ImPlot::SetupFinish();

        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkPtr->color()));
        ImPlot::SetNextLineStyle(lineColor);

        // clamp to n_history: show only the most recent samples
        std::size_t totalCount = sinkPtr->size();
        std::size_t dataCount  = totalCount;
        std::size_t offset     = 0;
        if (dataCount > static_cast<std::size_t>(n_history.value)) {
            dataCount = static_cast<std::size_t>(n_history.value);
            offset    = totalCount - dataCount;
        }

        struct XYContext {
            const SignalSink* sink;
            std::size_t       offset;
        };
        XYContext   ctx{sinkPtr.get(), offset};
        std::string signalLabel{sinkPtr->signalName()};

        ImPlot::PlotLineG(
            signalLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       c         = static_cast<XYContext*>(user_data);
                std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                double      x         = c->sink->xAt(actualIdx);
                double      y         = static_cast<double>(c->sink->yAt(actualIdx));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(dataCount));

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }

    void drawCorrelation(ImPlotFlags plotFlags, const ImVec2& size, bool showGrid = true) {
        const auto& sinkXPtr = _signalSinks[0];
        const auto& sinkYPtr = _signalSinks[1];

        if (!sinkXPtr || !sinkYPtr) {
            drawEmptyPlot("Invalid sinks", plotFlags, size);
            return;
        }

        // Skip if any signal is hidden
        if (!sinkXPtr->drawEnabled() || !sinkYPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", plotFlags, size);
            return;
        }

        // Acquire locks in canonical pointer order to prevent ABBA deadlock
        const auto& [first, second] = (sinkXPtr.get() < sinkYPtr.get()) ? std::tie(sinkXPtr, sinkYPtr) : std::tie(sinkYPtr, sinkXPtr);
        DataGuard lockFirst         = first->dataGuard();
        DataGuard lockSecond        = second->dataGuard();

        std::size_t totalCount = std::min(sinkXPtr->size(), sinkYPtr->size());
        if (totalCount == 0) {
            drawEmptyPlot("No data", plotFlags, size);
            return;
        }

        // clamp to n_history: show only the most recent samples
        std::size_t count  = std::min(totalCount, static_cast<std::size_t>(n_history.value));
        std::size_t offset = totalCount - count;

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        // Build axis labels with signal name and unit
        std::string xLabel = buildAxisLabel(sinkXPtr.get());
        std::string yLabel = buildAxisLabel(sinkYPtr.get());
        setupAxesWithAutoFit(xLabel.c_str(), yLabel.c_str(), showGrid);
        setupAxisScales();
        ImPlot::SetupFinish();

        // Plot X signal as dummy to create legend entry for D&D
        {
            ImVec4 xColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkXPtr->color()));
            ImPlot::SetNextLineStyle(xColor);
            double      dummyX = static_cast<double>(sinkXPtr->yAt(offset));
            double      dummyY = static_cast<double>(sinkYPtr->yAt(offset));
            std::string xLegendLabel{sinkXPtr->signalName()};
            ImPlot::PlotLine(xLegendLabel.c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        // Plot correlation line with Y signal's name as legend entry
        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkYPtr->color()));
        ImPlot::SetNextLineStyle(lineColor);

        struct CorrelationContext {
            const SignalSink* sink_x;
            const SignalSink* sink_y;
            std::size_t       offset;
        };
        CorrelationContext ctx{sinkXPtr.get(), sinkYPtr.get(), offset};
        std::string        yLegendLabel{sinkYPtr->signalName()};

        ImPlot::PlotLineG(
            yLegendLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       c         = static_cast<CorrelationContext*>(user_data);
                std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                double      x         = static_cast<double>(c->sink_x->yAt(actualIdx));
                double      y         = static_cast<double>(c->sink_y->yAt(actualIdx));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(count));

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }

    struct MultiYCategories {
        std::array<std::optional<AxisCategory>, 3> yCategories{};
        std::array<std::vector<std::string>, 3>    yAxisGroups{};
        std::vector<std::size_t>                   overflowSinkIndices;
    };

    MultiYCategories buildMultiYCategories() const {
        MultiYCategories result;
        for (std::size_t i = 1; i < _signalSinks.size(); ++i) {
            const std::shared_ptr<SignalSink>& sink = _signalSinks[i];
            if (!sink) {
                continue;
            }
            auto idx = axis::findOrCreateCategory(result.yCategories, sink->signalQuantity(), sink->signalUnit(), sink->color());
            if (idx.has_value()) {
                result.yAxisGroups[*idx].push_back(std::string(sink->uniqueName()));
            } else {
                result.overflowSinkIndices.push_back(i);
            }
        }
        return result;
    }

    void drawMultiCorrelation(ImPlotFlags plotFlags, const ImVec2& size, bool showGrid = true) {
        const auto& sinkXPtr = _signalSinks[0];
        if (!sinkXPtr) {
            drawEmptyPlot("No X data", plotFlags, size);
            return;
        }
        if (!sinkXPtr->drawEnabled()) {
            drawEmptyPlot("X signal hidden", plotFlags, size);
            return;
        }

        // Check X has data (brief scoped lock)
        {
            DataGuard lockX = sinkXPtr->dataGuard();
            if (sinkXPtr->size() == 0) {
                drawEmptyPlot("No X data", plotFlags, size);
                return;
            }
        }

        auto [yCategories, yAxisGroups, overflowSinkIndices] = buildMultiYCategories();
        std::size_t nYAxes                                   = axis::activeAxisCount(yCategories);

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        // X-axis from sink[0]'s signal metadata (no data lock needed for metadata accessors)
        {
            std::optional<AxisCategory> xCategory = AxisCategory{.quantity = sinkXPtr->signalQuantity().empty() ? std::string(sinkXPtr->signalName()) : std::string(sinkXPtr->signalQuantity()), .unit = std::string(sinkXPtr->signalUnit()), .color = sinkXPtr->color()};
            AxisScale                   xScale    = static_cast<AxisScale>(x_axis_scale.value);
            double                      xMinLimit = x_auto_scale ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(x_min.value);
            double                      xMaxLimit = x_auto_scale ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(x_max.value);
            axis::setupAxis(ImAxis_X1, xCategory, LabelFormat::Auto, 100.f, xMinLimit, xMaxLimit, 1, xScale, _unitStringStorage, showGrid);
        }

        // Y-axes (up to 3) grouped by quantity+unit
        AxisScale yScale = static_cast<AxisScale>(y_axis_scale.value);
        for (std::size_t i = 0; i < yCategories.size(); ++i) {
            if (!yCategories[i].has_value()) {
                continue;
            }
            double yMinLimit = (i == 0 && !y_auto_scale) ? static_cast<double>(y_min.value) : std::numeric_limits<double>::quiet_NaN();
            double yMaxLimit = (i == 0 && !y_auto_scale) ? static_cast<double>(y_max.value) : std::numeric_limits<double>::quiet_NaN();
            axis::setupAxis(ImAxis_Y1 + static_cast<int>(i), yCategories[i], LabelFormat::Auto, 100.f, yMinLimit, yMaxLimit, nYAxes, yScale, _unitStringStorage, showGrid);
        }

        ImPlot::SetupFinish();

        if (!overflowSinkIndices.empty()) {
            auto        limits  = ImPlot::GetPlotLimits();
            std::string warning = std::format("{} signal(s) hidden (max 3 Y-axes)", overflowSinkIndices.size());
            ImPlot::PlotText(warning.c_str(), (limits.X.Min + limits.X.Max) / 2, limits.Y.Max - (limits.Y.Max - limits.Y.Min) * 0.05);
        }

        // dummy plot for sink[0] legend entry (D&D support)
        {
            auto   lockX  = sinkXPtr->dataGuard();
            ImVec4 xColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkXPtr->color()));
            ImPlot::SetNextLineStyle(xColor);
            double      dummyX = static_cast<double>(sinkXPtr->yAt(0));
            double      dummyY = static_cast<double>(sinkXPtr->yAt(0));
            std::string xLegendLabel{sinkXPtr->signalName()};
            ImPlot::PlotLine(xLegendLabel.c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        struct PlotContext {
            const SignalSink* sinkX;
            const SignalSink* sinkY;
            std::size_t       offset;
        };

        for (std::size_t i = 1; i < _signalSinks.size(); ++i) {
            if (std::ranges::contains(overflowSinkIndices, i)) {
                continue;
            }

            const auto& sinkYPtr = _signalSinks[i];
            if (!sinkYPtr || !sinkYPtr->drawEnabled()) {
                continue;
            }

            // Acquire locks in canonical pointer order to prevent ABBA deadlock
            const auto& [first, second] = (sinkXPtr.get() < sinkYPtr.get()) ? std::tie(sinkXPtr, sinkYPtr) : std::tie(sinkYPtr, sinkXPtr);
            auto lockFirst              = first->dataGuard();
            auto lockSecond             = (first.get() != second.get()) ? second->dataGuard() : DataGuard{};

            std::size_t totalCount = std::min(sinkXPtr->size(), sinkYPtr->size());
            if (totalCount == 0) {
                continue;
            }

            std::size_t count  = std::min(totalCount, static_cast<std::size_t>(n_history.value));
            std::size_t offset = totalCount - count;

            std::size_t yAxisIdx = axis::findAxisForSink(sinkYPtr->uniqueName(), false, std::array<std::vector<std::string>, 3>{}, yAxisGroups);
            ImPlot::SetAxes(ImAxis_X1, static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkYPtr->color()));
            ImPlot::SetNextLineStyle(lineColor);

            PlotContext ctx{sinkXPtr.get(), sinkYPtr.get(), offset};
            std::string yLabel{sinkYPtr->signalName()};
            ImPlot::PlotLineG(
                yLabel.c_str(),
                [](int idx, void* user_data) -> ImPlotPoint {
                    auto*       c         = static_cast<PlotContext*>(user_data);
                    std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                    double      x         = static_cast<double>(c->sinkX->yAt(actualIdx));
                    double      y         = static_cast<double>(c->sinkY->yAt(actualIdx));
                    return ImPlotPoint(x, y);
                },
                &ctx, static_cast<int>(count));
        }

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
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
        // Use ImPlotCond_Always to ensure limits from context menu are applied every frame
        if (!x_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(x_min.value), static_cast<double>(x_max.value), ImPlotCond_Always);
        }
        if (!y_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(y_min.value), static_cast<double>(y_max.value), ImPlotCond_Always);
        }
    }
};

} // namespace opendigitizer::charts

// Register YYChart with the GR4 block registry
// GR_REGISTER_BLOCK is a marker macro for build tools; actual registration via gr::registerBlock
GR_REGISTER_BLOCK("opendigitizer::charts::YYChart", opendigitizer::charts::YYChart)
inline auto registerYYChart = gr::registerBlock<opendigitizer::charts::YYChart>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_YYCHART_HPP
