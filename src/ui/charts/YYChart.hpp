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

namespace gr::od::charts {

/// one signal against another, for Lissajous figures, phase plots and I-Q diagrams:
/// one signal plots over time, two as Y1 vs Y2, three or more against the first
struct YYChart : gr::Block<YYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName    = "YYChart";
    static constexpr SignalKind       kSupportedSignals = SignalKind::Streaming;

    struct MultiYCategories {
        std::array<std::optional<AxisCategory>, 3> yCategories{};
        std::array<std::vector<std::string>, 3>    yAxisGroups{};
        std::vector<std::size_t>                   overflowSinkIndices;
    };

    A<std::string, "chart name", gr::Visible>                                         chart_name;
    A<std::string, "chart title">                                                     chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                            data_sinks    = {};
    A<bool, "show legend", gr::Visible>                                               show_legend   = false;
    A<bool, "show grid", gr::Visible>                                                 show_grid     = true;
    A<bool, "anti-aliasing">                                                          anti_aliasing = true;
    A<int, "X-axis scale">                                                            x_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<int, "Y-axis scale">                                                            y_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<std::array<bool, 3UZ>, "X auto-scale">                                          x_auto_scale  = std::array{true, true, true};
    A<std::array<bool, 3UZ>, "Y auto-scale">                                          y_auto_scale  = std::array{true, true, true};
    A<std::array<double, 3UZ>, "X-axis min">                                          x_min         = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "X-axis max">                                          x_max         = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis min">                                          y_min         = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis max">                                          y_max         = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<gr::Size_t, "history depth", gr::Unit<"samples">>                               n_history     = kDefaultHistorySize;
    A<bool, "scatter", gr::Doc<"markers instead of a line, the newest point larger">> scatter       = false;

    GR_MAKE_REFLECTABLE(YYChart, chart_name, chart_title, data_sinks, show_legend, show_grid, anti_aliasing, x_axis_scale, y_axis_scale, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, n_history, scatter);

    mutable std::array<std::string, 6UZ> _unitStringStorage{};
    std::size_t                          _drawnMarkers = 0UZ; // in the last draw

    explicit YYChart(gr::property_map initParameters = {}) : gr::Block<YYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>(std::move(initParameters)) {}

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { applyChartSettings(newSettings); }

    [[nodiscard]] std::size_t drawnMarkers() const noexcept { return _drawnMarkers; }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);
        _drawnMarkers                                                                            = 0UZ;

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 1) {
            drawXYFallback(plotFlags, plotSize, chartMode, showGrid);
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 2) {
            drawCorrelation(plotFlags, plotSize, chartMode, showGrid);
            return gr::work::Status::OK;
        }

        drawMultiCorrelation(plotFlags, plotSize, chartMode, showGrid);
        return gr::work::Status::OK;
    }

    [[nodiscard]] std::optional<AxisScale> getAxisScale(AxisKind axis) const noexcept { return static_cast<AxisScale>((axis == AxisKind::X) ? x_axis_scale.value : y_axis_scale.value); }

    void setAxisScale(AxisKind axis, AxisScale scale) {
        if (axis == AxisKind::X) {
            x_axis_scale = static_cast<int>(scale);
            std::ignore  = this->settings().set({{"x_axis_scale", x_axis_scale.value}});
        } else {
            y_axis_scale = static_cast<int>(scale);
            std::ignore  = this->settings().set({{"y_axis_scale", y_axis_scale.value}});
        }
    }

    /// a line, or with `scatter` one marker per point and the newest point larger, under the same legend entry
    void plotSeries(const char* label, ImPlotGetter getter, void* data, std::size_t count, const ImVec4& colour, float lineWidth) {
        if (count == 0UZ) {
            return;
        }
        if (!scatter) {
            ImPlot::SetNextLineStyle(colour, lineWidth);
            ImPlot::PlotLineG(label, getter, data, static_cast<int>(count));
            return;
        }
        const auto& style = LookAndFeel::instance().chartStyle;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, style.scatterMarkerSize, colour, IMPLOT_AUTO, colour);
        ImPlot::PlotScatterG(label, getter, data, static_cast<int>(count));
        const ImPlotPoint newest = getter(static_cast<int>(count - 1UZ), data);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, style.scatterNewestMarkerSize, colour, IMPLOT_AUTO, colour);
        ImPlot::PlotScatter(label, &newest.x, &newest.y, 1);
        _drawnMarkers += count;
    }

    void drawXYFallback(ImPlotFlags plotFlags, const ImVec2& size, ChartMode chartMode, bool showGrid = true) {
        const std::shared_ptr<SignalSink>& sinkPtr = _signalSinks[0];
        if (!sinkPtr) {
            drawEmptyPlot("No data", plotFlags, size, chartMode);
            return;
        }

        if (!sinkPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", plotFlags, size, chartMode);
            return;
        }

        DataGuard dataLock = sinkPtr->dataGuard();
        if (sinkPtr->size() == 0) {
            drawEmptyPlot("No data", plotFlags, size, chartMode);
            return;
        }

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        {
            const auto xDashCfg               = parseAxisConfig(this->ui_constraints.value, true);
            AxisScale  xScale                 = static_cast<AxisScale>(x_axis_scale.value);
            const auto [xMinLimit, xMaxLimit] = axisLimits(x_auto_scale.value[0], x_min.value[0], x_max.value[0], std::nullopt);
            auto xCond                        = trackLimitsCond(true, xMinLimit, xMaxLimit);
            axis::setupAxis(ImAxis_X1, AxisCategory{.quantity = "Time", .unit = "s"}, LabelFormat::Auto, axisLabelWidthOrDefault(xDashCfg, defaultAxisLabelWidthFor(ImAxis_X1, size)), xMinLimit, xMaxLimit, 1UZ, xScale, _unitStringStorage, showGrid, /*foreground=*/false, xCond);

            const auto yDashCfg               = parseAxisConfig(this->ui_constraints.value, false);
            AxisScale  yScale                 = static_cast<AxisScale>(y_axis_scale.value);
            const auto [yMinLimit, yMaxLimit] = axisLimits(y_auto_scale.value[0], y_min.value[0], y_max.value[0], std::nullopt);
            auto         yCond                = trackLimitsCond(false, yMinLimit, yMaxLimit);
            AxisCategory yCat{.quantity = std::string(sinkPtr->signalQuantity()), .unit = std::string(sinkPtr->signalUnit()), .color = sinkPtr->color()};
            axis::setupAxis(ImAxis_Y1, yCat, LabelFormat::Auto, axisLabelWidthOrDefault(yDashCfg, defaultAxisLabelWidthFor(ImAxis_Y1, size)), yMinLimit, yMaxLimit, 1UZ, yScale, _unitStringStorage, showGrid, /*foreground=*/false, yCond);
        }
        setupFinish();

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
        std::string signalLabel = plotLabel(*sinkPtr);

        plotSeries(
            signalLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       c         = static_cast<XYContext*>(user_data);
                std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                double      x         = c->sink->xAt(actualIdx);
                double      y         = static_cast<double>(c->sink->yAt(actualIdx));
                return ImPlotPoint(x, y);
            },
            &ctx, dataCount, sinkColor(sinkPtr->color()), seriesLineWidth(*sinkPtr));

        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
    }

    void drawCorrelation(ImPlotFlags plotFlags, const ImVec2& size, ChartMode chartMode, bool showGrid = true) {
        const auto& sinkXPtr = _signalSinks[0];
        const auto& sinkYPtr = _signalSinks[1];

        if (!sinkXPtr || !sinkYPtr) {
            drawEmptyPlot("Invalid sinks", plotFlags, size, chartMode);
            return;
        }

        if (!sinkXPtr->drawEnabled() || !sinkYPtr->drawEnabled()) {
            drawEmptyPlot("Signal hidden", plotFlags, size, chartMode);
            return;
        }

        const std::vector<DataGuard> guards = lockAll(std::array{sinkXPtr.get(), sinkYPtr.get()});

        std::size_t totalCount = std::min(sinkXPtr->size(), sinkYPtr->size());
        if (totalCount == 0) {
            drawEmptyPlot("No data", plotFlags, size, chartMode);
            return;
        }

        std::size_t count  = std::min(totalCount, static_cast<std::size_t>(n_history.value));
        std::size_t offset = totalCount - count;

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        {
            const auto xDashCfg               = parseAxisConfig(this->ui_constraints.value, true);
            AxisScale  xScale                 = static_cast<AxisScale>(x_axis_scale.value);
            const auto [xMinLimit, xMaxLimit] = axisLimits(x_auto_scale.value[0], x_min.value[0], x_max.value[0], std::nullopt);
            auto         xCond                = trackLimitsCond(true, xMinLimit, xMaxLimit);
            AxisCategory xCat{.quantity = sinkXPtr->signalQuantity().empty() ? std::string(sinkXPtr->signalName()) : std::string(sinkXPtr->signalQuantity()), .unit = std::string(sinkXPtr->signalUnit()), .color = sinkXPtr->color()};
            axis::setupAxis(ImAxis_X1, xCat, LabelFormat::Auto, axisLabelWidthOrDefault(xDashCfg, defaultAxisLabelWidthFor(ImAxis_X1, size)), xMinLimit, xMaxLimit, 1UZ, xScale, _unitStringStorage, showGrid, /*foreground=*/false, xCond);

            const auto yDashCfg               = parseAxisConfig(this->ui_constraints.value, false);
            AxisScale  yScale                 = static_cast<AxisScale>(y_axis_scale.value);
            const auto [yMinLimit, yMaxLimit] = axisLimits(y_auto_scale.value[0], y_min.value[0], y_max.value[0], std::nullopt);
            auto         yCond                = trackLimitsCond(false, yMinLimit, yMaxLimit);
            AxisCategory yCat{.quantity = sinkYPtr->signalQuantity().empty() ? std::string(sinkYPtr->signalName()) : std::string(sinkYPtr->signalQuantity()), .unit = std::string(sinkYPtr->signalUnit()), .color = sinkYPtr->color()};
            axis::setupAxis(ImAxis_Y1, yCat, LabelFormat::Auto, axisLabelWidthOrDefault(yDashCfg, defaultAxisLabelWidthFor(ImAxis_Y1, size)), yMinLimit, yMaxLimit, 1UZ, yScale, _unitStringStorage, showGrid, /*foreground=*/false, yCond);
        }
        setupFinish();

        // the X signal's legend entry, for drag and drop
        {
            ImVec4 xColor = sinkColor(sinkXPtr->color());
            ImPlot::SetNextLineStyle(xColor);
            double      dummyX       = static_cast<double>(sinkXPtr->yAt(offset));
            double      dummyY       = static_cast<double>(sinkYPtr->yAt(offset));
            std::string xLegendLabel = plotLabel(*sinkXPtr);
            ImPlot::PlotLine(xLegendLabel.c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        struct CorrelationContext {
            const SignalSink* sinkX;
            const SignalSink* sinkY;
            std::size_t       offset;
        };
        CorrelationContext ctx{sinkXPtr.get(), sinkYPtr.get(), offset};
        std::string        yLegendLabel = plotLabel(*sinkYPtr);

        plotSeries(
            yLegendLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       c         = static_cast<CorrelationContext*>(user_data);
                std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                double      x         = static_cast<double>(c->sinkX->yAt(actualIdx));
                double      y         = static_cast<double>(c->sinkY->yAt(actualIdx));
                return ImPlotPoint(x, y);
            },
            &ctx, count, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr));

        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
    }

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

    void drawMultiCorrelation(ImPlotFlags plotFlags, const ImVec2& size, ChartMode chartMode, bool showGrid = true) {
        const auto& sinkXPtr = _signalSinks[0];
        if (!sinkXPtr) {
            drawEmptyPlot("No X data", plotFlags, size, chartMode);
            return;
        }
        if (!sinkXPtr->drawEnabled()) {
            drawEmptyPlot("X signal hidden", plotFlags, size, chartMode);
            return;
        }

        {
            DataGuard lockX = sinkXPtr->dataGuard();
            if (sinkXPtr->size() == 0) {
                drawEmptyPlot("No X data", plotFlags, size, chartMode);
                return;
            }
        }

        auto [yCategories, yAxisGroups, overflowSinkIndices] = buildMultiYCategories();
        std::size_t nYAxes                                   = axis::activeAxisCount(yCategories);

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        // metadata accessors need no data lock
        {
            std::optional<AxisCategory> xCategory = AxisCategory{.quantity = sinkXPtr->signalQuantity().empty() ? std::string(sinkXPtr->signalName()) : std::string(sinkXPtr->signalQuantity()), .unit = std::string(sinkXPtr->signalUnit()), .color = sinkXPtr->color()};
            const auto                  xDashCfg  = parseAxisConfig(this->ui_constraints.value, true);
            AxisScale                   xScale    = static_cast<AxisScale>(x_axis_scale.value);
            const auto [xMinLimit, xMaxLimit]     = axisLimits(x_auto_scale.value[0], x_min.value[0], x_max.value[0], std::nullopt);
            auto xCond                            = trackLimitsCond(true, xMinLimit, xMaxLimit);
            axis::setupAxis(ImAxis_X1, xCategory, LabelFormat::Auto, axisLabelWidthOrDefault(xDashCfg, defaultAxisLabelWidthFor(ImAxis_X1, size)), xMinLimit, xMaxLimit, 1UZ, xScale, _unitStringStorage, showGrid, /*foreground=*/false, xCond);
        }

        AxisScale yScale = static_cast<AxisScale>(y_axis_scale.value);
        for (std::size_t i = 0; i < yCategories.size(); ++i) {
            if (!yCategories[i].has_value()) {
                continue;
            }
            const auto [yMinLimit, yMaxLimit] = axisLimits(y_auto_scale.value[i], y_min.value[i], y_max.value[i], std::nullopt);
            auto       yCond                  = trackLimitsCond(false, yMinLimit, yMaxLimit, i);
            auto       yDashCfg               = parseAxisConfig(this->ui_constraints.value, false, i);
            const auto axisId                 = ImAxis_Y1 + static_cast<int>(i);
            axis::setupAxis(axisId, yCategories[i], LabelFormat::Auto, axisLabelWidthOrDefault(yDashCfg, defaultAxisLabelWidthFor(axisId, size)), yMinLimit, yMaxLimit, nYAxes, yScale, _unitStringStorage, showGrid, /*foreground=*/false, yCond);
        }

        setupFinish();

        if (!overflowSinkIndices.empty()) {
            auto        limits  = ImPlot::GetPlotLimits();
            std::string warning = std::format("{} signal(s) hidden (max 3 Y-axes)", overflowSinkIndices.size());
            ImPlot::PlotText(warning.c_str(), (limits.X.Min + limits.X.Max) / 2, limits.Y.Max - (limits.Y.Max - limits.Y.Min) * 0.05);
        }

        // the X signal's legend entry, for drag and drop
        {
            auto   lockX  = sinkXPtr->dataGuard();
            ImVec4 xColor = sinkColor(sinkXPtr->color());
            ImPlot::SetNextLineStyle(xColor);
            double      dummyX       = static_cast<double>(sinkXPtr->yAt(0));
            double      dummyY       = static_cast<double>(sinkXPtr->yAt(0));
            std::string xLegendLabel = plotLabel(*sinkXPtr);
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

            const std::vector<DataGuard> guards = lockAll(std::array{sinkXPtr.get(), sinkYPtr.get()});

            std::size_t totalCount = std::min(sinkXPtr->size(), sinkYPtr->size());
            if (totalCount == 0) {
                continue;
            }

            std::size_t count  = std::min(totalCount, static_cast<std::size_t>(n_history.value));
            std::size_t offset = totalCount - count;

            std::size_t yAxisIdx = axis::findAxisForSink(sinkYPtr->uniqueName(), false, std::array<std::vector<std::string>, 3>{}, yAxisGroups);
            ImPlot::SetAxes(ImAxis_X1, static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            PlotContext ctx{sinkXPtr.get(), sinkYPtr.get(), offset};
            std::string yLabel = plotLabel(*sinkYPtr);
            plotSeries(
                yLabel.c_str(),
                [](int idx, void* user_data) -> ImPlotPoint {
                    auto*       c         = static_cast<PlotContext*>(user_data);
                    std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                    double      x         = static_cast<double>(c->sinkX->yAt(actualIdx));
                    double      y         = static_cast<double>(c->sinkY->yAt(actualIdx));
                    return ImPlotPoint(x, y);
                },
                &ctx, count, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr));
        }

        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
    }
};

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_YYCHART_HPP
