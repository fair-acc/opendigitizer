#ifndef OPENDIGITIZER_CHARTS_XYCHART_HPP
#define OPENDIGITIZER_CHARTS_XYCHART_HPP

#include "Chart.hpp"
#include "GpuLineRenderer.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include "../utils/TransparentStringHash.hpp"
#include <imgui.h>
#include <implot.h>

namespace gr::od::charts {

struct DataSetPlotContext {
    const gr::DataSet<float>* dataSet;
    std::size_t               row     = 0UZ;
    double                    yOffset = 0.0;
};

struct HistoryTrace {
    float  opacity = 1.0f;
    double yOffset = 0.0;
};

[[nodiscard]] inline HistoryTrace historyTrace(std::size_t age, float opacityDecay, float verticalOffset) noexcept { //
    return {.opacity = std::pow(1.0f - std::clamp(opacityDecay, 0.0f, 1.0f), static_cast<float>(age)), .yOffset = static_cast<double>(age) * static_cast<double>(verticalOffset)};
}

struct XYChart : gr::Block<XYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"Y values over a common X axis: signals grouped onto several axes by quantity and unit, with a UTC, relative-time or sample-index X axis, tags and a fading DataSet history.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName    = "XYChart";
    static constexpr SignalKind       kSupportedSignals = SignalKind::Streaming | SignalKind::Dataset1D;

    struct StreamSnapshot {
        std::vector<double> x; // window X, pre-transformed for the active axis scale (double: absolute timestamps lose precision as float)
        std::vector<double> y;
        std::size_t         sourceCount = 0UZ; // the samples the window was built from, before reduceToColumns
        double              lastRefresh = 0.0;
        axis::ErrorBand     errors; // in the window's transformed x; empty without uncertainties
    };

    A<std::string, "chart name", gr::Visible>                                                                                                                                      chart_name;
    A<std::string, "chart title">                                                                                                                                                  chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                                                                                                                         data_sinks              = {};
    A<int, "X-axis mode", gr::Visible>                                                                                                                                             x_axis_mode             = static_cast<int>(XAxisMode::RelativeTime);
    A<bool, "show legend", gr::Visible>                                                                                                                                            show_legend             = false;
    A<bool, "show tags", gr::Visible>                                                                                                                                              show_tags               = true;
    A<bool, "tags of every signal", gr::Doc<"each signal's tags in its colour; false: the first signal's only">>                                                                   tags_of_every_signal    = false;
    A<bool, "show grid", gr::Visible>                                                                                                                                              show_grid               = true;
    A<bool, "anti-aliasing">                                                                                                                                                       anti_aliasing           = true;
    A<gr::Size_t, "max history count", gr::Limits<1U, 128U>>                                                                                                                       max_history_count       = 3U;
    A<float, "history opacity decay", gr::Limits<0.001f, 1.f>>                                                                                                                     history_opacity_decay   = 0.3f;
    A<float, "history vertical offset", gr::Limits<0.f, 100.f>>                                                                                                                    history_vertical_offset = 0.0f;
    A<std::array<bool, 3UZ>, "X auto-scale">                                                                                                                                       x_auto_scale            = std::array{true, true, true};
    A<std::array<bool, 3UZ>, "Y auto-scale">                                                                                                                                       y_auto_scale            = std::array{true, true, true};
    A<std::array<double, 3UZ>, "X-axis min">                                                                                                                                       x_min                   = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "X-axis max">                                                                                                                                       x_max                   = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis min">                                                                                                                                       y_min                   = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis max">                                                                                                                                       y_max                   = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<gr::Size_t, "history depth", gr::Unit<"samples">, gr::Limits<4U, 2'000'000U>>                                                                                                n_history               = kDefaultHistorySize;
    A<float, "history seconds", gr::Unit<"s">, gr::Doc<"history depth in seconds at the first signal's sample rate, which then sets n_history; 0: n_history as given">>            history_seconds         = 0.0f;
    A<float, "update rate", gr::Unit<"Hz">, gr::Limits<0.f, 240.f>, gr::Doc<"streaming-trace redraw cadence; decouples the display from the mouse/redraw rate (0 = every frame)">> update_rate             = 25.0f;
    A<bool, "GPU acceleration", gr::Doc<"streaming traces drawn on the GPU, uploading only new samples; false: on the CPU">>                                                       gpu_acceleration        = true;
    ErrorStyleSetting                                                                                                                                                              error_style             = ErrorStyle::combo;

    GR_MAKE_REFLECTABLE(XYChart, chart_name, chart_title, data_sinks, x_axis_mode, show_legend, show_tags, tags_of_every_signal, show_grid, anti_aliasing, max_history_count, history_opacity_decay, history_vertical_offset, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, n_history, history_seconds, update_rate, gpu_acceleration, error_style);

    std::array<std::optional<AxisCategory>, 3UZ> _xCategories{};
    std::array<std::optional<AxisCategory>, 3UZ> _yCategories{};
    std::array<std::vector<std::string>, 3UZ>    _xAxisGroups{};
    std::array<std::vector<std::string>, 3UZ>    _yAxisGroups{};
    mutable std::array<std::string, 6UZ>         _unitStringStorage{};
    std::array<double, 6UZ>                      _symLogLinearThresholds{}; // per ImAxis: the SymLog transform reads it while the plot draws

    std::unordered_map<std::string, StreamSnapshot>                                          _streamSnapshots;
    GpuLineProgram                                                                           _gpuLineProgram;
    std::unordered_map<std::string, GpuLineRenderer, TransparentStringHash, std::equal_to<>> _gpuLines;

    explicit XYChart(gr::property_map initParameters = {}) : gr::Block<XYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>(std::move(initParameters)) {}

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { applyChartSettings(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }
        followHistorySeconds();

        buildAxisCategoriesWithFallback();

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            return gr::work::Status::OK;
        }

        setupAxes(plotSize, showGrid);
        setupFinish();
        drawSignals();
        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();

        return gr::work::Status::OK;
    }

    // a history stated in seconds holds that long at whatever rate the device delivers, not at the rate it was written for
    void followHistorySeconds() {
        const float rate = _signalSinks.empty() ? 0.0f : _signalSinks.front()->sampleRate();
        if (history_seconds.value <= 0.0f || rate <= 0.0f) {
            return;
        }
        const auto samples = static_cast<gr::Size_t>(std::clamp(std::ceil(static_cast<double>(history_seconds.value) * static_cast<double>(rate)), 4.0, 2'000'000.0));
        if (samples != n_history.value) {
            n_history = samples;
            for (const auto& sink : _signalSinks) { // asked again, now for the new depth
                addSignalSink(sink);
            }
        }
    }

    void buildAxisCategoriesWithFallback() {
        axis::buildAxisCategories(_signalSinks, _xCategories, _yCategories, _xAxisGroups, _yAxisGroups);

        const auto fallback = [this](std::optional<AxisCategory>& category, std::vector<std::string>& group, const char* quantity, const char* unit) {
            if (!_signalSinks.empty() && !category.has_value()) {
                category = AxisCategory{.quantity = quantity, .unit = unit, .color = _signalSinks[0]->color()};
                std::ranges::transform(_signalSinks, std::back_inserter(group), [](const auto& sink) { return std::string(sink->uniqueName()); });
            }
        };
        fallback(_xCategories[0], _xAxisGroups[0], "time", "s");
        fallback(_yCategories[0], _yAxisGroups[0], "signal", "");
    }

    void setupAxes(const ImVec2& plotSize, bool showGrid = true) {
        const auto setup = [&](bool isX, auto& categories, const auto& autoScale, const auto& min, const auto& max) {
            const std::size_t nAxes = axis::activeAxisCount(categories);
            for (std::size_t i = 0; i < categories.size(); ++i) {
                const auto dashCfg              = parseAxisConfig(this->ui_constraints.value, isX, i);
                const auto [minLimit, maxLimit] = axisLimits(autoScale.value[i], min.value[i], max.value[i], dashCfg);
                const auto      axisId          = (isX ? ImAxis_X1 : ImAxis_Y1) + static_cast<int>(i);
                const AxisScale scale           = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
                if (categories[i].has_value()) {
                    categories[i]->scale = scale;
                }
                const auto cond      = trackLimitsCond(isX, minLimit, maxLimit, i);
                double&    threshold = _symLogLinearThresholds[static_cast<std::size_t>(axisId)];
                threshold            = dashCfg ? static_cast<double>(dashCfg->linearThreshold) : std::numeric_limits<double>::quiet_NaN();
                axis::setupAxis(axisId, categories[i], dashCfg ? dashCfg->format : LabelFormat::Auto, axisLabelWidthOrDefault(dashCfg, defaultAxisLabelWidthFor(axisId, plotSize)), minLimit, maxLimit, nAxes, scale, _unitStringStorage, showGrid, /*foreground=*/false, cond, &threshold);
                if (ImPlotAxis& plotAxis = ImPlot::GetCurrentPlot()->Axes[axisId]; !isX && ImHasFlag(plotAxis.Flags, ImPlotAxisFlags_AutoFit)) { // Y fits only what X shows
                    plotAxis.Flags |= ImPlotAxisFlags_RangeFit;
                }
            }
        };
        setup(true, _xCategories, x_auto_scale, x_min, x_max);
        setup(false, _yCategories, y_auto_scale, y_min, y_max);
    }

    void drawSignals() {
        bool tagsDrawnForFirstSink = false;

        for (const auto& sink : enabledSinks()) {
            std::string_view sinkUniqueName = sink->uniqueName();
            std::size_t      xAxisIdx       = axis::findAxisForSink(sinkUniqueName, true, _xAxisGroups, _yAxisGroups);
            std::size_t      yAxisIdx       = axis::findAxisForSink(sinkUniqueName, false, _xAxisGroups, _yAxisGroups);

            ImPlot::SetAxes(static_cast<ImAxis>(ImAxis_X1 + xAxisIdx), static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            auto dataLock = sink->dataGuard();

            if (sink->hasDataSets() || sink->sampleCount() > 0) {
                ImVec4     baseColor = sinkColor(sink->color());
                const bool withTags  = show_tags.value && (tags_of_every_signal.value || !tagsDrawnForFirstSink);

                if (sink->hasDataSets()) {
                    drawDataSetSignal(*sink);

                    if (withTags) {
                        auto allDataSets = sink->dataSets();
                        if (!allDataSets.empty()) {
                            tags::drawDataSetTimingEvents(allDataSets.back(), baseColor);
                        }
                        tagsDrawnForFirstSink = true;
                    }
                } else {
                    drawStreamingSignal(*sink);

                    if (withTags) {
                        const SampleWindow window     = sink->samples().newest(static_cast<std::size_t>(n_history.value));
                        const double       xMin       = window.x.front();
                        const double       xMax       = window.x.back();
                        AxisScale          xAxisScale = _xCategories[xAxisIdx].has_value() ? _xCategories[xAxisIdx]->scale : AxisScale::Linear;
                        ImVec4             tagColor   = baseColor;
                        tagColor.w *= LookAndFeel::instance().chartStyle.tagAlpha;
                        tags::drawTags([&](auto&& fn) { sink->forEachTag(fn); }, xAxisScale, xMin, xMax, tagColor);
                        tagsDrawnForFirstSink = true;
                    }
                }
            }
        }
        limitXFitToHistory();
        std::erase_if(_gpuLines, [frame = ImGui::GetFrameCount()](const auto& entry) { return entry.second.lastDrawnFrame != frame; });
    }

    void limitXFitToHistory() {
        if (!_xCategories[0].has_value() || _xCategories[0]->scale != AxisScale::Time) {
            return;
        }
        double newest  = -std::numeric_limits<double>::infinity();
        double history = 0.0;
        for (const auto& sink : enabledSinks()) {
            auto dataLock = sink->dataGuard();
            if (!sink->hasDataSets() && sink->sampleRate() > 0.0f) {
                newest  = std::max(newest, sink->timeLast());
                history = std::max(history, static_cast<double>(n_history.value) / static_cast<double>(sink->sampleRate()));
            }
        }
        for (ImPlotAxis& xAxis : std::span(ImPlot::GetCurrentPlot()->Axes).first(IMPLOT_NUM_X_AXES)) {
            xAxis.FitExtents.Min = std::max(xAxis.FitExtents.Min, newest - history);
        }
    }

    void drawStreamingSignal(const SignalSink& sink) {
        std::size_t totalCount = sink.sampleCount();
        if (totalCount == 0) {
            return;
        }

        std::size_t dataCount = std::min(totalCount, static_cast<std::size_t>(n_history.value));
        std::size_t offset    = totalCount - dataCount;

        const AxisScale xAxisScale = _xCategories[0].has_value() ? _xCategories[0]->scale : AxisScale::Linear;

        // the displayed window is rebuilt at most update_rate times/s instead of from the live ring buffer at each redraw,
        // so the trace advances at a steady cadence rather than with the mouse or redraw rate
        StreamSnapshot& snap            = _streamSnapshots[std::string(sink.uniqueName())];
        const double    now             = ImGui::GetTime();
        const double    refreshInterval = update_rate.value > 0.f ? 1.0 / static_cast<double>(update_rate.value) : 0.0;
        const bool      refreshDue      = snap.sourceCount != dataCount || (now - snap.lastRefresh) >= refreshInterval;
        const auto      columns         = axis::plotColumns();
        const bool      drawErrors      = error_style.value != ErrorStyle::none && sink.hasErrors();
        if (drawErrors && (refreshDue || snap.errors.x.empty())) {
            const SampleWindow window = sink.samples().newest(dataCount);
            const double       shift  = xOffset(xAxisScale, window.x.front(), window.x.back());
            const auto         x      = window.x | std::views::transform([shift](double t) { return t - shift; }) | std::ranges::to<std::vector>();
            snap.errors               = axis::errorBand(x, SampleWindow::doubles(window.y, window.size()), SampleWindow::doubles(window.yError, window.size()), columns);
        }
        if (drawErrors) { // under the line, whichever path draws it
            axis::drawErrorBand(snap.errors, error_style.value, sinkColor(sink.color()), seriesLineWidth(sink), sink.uniqueName());
        }
        if (gpu_acceleration.value && _gpuLineProgram.available()) {
            auto lineIt = _gpuLines.find(sink.uniqueName());
            if (lineIt == _gpuLines.end()) {
                lineIt = _gpuLines.try_emplace(std::string(sink.uniqueName())).first;
            }
            GpuLineRenderer& line = lineIt->second;
            if (refreshDue || line.lastDrawnFrame < 0) {
                line.refresh(sink, static_cast<std::size_t>(n_history.value), xAxisScale);
                snap.x.clear();
                snap.sourceCount = dataCount;
                snap.lastRefresh = now;
            }
            line.lastDrawnFrame = ImGui::GetFrameCount();
            if (!line.outOfMemory) {
                line.draw(_gpuLineProgram, sink, plotLabel(sink).c_str(), sinkColor(sink.color()), seriesLineWidth(sink), anti_aliasing.value);
                return;
            }
        }
        if (refreshDue || snap.x.empty()) {
            const SampleWindow window = sink.samples();
            const double       shift  = xOffset(xAxisScale, window.x[offset], window.x[offset + dataCount - 1]);
            snap.x.resize(dataCount);
            snap.y.resize(dataCount);
            for (std::size_t i = 0; i < dataCount; ++i) {
                snap.x[i] = window.x[offset + i] - shift;
                snap.y[i] = window.yAt(offset + i);
            }
            axis::reduceToColumns(snap.x, snap.y, columns);
            snap.sourceCount = dataCount;
            snap.lastRefresh = now;
        }

        ImPlot::SetNextLineStyle(sinkColor(sink.color()), seriesLineWidth(sink));
        ImPlot::PlotLine(plotLabel(sink).c_str(), snap.x.data(), snap.y.data(), static_cast<int>(snap.x.size()));
    }

    void drawDataSetSignal(const SignalSink& sink) {
        // DataSet x values are absolute (e.g. frequency): no transformation
        auto allDataSets = sink.dataSets();
        if (allDataSets.empty()) {
            return;
        }

        ImVec4      baseColor   = sinkColor(sink.color());
        std::size_t historySize = std::min(allDataSets.size(), static_cast<std::size_t>(max_history_count.value));
        std::string baseLabel   = plotLabel(sink);

        // oldest first, so the newest draws on top
        const std::size_t spanSize = allDataSets.size();
        const std::size_t offset   = spanSize - historySize;
        for (std::size_t i = 0; i < historySize; ++i) {
            const auto& ds = allDataSets[offset + i];
            if (ds.axis_values.empty() || ds.axis_values[0].empty() || ds.extents.size() > 1UZ) {
                continue; // a rank-2 DataSet is an image: ImageChart shows it
            }
            const std::size_t  age   = historySize - 1UZ - i;
            const HistoryTrace trace = historyTrace(age, history_opacity_decay, history_vertical_offset);
            DataSetPlotContext ctx{.dataSet = &ds, .row = sink.datasetRow(ds), .yOffset = trace.yOffset};

            ImVec4 lineColor = baseColor;
            lineColor.w      = trace.opacity;
            ImPlot::SetNextLineStyle(lineColor, seriesLineWidth(sink));

            std::string label     = age == 0UZ ? baseLabel : std::format("##{}_hist_{}", sink.uniqueName(), i);
            int         dataCount = static_cast<int>(ds.axis_values[0].size());
            if (age == 0UZ && error_style.value != ErrorStyle::none && sink.hasErrors()) {
                drawDataSetErrors(sink, ds, sink.dataSetErrors()[offset + i], baseColor);
            }

            const ImPlotGetter dataSetPoint = [](int idx, void* user_data) -> ImPlotPoint {
                auto*       context = static_cast<DataSetPlotContext*>(user_data);
                const auto& dataSet = *context->dataSet;
                double      xVal    = static_cast<double>(dataSet.axis_values[0][static_cast<std::size_t>(idx)]);
                double      yVal    = context->yOffset;
                auto        sigVals = dataSet.signalValues(context->row);
                if (static_cast<std::size_t>(idx) < sigVals.size()) {
                    yVal += static_cast<double>(sigVals[static_cast<std::size_t>(idx)]);
                }
                return ImPlotPoint(xVal, yVal);
            };
            if (dataCount < 2) {
                ImPlot::PlotScatterG(label.c_str(), dataSetPoint, &ctx, dataCount);
            } else {
                ImPlot::PlotLineG(label.c_str(), dataSetPoint, &ctx, dataCount);
            }
        }
    }

    void drawDataSetErrors(const SignalSink& sink, const gr::DataSet<float>& dataSet, const DataSetErrors& errors, ImVec4 colour) const {
        const std::size_t row    = sink.datasetRow(dataSet);
        const std::size_t n      = dataSet.axis_values[0].size();
        const auto        values = dataSet.signalValues(row);
        if (values.size() < n || errors.signals.size() < (row + 1UZ) * n) {
            return;
        }
        const auto x     = SampleWindow::doubles(std::span(dataSet.axis_values[0]), n);
        const auto y     = SampleWindow::doubles(values, n);
        const auto sigma = SampleWindow::doubles(std::span(errors.signals).subspan(row * n, n), n);
        const auto band  = axis::errorBand(x, y, sigma, axis::plotColumns());
        axis::drawErrorBand(band, error_style.value, colour, seriesLineWidth(sink), sink.uniqueName());
        if (axis::errorDrawing(band, error_style.value).bars && band.perPoint && !errors.axes.empty() && errors.axes[0].size() == n) {
            axis::plotErrorBars(std::format("##{}_x_error_bars", sink.uniqueName()).c_str(), x, y, SampleWindow::doubles(std::span(errors.axes[0]), n), colour, seriesLineWidth(sink), ImPlotErrorBarsFlags_Horizontal);
        }
    }
};

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_XYCHART_HPP
