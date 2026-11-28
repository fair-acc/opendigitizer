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

struct YYChart : gr::Block<YYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"One signal against another, for Lissajous figures, phase plots and I-Q diagrams: one signal plots over time, two as Y1 vs Y2, three or more against the first.">;

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
    ErrorStyleSetting                                                                 error_style   = ErrorStyle::combo;

    GR_MAKE_REFLECTABLE(YYChart, chart_name, chart_title, data_sinks, show_legend, show_grid, anti_aliasing, x_axis_scale, y_axis_scale, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, n_history, scatter, error_style);

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

    struct Correlation { // two signals paired from the newest end: the sinks may hold different counts
        SampleWindow x;
        SampleWindow y;

        static ImPlotPoint point(int idx, void* data) {
            const auto* c = static_cast<const Correlation*>(data);
            return {c->x.yAt(static_cast<std::size_t>(idx)), c->y.yAt(static_cast<std::size_t>(idx))};
        }
    };

    [[nodiscard]] static AxisCategory signalCategory(const SignalSink& sink) { //
        return {.quantity = sink.signalQuantity().empty() ? std::string(sink.signalName()) : std::string(sink.signalQuantity()), .unit = std::string(sink.signalUnit()), .color = sink.color()};
    }

    void setupSignalAxis(bool isX, std::size_t index, const std::optional<AxisCategory>& category, std::size_t nAxes, const ImVec2& size, bool showGrid) {
        const auto axisId               = (isX ? ImAxis_X1 : ImAxis_Y1) + static_cast<int>(index);
        const auto dashCfg              = parseAxisConfig(this->ui_constraints.value, isX, index);
        const auto [minLimit, maxLimit] = isX ? axisLimits(x_auto_scale.value[index], x_min.value[index], x_max.value[index], std::nullopt) : axisLimits(y_auto_scale.value[index], y_min.value[index], y_max.value[index], std::nullopt);
        const auto cond                 = trackLimitsCond(isX, minLimit, maxLimit, index);

        axis::setupAxis(axisId, category, LabelFormat::Auto, axisLabelWidthOrDefault(dashCfg, defaultAxisLabelWidthFor(axisId, size)), minLimit, maxLimit, nAxes, static_cast<AxisScale>(isX ? x_axis_scale.value : y_axis_scale.value), _unitStringStorage, showGrid, /*foreground=*/false, cond, nullptr, this->decorated(axisId, isX ? AxisKind::X : AxisKind::Y, index));
    }

    /// bars for every style but none: a surface needs x ordered, which a correlation's is not
    void drawCorrelationErrors(const SampleWindow& x, const SampleWindow& y, ImVec4 colour, float lineWidth, std::string_view uniqueName) const {
        const std::size_t n = std::min(x.size(), y.size());
        if (error_style.value == ErrorStyle::none || n == 0UZ || (!x.hasErrors() && !y.hasErrors())) {
            return;
        }
        const auto xs = SampleWindow::doubles(x.y, n);
        const auto ys = SampleWindow::doubles(y.y, n);
        if (x.hasErrors()) {
            axis::plotErrorBars(std::format("##{}_x_error_bars", uniqueName).c_str(), xs, ys, SampleWindow::doubles(x.yError, n), colour, lineWidth, ImPlotErrorBarsFlags_Horizontal);
        }
        if (y.hasErrors()) {
            axis::plotErrorBars(std::format("##{}_y_error_bars", uniqueName).c_str(), xs, ys, SampleWindow::doubles(y.yError, n), colour, lineWidth);
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
        if (sinkPtr->sampleCount() == 0) {
            drawEmptyPlot("No data", plotFlags, size, chartMode);
            return;
        }

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        setupSignalAxis(true, 0UZ, AxisCategory{.quantity = "Time", .unit = "s"}, 1UZ, size, showGrid);
        setupSignalAxis(false, 0UZ, AxisCategory{.quantity = std::string(sinkPtr->signalQuantity()), .unit = std::string(sinkPtr->signalUnit()), .color = sinkPtr->color()}, 1UZ, size, showGrid);
        setupFinish();

        std::size_t totalCount = sinkPtr->sampleCount();
        std::size_t dataCount  = totalCount;
        std::size_t offset     = 0;
        if (dataCount > static_cast<std::size_t>(n_history.value)) {
            dataCount = static_cast<std::size_t>(n_history.value);
            offset    = totalCount - dataCount;
        }

        struct XYContext {
            SampleWindow window;
            std::size_t  offset;
        };
        XYContext   ctx{sinkPtr->samples(), offset};
        std::string signalLabel = plotLabel(*sinkPtr);
        if (error_style.value != ErrorStyle::none && ctx.window.hasErrors()) { // a time plot of one signal: as XYChart draws it
            const SampleWindow shown = ctx.window.newest(dataCount);
            const auto         band  = axis::errorBand(shown.x, SampleWindow::doubles(shown.y, shown.size()), SampleWindow::doubles(shown.yError, shown.size()), axis::plotColumns());
            axis::drawErrorBand(band, error_style.value, sinkColor(sinkPtr->color()), seriesLineWidth(*sinkPtr), sinkPtr->uniqueName());
        }

        plotSeries(
            signalLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       c         = static_cast<XYContext*>(user_data);
                std::size_t actualIdx = c->offset + static_cast<std::size_t>(idx);
                return ImPlotPoint(c->window.x[actualIdx], c->window.yAt(actualIdx));
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

        std::size_t totalCount = std::min(sinkXPtr->sampleCount(), sinkYPtr->sampleCount());
        if (totalCount == 0) {
            drawEmptyPlot("No data", plotFlags, size, chartMode);
            return;
        }

        std::size_t count = std::min(totalCount, static_cast<std::size_t>(n_history.value));

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, size, plotFlags)) {
            return;
        }

        setupSignalAxis(true, 0UZ, signalCategory(*sinkXPtr), 1UZ, size, showGrid);
        setupSignalAxis(false, 0UZ, signalCategory(*sinkYPtr), 1UZ, size, showGrid);
        setupFinish();

        // the X signal's legend entry, for drag and drop
        {
            ImVec4 xColor = sinkColor(sinkXPtr->color());
            ImPlot::SetNextLineStyle(xColor);
            double      dummyX       = sinkXPtr->samples().newest(count).yAt(0UZ);
            double      dummyY       = sinkYPtr->samples().newest(count).yAt(0UZ);
            std::string xLegendLabel = plotLabel(*sinkXPtr);
            ImPlot::PlotLine(xLegendLabel.c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        Correlation ctx{sinkXPtr->samples().newest(count), sinkYPtr->samples().newest(count)};
        std::string yLegendLabel = plotLabel(*sinkYPtr);
        drawCorrelationErrors(ctx.x, ctx.y, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr), sinkYPtr->uniqueName());
        plotSeries(yLegendLabel.c_str(), &Correlation::point, &ctx, count, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr));

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
            if (sinkXPtr->sampleCount() == 0) {
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
        setupSignalAxis(true, 0UZ, signalCategory(*sinkXPtr), 1UZ, size, showGrid);
        for (std::size_t i = 0; i < yCategories.size(); ++i) {
            if (yCategories[i].has_value()) {
                setupSignalAxis(false, i, yCategories[i], nYAxes, size, showGrid);
            }
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
            double      dummyX       = sinkXPtr->samples().yAt(0UZ);
            double      dummyY       = dummyX;
            std::string xLegendLabel = plotLabel(*sinkXPtr);
            ImPlot::PlotLine(xLegendLabel.c_str(), &dummyX, &dummyY, 1, ImPlotLineFlags_NoClip);
        }

        for (std::size_t i = 1; i < _signalSinks.size(); ++i) {
            if (std::ranges::contains(overflowSinkIndices, i)) {
                continue;
            }

            const auto& sinkYPtr = _signalSinks[i];
            if (!sinkYPtr || !sinkYPtr->drawEnabled()) {
                continue;
            }

            const std::vector<DataGuard> guards = lockAll(std::array{sinkXPtr.get(), sinkYPtr.get()});

            std::size_t totalCount = std::min(sinkXPtr->sampleCount(), sinkYPtr->sampleCount());
            if (totalCount == 0) {
                continue;
            }

            std::size_t count = std::min(totalCount, static_cast<std::size_t>(n_history.value));

            std::size_t yAxisIdx = axis::findAxisForSink(sinkYPtr->uniqueName(), false, std::array<std::vector<std::string>, 3>{}, yAxisGroups);
            ImPlot::SetAxes(ImAxis_X1, static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            Correlation ctx{sinkXPtr->samples().newest(count), sinkYPtr->samples().newest(count)};
            std::string yLabel = plotLabel(*sinkYPtr);
            drawCorrelationErrors(ctx.x, ctx.y, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr), sinkYPtr->uniqueName());
            plotSeries(yLabel.c_str(), &Correlation::point, &ctx, count, sinkColor(sinkYPtr->color()), seriesLineWidth(*sinkYPtr));
        }

        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
    }
};

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_YYCHART_HPP
