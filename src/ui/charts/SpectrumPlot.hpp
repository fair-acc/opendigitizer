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
    std::array<std::string, 6UZ>                      _unitStore{};

    static constexpr std::string_view kChartTypeName = "SpectrumPlot";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        const auto&            lnf = DigitizerUi::LookAndFeel::instance();
        DigitizerUi::IMW::Font plotFont(lnf.fontSmall[lnf.prototypeMode ? 1UZ : 0UZ]); // smaller font to prevent MetricInline label overlap

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            return gr::work::Status::OK;
        }

        setupAxes(showGrid);
        ImPlot::SetupFinish();
        drawSpectrumSignals();
        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
        DigitizerUi::TouchHandler<>::EndZoomablePlot();

        return gr::work::Status::OK;
    }

    void reset() { _tracesPerSink.clear(); }

    void setupAxes(bool showGrid) {
        setupSingleAxis(true, ImAxis_X1, showGrid, LabelFormat::MetricInline);
        setupSingleAxis(false, ImAxis_Y1, showGrid);
    }

    void drawSpectrumSignals() {
        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
            if (sink.drawEnabled()) {
                plotTrace(std::string(sink.signalName()).c_str(), f.xValues, f.yValues, f.nBins, sinkColor(sink.color()));
            }
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
