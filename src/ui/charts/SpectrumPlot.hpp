#ifndef OPENDIGITIZER_CHARTS_SPECTRUMPLOT_HPP
#define OPENDIGITIZER_CHARTS_SPECTRUMPLOT_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

struct SpectrumPlot : gr::Block<SpectrumPlot, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"Spectrum magnitude plot with optional max-hold, min-hold, and average traces.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks  = {};
    A<bool, "show legend", gr::Visible>                    show_legend = false;
    A<bool, "show grid", gr::Visible>                      show_grid   = true;

    // trace toggles and accumulation
    A<bool, "show max hold", gr::Visible>                                                                       show_max_hold    = true;
    A<bool, "show min hold", gr::Visible>                                                                       show_min_hold    = true;
    A<bool, "show average", gr::Visible>                                                                        show_average     = false;
    A<std::uint32_t, "trace color", gr::Visible, gr::Doc<"RGB colour for accumulated traces (0xRRGGBB)">>       trace_color      = 0x8855BBU;
    A<gr::Size_t, "decay tau frames", gr::Doc<"exponential decay time constant in frames (0 = infinite hold)">> decay_tau_frames = 100U;

    // axis limits
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = true;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = -120.0;
    A<double, "Y-axis max"> y_max        = 0.0;

    GR_MAKE_REFLECTABLE(SpectrumPlot, chart_name, chart_title, data_sinks, show_legend, show_grid, show_max_hold, show_min_hold, show_average, trace_color, decay_tau_frames, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    std::unordered_map<std::string, TraceAccumulator> _tracesPerSink;

    static constexpr std::string_view kChartTypeName = "SpectrumPlot";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            return gr::work::Status::OK;
        }

        setupAxes(showGrid);
        ImPlot::SetupFinish();
        drawSpectrumSignals();
        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();

        return gr::work::Status::OK;
    }

    void reset() { _tracesPerSink.clear(); }

    void setupAxes(bool showGrid) {
        // x-axis: frequency
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, true);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::MetricInline;

            double minLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_min.value;
            double maxLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_max.value;
            if (dashCfg && x_auto_scale.value) {
                minLimit = std::isfinite(dashCfg->min) ? static_cast<double>(dashCfg->min) : minLimit;
                maxLimit = std::isfinite(dashCfg->max) ? static_cast<double>(dashCfg->max) : maxLimit;
            }

            auto [xQuantity, xUnit] = sinkAxisInfo(true);
            AxisCategory               xCat{.quantity = xQuantity, .unit = xUnit};
            std::array<std::string, 6> unitStore{};
            auto                       xCond = trackLimitsCond(true, minLimit, maxLimit);
            axis::setupAxis(ImAxis_X1, xCat, format, 100.f, minLimit, maxLimit, 1, scale, unitStore, showGrid, /*foreground=*/false, xCond);
        }

        // y-axis: magnitude
        {
            const auto      dashCfg = parseAxisConfig(this->ui_constraints.value, false);
            const AxisScale scale   = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;
            const auto      format  = dashCfg ? dashCfg->format : LabelFormat::Auto;

            double minLimit = y_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : y_min.value;
            double maxLimit = y_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : y_max.value;
            if (dashCfg && y_auto_scale.value) {
                minLimit = std::isfinite(dashCfg->min) ? static_cast<double>(dashCfg->min) : minLimit;
                maxLimit = std::isfinite(dashCfg->max) ? static_cast<double>(dashCfg->max) : maxLimit;
            }

            auto [yQuantity, yUnit] = sinkAxisInfo(false);
            AxisCategory               yCat{.quantity = yQuantity, .unit = yUnit};
            std::array<std::string, 6> unitStore{};
            auto                       yCond = trackLimitsCond(false, minLimit, maxLimit);
            axis::setupAxis(ImAxis_Y1, yCat, format, 100.f, minLimit, maxLimit, 1, scale, unitStore, showGrid, /*foreground=*/false, yCond);
        }
    }

    void drawSpectrumSignals() {
        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
            plotTrace(std::string(sink.signalName()).c_str(), f.xValues, f.yValues, f.nBins, sinkColor(sink.color()));
            auto& traces = _tracesPerSink[std::string(sink.signalName())];
            drawTraceOverlays(traces, f.xValues, f.yValues, f.nBins, static_cast<double>(decay_tau_frames), sinkColor(trace_color), show_max_hold, show_min_hold, show_average);
            return true;
        });
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::SpectrumPlot", opendigitizer::charts::SpectrumPlot)
inline auto registerSpectrumPlot = gr::registerBlock<opendigitizer::charts::SpectrumPlot>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_SPECTRUMPLOT_HPP
