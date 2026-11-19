#ifndef OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP
#define OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

enum class WaterfallOrientation : int { Vertical = 0, Horizontal = 1 }; // time on Y (Vertical) or on X with frequency on Y (Horizontal)

struct WaterfallPlot : gr::Block<WaterfallPlot, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"Scrolling spectrogram using a GPU ring-buffer texture with single-row updates.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // identity
    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks  = {};
    A<bool, "show legend", gr::Visible>                    show_legend = false;
    A<bool, "show grid", gr::Visible>                      show_grid   = true;

    // waterfall
    A<gr::Size_t, "history depth", gr::Visible, gr::Limits<16U, 4096U>>                                                                         n_history        = 256U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                                                                                 colormap         = ImPlotColormap_Viridis;
    A<bool, "GPU acceleration", gr::Doc<"use GPU texture for rendering (falls back to CPU if unavailable)">>                                    gpu_acceleration = true;
    A<WaterfallOrientation, "orientation", gr::Visible, gr::Doc<"scroll axis: Vertical (time on Y) or Horizontal (time on X, frequency on Y)">> orientation      = WaterfallOrientation::Vertical;
    // axis limits (Z = colour scale: NaN min/max → auto-scale from data)
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = true;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = std::numeric_limits<double>::lowest();
    A<double, "Y-axis max"> y_max        = std::numeric_limits<double>::max();

    RangeStyleSetting      range_style     = RangeStyle::hidden;
    RangeMinSetting        range_min       = std::numeric_limits<double>::quiet_NaN();
    RangeMaxSetting        range_max       = std::numeric_limits<double>::quiet_NaN();
    RangeMinWidthSetting   range_min_width = 0.0;
    RangeColorSetting      range_color     = 0U;
    RangeMinTargetsSetting range_min_targets;
    RangeMaxTargetsSetting range_max_targets;
    F0StyleSetting         f0_style = MarkerStyle::hidden;
    F0Setting              f0       = std::numeric_limits<double>::quiet_NaN();
    F0ColorSetting         f0_color = 0U;
    F0TargetsSetting       f0_targets;
    FollowInputsSetting    follow_inputs     = true;
    SnapToBinsSetting      snap_to_bins      = true;
    RangeLabelsSetting     show_range_labels = true;
    F0LabelsSetting        show_f0_labels    = true;

    static constexpr SignalKind supportedSignals = SignalKind::Dataset1D;

    GR_MAKE_REFLECTABLE(WaterfallPlot, chart_name, chart_title, data_sinks, show_legend, show_grid, n_history, colormap, gpu_acceleration, orientation, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, range_style, range_min, range_max, range_min_width, range_color, range_min_targets, range_max_targets, f0_style, f0, f0_color, f0_targets, follow_inputs, snap_to_bins, show_range_labels, show_f0_labels);

    WaterfallBuffer              _waterfall;
    std::array<std::string, 6>   _unitStore{};
    std::size_t                  _lastPushedSampleCount = std::numeric_limits<std::size_t>::max();
    std::optional<FrequencySpan> _lastRenderInfo;

    static constexpr std::string_view kChartTypeName = "WaterfallPlot";

    [[nodiscard]] std::optional<AxisScale> getAxisScale(AxisKind axis) const noexcept {
        if (auto cfg = parseAxisConfig(this->ui_constraints.value, axis)) {
            if (cfg->scale) {
                return cfg->scale;
            }
        }
        return (axis == AxisKind::Y) ? AxisScale::LinearReverse : AxisScale::Linear;
    }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);

        // sync GPU preference with setting
        _waterfall.setPreferGpu(gpu_acceleration);

        // sync waterfall depth with n_history after debounced UI changes
        // (UI modifies n_history directly, bypassing settingsChanged)
        if (_pendingResizeTime == 0.0 && _waterfall.width() > 0) {
            _waterfall.resizeHistory(static_cast<std::size_t>(n_history));
        }

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            ImPlot::PopStyleColor();
            return gr::work::Status::OK;
        }

        // phase 1: set up axes (X fully, Y skeleton without limits)
        setupAxes(plotSize, showGrid);

        // phase 2: fetch new data and push into waterfall (skips duplicate frames)
        const auto renderInfo = _waterfall.pushNewSpectra(_signalSinks, _lastPushedSampleCount, logFreqRange(parseAxisConfig(this->ui_constraints.value, AxisKind::X)), static_cast<std::size_t>(n_history), gpu_acceleration, colormap.value, [&] { return effectiveColourRange(this->ui_constraints.value, _waterfall.scaleMin(), _waterfall.scaleMax()); });
        if (renderInfo) {
            _lastRenderInfo = renderInfo;
        }

        // phase 3: compute the time-axis bounds; in horizontal mode time is on X, otherwise on Y
        const bool      horizontal = orientation.value == WaterfallOrientation::Horizontal;
        const ImAxis    timeAxis   = horizontal ? ImAxis_X1 : ImAxis_Y1;
        const AxisScale timeScale  = getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse);
        auto [tOldest, tNewest]    = _waterfall.rawTimeBounds();
        auto [timeLo, timeHi]      = timeAxisBounds(tOldest, tNewest, timeScale);

        if (_waterfall.filledRows() > 0 && timeHi > timeLo) {
            ImPlot::SetupAxisLimits(timeAxis, timeLo, timeHi, ImPlotCond_Always);
        }

        setupFinish();

        plotLegendEntries();

        // phase 4: render waterfall image (always, even when no new data this frame)
        const bool newestLeading = (timeScale == AxisScale::LinearReverse);
        if (renderInfo) {
            _waterfall.render(renderInfo->freqMin, renderInfo->freqMax, timeLo, timeHi, newestLeading, horizontal);
        } else if (_waterfall.filledRows() > 0 && _lastRenderInfo) {
            _waterfall.render(_lastRenderInfo->freqMin, _lastRenderInfo->freqMax, timeLo, timeHi, newestLeading, horizontal);
        }
        drawFrequencyMarkers(0UZ, horizontal ? ImAxis_Y1 : ImAxis_X1);

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        ImPlot::PopStyleColor();

        return gr::work::Status::OK;
    }

    void reset() { requestRenderReset(); }

    void resetRenderState() { _waterfall.clear(); }

    void setupAxes(const ImVec2& plotSize, bool showGrid) {
        // frequency keeps the "X" dashboard entry, time the "Y" entry; orientation only rotates which screen axis each uses
        const bool   horizontal = orientation.value == WaterfallOrientation::Horizontal;
        const ImAxis freqAxis   = horizontal ? ImAxis_Y1 : ImAxis_X1;
        const ImAxis timeAxis   = horizontal ? ImAxis_X1 : ImAxis_Y1;

        // frequency axis
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, true);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::MetricInline;

            const auto [minLimit, maxLimit] = axisLimits(x_auto_scale.value, x_min.value, x_max.value, dashCfg);

            auto [xQuantity, xUnit] = sinkAxisInfo(true);
            AxisCategory xCat{.quantity = xQuantity, .unit = xUnit};
            axis::setupAxis(freqAxis, xCat, format, axisLabelWidthOrDefault(dashCfg, defaultAxisLabelWidthFor(freqAxis, plotSize)), minLimit, maxLimit, 1, scale, _unitStore, showGrid, /*foreground=*/true);
        }

        // limits are set in draw() after the data push, so axis and image stay synchronised
        setupTimeAxis(timeAxis, getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse), showGrid, _unitStore[static_cast<std::size_t>(timeAxis)]);
    }
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP
