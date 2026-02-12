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

namespace opendigitizer::charts {

struct SpectrumDensity : gr::Block<SpectrumDensity, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"2D persistence display showing spectrum amplitude density over time.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // identity
    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks  = {};
    A<bool, "show legend", gr::Visible>                    show_legend = false;
    A<bool, "show grid", gr::Visible>                      show_grid   = true;

    // density histogram
    A<gr::Size_t, "amplitude bins", gr::Visible, gr::Limits<4U, 1024U>>                       amplitude_bins             = 256U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                               colormap                   = ImPlotColormap_Viridis;
    A<gr::Size_t, "histogram decay tau", gr::Doc<"histogram decay in frames (0 = infinite)">> histogram_decay_tau_frames = 100U;

    // trace overlays
    A<bool, "show current overlay", gr::Visible>                                                     show_current_overlay   = true;
    A<bool, "show max hold", gr::Visible>                                                            show_max_hold          = false;
    A<bool, "show min hold", gr::Visible>                                                            show_min_hold          = false;
    A<bool, "show average", gr::Visible>                                                             show_average           = false;
    A<std::uint32_t, "trace color", gr::Visible, gr::Doc<"RGB colour for overlay lines (0xRRGGBB)">> trace_color            = 0xFF8C00U;
    A<gr::Size_t, "trace decay tau", gr::Doc<"trace decay in frames (0 = infinite hold)">>           trace_decay_tau_frames = 25U;

    A<bool, "GPU acceleration", gr::Doc<"use GPU shaders for histogram (falls back to CPU if unavailable)">> gpu_acceleration = true;
    A<bool, "adaptive Y range", gr::Visible, gr::Doc<"rebin histogram to visible Y-axis range on zoom">>     adaptive_y_range = true;

    // axis limits
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = false;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = -120.0;
    A<double, "Y-axis max"> y_max        = 0.0;

    GR_MAKE_REFLECTABLE(SpectrumDensity, chart_name, chart_title, data_sinks, show_legend, show_grid, amplitude_bins, colormap, histogram_decay_tau_frames, gpu_acceleration, adaptive_y_range, show_current_overlay, show_max_hold, show_min_hold, show_average, trace_color, trace_decay_tau_frames, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    DensityHistogram _density;
    TraceAccumulator _traces;
    double           _lastSetYMin         = std::numeric_limits<double>::quiet_NaN();
    double           _lastSetYMax         = std::numeric_limits<double>::quiet_NaN();
    bool             _yLimitsForceApplied = false;

    static constexpr std::string_view kChartTypeName = "SpectrumDensity";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            ImPlot::PopStyleColor();
            return gr::work::Status::OK;
        }

        setupAxes(showGrid);
        ImPlot::SetupFinish();

        // detect user zoom on Y-axis: if auto-fit is active and the actual plot limits
        // differ from what we programmatically set, the user zoomed → disable auto-fit
        if (y_auto_scale.value && !_yLimitsForceApplied) {
            auto         plotLimits = ImPlot::GetPlotLimits();
            const double range      = std::abs(_lastSetYMax - _lastSetYMin);
            const double tol        = std::max(range * 1e-3, 1e-10);
            if (std::abs(plotLimits.Y.Min - _lastSetYMin) > tol || std::abs(plotLimits.Y.Max - _lastSetYMax) > tol) {
                y_auto_scale = false;
                y_min        = plotLimits.Y.Min;
                y_max        = plotLimits.Y.Max;
                _lastSetYMin = plotLimits.Y.Min;
                _lastSetYMax = plotLimits.Y.Max;
            }
        }

        // register legend entries for each sink (enables legend display and D&D)
        for (const auto& sink : _signalSinks) {
            ImVec4 color = sinkColor(sink->color());
            ImPlot::SetNextLineStyle(color);
            ImPlot::PlotDummy(std::string(sink->signalName()).c_str());
        }

        drawDensitySignals();
        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        ImPlot::PopStyleColor();

        return gr::work::Status::OK;
    }

    void reset() {
        _density.reset();
        _traces.reset();
    }

    // effective Y-range considering dashboard config override (always finite, needed for histogram binning)
    [[nodiscard]] std::pair<double, double> effectiveYRange() const {
        double yMin = y_min.value;
        double yMax = y_max.value;
        if (auto dashCfg = parseAxisConfig(this->ui_constraints.value, false)) {
            if (std::isfinite(dashCfg->min)) {
                yMin = static_cast<double>(dashCfg->min);
            }
            if (std::isfinite(dashCfg->max)) {
                yMax = static_cast<double>(dashCfg->max);
            }
        }
        return {yMin, yMax};
    }

    void setupAxes(bool showGrid) {
        // x-axis: frequency
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, true);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::MetricInline;

            double minLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_min.value;
            double maxLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_max.value;
            if (dashCfg) {
                if (std::isfinite(dashCfg->min)) {
                    minLimit = static_cast<double>(dashCfg->min);
                }
                if (std::isfinite(dashCfg->max)) {
                    maxLimit = static_cast<double>(dashCfg->max);
                }
            }

            auto [xQuantity, xUnit] = sinkAxisInfo(true);
            AxisCategory               xCat{.quantity = xQuantity, .unit = xUnit};
            std::array<std::string, 6> unitStore{};
            axis::setupAxis(ImAxis_X1, xCat, format, 100.f, minLimit, maxLimit, 1, scale, unitStore, showGrid, /*foreground=*/true);
        }

        // y-axis: amplitude — always use finite limits because the heatmap provides no
        // plottable data for ImPlot's AutoFit. Force-apply only when the desired range
        // changes; otherwise use ImPlotCond_Once so user zoom/pan is preserved.
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, false);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::Auto;

            auto [yMin, yMax]       = effectiveYRange();
            auto [yQuantity, yUnit] = sinkAxisInfo(false);

            double minLimit = y_auto_scale.value ? yMin : y_min.value;
            double maxLimit = y_auto_scale.value ? yMax : y_max.value;

            bool limitsChanged   = (minLimit != _lastSetYMin || maxLimit != _lastSetYMax);
            auto limitCond       = limitsChanged ? ImPlotCond_Always : ImPlotCond_Once;
            _lastSetYMin         = minLimit;
            _lastSetYMax         = maxLimit;
            _yLimitsForceApplied = limitsChanged;

            AxisCategory               yCat{.quantity = yQuantity, .unit = yUnit};
            std::array<std::string, 6> unitStore{};
            axis::setupAxis(ImAxis_Y1, yCat, format, 100.f, minLimit, maxLimit, 1, scale, unitStore, showGrid, /*foreground=*/true, limitCond);
        }
    }

    void drawDensitySignals() {
        forEachValidSpectrum(_signalSinks, [&](const auto& /*sink*/, const SpectrumFrame& f) {
            const auto ampBins      = static_cast<std::size_t>(amplitude_bins);
            auto [effYMin, effYMax] = effectiveYRange();

            if (adaptive_y_range.value) {
                auto limits = ImPlot::GetPlotLimits();
                effYMin     = std::max(limits.Y.Min, effYMin);
                effYMax     = std::min(limits.Y.Max, effYMax);
            }

            _density.update(f.yValues, f.nBins, ampBins, static_cast<double>(histogram_decay_tau_frames), effYMin, effYMax, colormap.value, gpu_acceleration);
            _density.plot(f.xValues, effYMin, effYMax);

            ImVec4 traceBase = sinkColor(trace_color);
            drawTraceOverlays(_traces, f.xValues, f.yValues, f.nBins, static_cast<double>(trace_decay_tau_frames), traceBase, show_max_hold, show_min_hold, show_average);

            if (show_current_overlay.value) {
                plotTrace("##current", f.xValues, f.yValues, f.nBins, ImVec4(traceBase.x, traceBase.y, traceBase.z, 1.0f));
            }

            return false; // density display uses first valid sink only
        });
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::SpectrumDensity", opendigitizer::charts::SpectrumDensity)
inline auto registerSpectrumDensity = gr::registerBlock<opendigitizer::charts::SpectrumDensity>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_SPECTRUMDENSITY_HPP
