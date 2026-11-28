#ifndef OPENDIGITIZER_CHARTS_SPECTRUMDENSITY_HPP
#define OPENDIGITIZER_CHARTS_SPECTRUMDENSITY_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <limits>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace gr::od::charts {

struct SpectrumDensity : gr::Block<SpectrumDensity, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"2D persistence display showing spectrum amplitude density over time.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName    = "SpectrumDensity";
    static constexpr SignalKind       kSupportedSignals = SignalKind::Dataset1D;

    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks  = {};
    A<bool, "show legend", gr::Visible>                    show_legend = false;
    A<bool, "show grid", gr::Visible>                      show_grid   = true;

    A<gr::Size_t, "amplitude bins", gr::Visible, gr::Limits<4U, 1024U>>                       amplitude_bins             = 256U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                               colormap                   = ImPlotColormap_Viridis;
    A<gr::Size_t, "histogram decay tau", gr::Doc<"histogram decay in frames (0 = infinite)">> histogram_decay_tau_frames = 100U;

    A<bool, "show current overlay", gr::Visible>                                                                        show_current_overlay   = true;
    A<bool, "show max hold", gr::Visible>                                                                               show_max_hold          = false;
    A<bool, "show min hold", gr::Visible>                                                                               show_min_hold          = false;
    A<bool, "show average", gr::Visible>                                                                                show_average           = false;
    A<std::uint32_t, "trace color", gr::Visible, gr::Unit<"Color">, gr::Doc<"RGB colour for overlay lines (0xRRGGBB)">> trace_color            = 0xFF8C00U;
    A<gr::Size_t, "trace decay tau", gr::Doc<"trace decay in frames (0 = infinite hold)">>                              trace_decay_tau_frames = 25U;

    A<DensityNormalisation, "density normalisation", gr::Visible, gr::Doc<"peak: a steady signal at full scale (p ← p (1 − 1/τ) + 1, → τ); maximum: the densest cell at full scale">> density_normalisation = DensityNormalisation::peak;
    A<bool, "adaptive Y range", gr::Visible, gr::Doc<"rebin histogram to visible Y-axis range on zoom">>                                                                              adaptive_y_range      = true;

    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = false;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = -120.0;
    A<double, "Y-axis max"> y_max        = 0.0;

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

    GR_MAKE_REFLECTABLE(SpectrumDensity, chart_name, chart_title, data_sinks, show_legend, show_grid, amplitude_bins, colormap, histogram_decay_tau_frames, density_normalisation, adaptive_y_range, show_current_overlay, show_max_hold, show_min_hold, show_average, trace_color, trace_decay_tau_frames, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, range_style, range_min, range_max, range_min_width, range_color, range_min_targets, range_max_targets, f0_style, f0, f0_color, f0_targets, follow_inputs, snap_to_bins, show_range_labels, show_f0_labels);

    DensityHistogram             _density;
    TraceAccumulator             _traces;
    std::size_t                  _lastSampleCount = std::numeric_limits<std::size_t>::max();
    std::array<std::string, 6UZ> _unitStore{};

    void reset() { requestRenderReset(); }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { applyChartSettings(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));

        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            ImPlot::PopStyleColor();
            return gr::work::Status::OK;
        }

        setupAxes(plotSize, showGrid);
        setupFinish();

        if (y_auto_scale.value && !_limitsForceAppliedY[0]) {
            auto         plotLimits = ImPlot::GetPlotLimits();
            const double range      = std::abs(_prevYMax[0] - _prevYMin[0]);
            const double tol        = std::max(range * 1e-3, 1e-10);
            if (std::abs(plotLimits.Y.Min - _prevYMin[0]) > tol || std::abs(plotLimits.Y.Max - _prevYMax[0]) > tol) {
                y_auto_scale = false;
                y_min        = plotLimits.Y.Min;
                y_max        = plotLimits.Y.Max;
                _prevYMin[0] = plotLimits.Y.Min;
                _prevYMax[0] = plotLimits.Y.Max;
            }
        }

        plotLegendEntries();

        drawDensitySignals();
        drawFrequencyMarkers(0UZ);
        tooltip::showPlotMouseTooltip();
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
        ImPlot::PopStyleColor();

        return gr::work::Status::OK;
    }

    void resetRenderState() {
        _density.reset();
        _traces.reset();
        _lastSampleCount = std::numeric_limits<std::size_t>::max();
    }

    void enableAxisAutoFit(AxisKind axis, std::size_t axisIndex = 0UZ) {
        if (axis == AxisKind::Y) {
            writeAutoScale(y_auto_scale, axisIndex, true);
            return;
        }
        Chart::enableAxisAutoFit(axis, axisIndex);
    }

    // always finite: the histogram bins over it
    [[nodiscard]] std::array<double, 2> effectiveYRange() const { return axisLimits(false, y_min.value, y_max.value, parseAxisConfig(this->ui_constraints.value, false), ConstrainedWhen::always); }

    void setupAxes(const ImVec2& plotSize, bool showGrid) {
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, true);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::MetricInline;

            const auto [minLimit, maxLimit] = axisLimits(x_auto_scale.value, x_min.value, x_max.value, dashCfg, ConstrainedWhen::always);

            auto [xQuantity, xUnit] = sinkAxisInfo(true);
            AxisCategory xCat{.quantity = xQuantity, .unit = xUnit};
            axis::setupAxis(ImAxis_X1, xCat, format, axisLabelWidthOrDefault(dashCfg, defaultAxisLabelWidthFor(ImAxis_X1, plotSize)), minLimit, maxLimit, 1UZ, scale, _unitStore, showGrid, /*foreground=*/true);
        }

        // always finite limits: the heatmap gives ImPlot's AutoFit no plottable data.
        // Forced only when the desired range changes, else ImPlotCond_Once keeps the user's zoom and pan.
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, false);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::Auto;

            auto [yQuantity, yUnit]         = sinkAxisInfo(false);
            const auto [minLimit, maxLimit] = y_auto_scale.value ? effectiveYRange() : std::array{y_min.value, y_max.value};

            const auto limitCond = trackLimitsCond(false, minLimit, maxLimit);

            AxisCategory yCat{.quantity = yQuantity, .unit = yUnit};
            axis::setupAxis(ImAxis_Y1, yCat, format, axisLabelWidthOrDefault(dashCfg, defaultAxisLabelWidthFor(ImAxis_Y1, plotSize)), minLimit, maxLimit, 1UZ, scale, _unitStore, showGrid, /*foreground=*/true, limitCond);
        }
    }

    void drawDensitySignals() {
        const auto logRange = logFreqRange(parseAxisConfig(this->ui_constraints.value, AxisKind::X));
        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
            const auto ampBins      = static_cast<std::size_t>(amplitude_bins);
            auto [effYMin, effYMax] = effectiveYRange();

            if (adaptive_y_range.value) {
                auto limits = ImPlot::GetPlotLimits();
                effYMin     = std::max(limits.Y.Min, effYMin);
                effYMax     = std::min(limits.Y.Max, effYMax);
            }

            const std::size_t nNew = consumeNewData(_lastSampleCount, sink.totalSampleCount());
            _density.accumulateAndPlot(f, nNew, logRange, ampBins, static_cast<double>(histogram_decay_tau_frames), effYMin, effYMax, colormap.value, density_normalisation);

            ImVec4 traceBase = sinkColor(trace_color);
            drawTraceOverlays(_traces, f, nNew, static_cast<double>(trace_decay_tau_frames), traceBase, show_max_hold, show_min_hold, show_average);

            if (show_current_overlay.value && sink.drawEnabled()) {
                plotTrace("##current", f.xValues, f.yValues, f.nBins, ImVec4(traceBase.x, traceBase.y, traceBase.z, 1.0f));
            }

            return false; // density display uses first valid sink only
        });
    }
};

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_SPECTRUMDENSITY_HPP
