#ifndef OPENDIGITIZER_CHARTS_SPECTRUMVIEW_HPP
#define OPENDIGITIZER_CHARTS_SPECTRUMVIEW_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

enum class TopPaneMode : int {
    Spectrum = 0, // magnitude line traces (max/min hold, average)
    Density  = 1  // 2-D histogram heatmap
};

struct SpectrumView : gr::Block<SpectrumView, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"Composite spectrum display: magnitude plot or density (top) + waterfall (bottom) with synchronised X-axis.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // identity
    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks  = {};
    A<bool, "show legend", gr::Visible>                    show_legend = false;
    A<bool, "show grid", gr::Visible>                      show_grid   = true;

    A<TopPaneMode, "top pane mode", gr::Visible> top_pane_mode = TopPaneMode::Spectrum;

    // spectrum trace settings (top pane mode 0)
    A<bool, "show max hold", gr::Visible>                                                                       show_max_hold    = true;
    A<bool, "show min hold", gr::Visible>                                                                       show_min_hold    = true;
    A<bool, "show average", gr::Visible>                                                                        show_average     = false;
    A<std::uint32_t, "trace color", gr::Visible, gr::Doc<"RGB colour for accumulated traces (0xRRGGBB)">>       trace_color      = 0x8855BBU;
    A<gr::Size_t, "decay tau frames", gr::Doc<"exponential decay time constant in frames (0 = infinite hold)">> decay_tau_frames = 100U;

    // density settings (top pane mode 1)
    A<gr::Size_t, "amplitude bins", gr::Visible, gr::Limits<4U, 1024U>>                       amplitude_bins             = 256U;
    A<gr::Size_t, "histogram decay tau", gr::Doc<"histogram decay in frames (0 = infinite)">> histogram_decay_tau_frames = 100U;
    A<bool, "show current overlay", gr::Visible>                                              show_current_overlay       = true;

    // waterfall settings (bottom pane)
    A<gr::Size_t, "history depth", gr::Visible, gr::Limits<16U, 4096U>>                                      n_history        = 256U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                                              colormap         = ImPlotColormap_Viridis;
    A<bool, "GPU acceleration", gr::Doc<"use GPU texture for rendering (falls back to CPU if unavailable)">> gpu_acceleration = true;

    // pane layout
    A<float, "top pane ratio", gr::Limits<0.2f, 0.8f>, gr::Doc<"fraction of height for top pane">> top_pane_ratio = 0.4f;

    // axis limits
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = true;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = -120.0;
    A<double, "Y-axis max"> y_max        = 0.0;

    GR_MAKE_REFLECTABLE(SpectrumView, chart_name, data_sinks, show_legend, show_grid, top_pane_mode, show_max_hold, show_min_hold, show_average, trace_color, decay_tau_frames, amplitude_bins, histogram_decay_tau_frames, show_current_overlay, n_history, colormap, gpu_acceleration, top_pane_ratio, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    std::unordered_map<std::string, TraceAccumulator> _tracesPerSink;
    DensityHistogram                                  _density;
    WaterfallBuffer                                   _waterfall;
    std::size_t                                       _lastSpectrumSize    = 0;
    int64_t                                           _lastPushedTimestamp = 0;
    std::array<float, 2>                              _rowRatios           = {0.4f, 0.6f};
    std::array<std::string, 6>                        _unitStore{};

    struct RenderInfo {
        double freqMin;
        double freqMax;
    };
    std::optional<RenderInfo> _lastRenderInfo;
    ImPlotCond                _sharedXCond = ImPlotCond_Once; // computed once per frame, shared across both panes
    double                    _topPaneYMin = -120.0;          // captured from the displayed top-pane Y-axis range
    double                    _topPaneYMax = 0.0;

    static constexpr std::string_view kChartTypeName = "SpectrumView";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    void reset() {
        _tracesPerSink.clear();
        _density.reset();
        _waterfall.clear();
    }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        _waterfall.setPreferGpu(gpu_acceleration);
        if (_pendingResizeTime == 0.0 && _waterfall.width() > 0) {
            _waterfall.resizeHistory(static_cast<std::size_t>(n_history));
        }

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        _rowRatios[0] = top_pane_ratio;
        _rowRatios[1] = 1.0f - top_pane_ratio;

        // precompute x-axis limit condition once — both panes share x-axis via LinkAllX
        {
            const auto dashCfg = parseAxisConfig(this->ui_constraints.value, true);
            double     xMinLim = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(x_min.value);
            double     xMaxLim = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(x_max.value);
            if (dashCfg && x_auto_scale.value) {
                xMinLim = std::isfinite(dashCfg->min) ? static_cast<double>(dashCfg->min) : xMinLim;
                xMaxLim = std::isfinite(dashCfg->max) ? static_cast<double>(dashCfg->max) : xMaxLim;
            }
            _sharedXCond = trackLimitsCond(true, xMinLim, xMaxLim);
        }

        ImPlotSubplotFlags subplotFlags = ImPlotSubplotFlags_LinkCols | ImPlotSubplotFlags_LinkAllX;
        auto               subplotId    = std::format("##combined_{}", chart_name.value);
        if (!ImPlot::BeginSubplots(subplotId.c_str(), 2, 1, plotSize, subplotFlags, _rowRatios.data())) {
            return gr::work::Status::OK;
        }

        drawTopPane(plotFlags, showGrid);
        drawBottomPane(plotFlags, showGrid);

        ImPlot::EndSubplots();
        return gr::work::Status::OK;
    }

    void setupFrequencyAxis(bool showGrid) {
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
        axis::setupAxis(ImAxis_X1, xCat, format, 100.f, minLimit, maxLimit, 1, scale, unitStore, showGrid, /*foreground=*/true, _sharedXCond);
    }

    void drawTopPane(ImPlotFlags plotFlags, bool showGrid) {
        const bool isDensity = (top_pane_mode.value == TopPaneMode::Density);

        ImGui::PushID("top");
        if (isDensity) {
            ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));
        }

        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2{0.0f, 0.05f});
        if (ImPlot::BeginPlot("##spectrum", ImVec2(0, 0), plotFlags)) {
            setupFrequencyAxis(showGrid);
            setupMagnitudeAxis(showGrid);
            ImPlot::SetupFinish();

            if (isDensity) {
                drawDensitySignals();
            } else {
                drawSpectrumSignals();
            }

            tooltip::showPlotMouseTooltip();
            handleCommonInteractions();

            auto yLimits = ImPlot::GetPlotLimits(ImAxis_X1, ImAxis_Y1).Y;
            _topPaneYMin = yLimits.Min;
            _topPaneYMax = yLimits.Max;

            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();

        if (isDensity) {
            ImPlot::PopStyleColor();
        }
        ImGui::PopID();
    }

    void setupMagnitudeAxis(bool showGrid) {
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

    void drawSpectrumSignals() {
        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
            plotTrace(std::string(sink.signalName()).c_str(), f.xValues, f.yValues, f.nBins, sinkColor(sink.color()));
            auto& traces = _tracesPerSink[std::string(sink.signalName())];
            drawTraceOverlays(traces, f.xValues, f.yValues, f.nBins, static_cast<double>(decay_tau_frames), sinkColor(trace_color), show_max_hold, show_min_hold, show_average);
            return true;
        });
    }

    void drawDensitySignals() {
        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
            const auto ampBins = static_cast<std::size_t>(amplitude_bins);
            double     effYMin = y_min.value;
            double     effYMax = y_max.value;

            _density.update(f.yValues, f.nBins, ampBins, static_cast<double>(histogram_decay_tau_frames), effYMin, effYMax, colormap.value, gpu_acceleration);
            _density.plot(f.xValues, effYMin, effYMax);

            ImVec4 traceBase = sinkColor(trace_color);
            auto&  traces    = _tracesPerSink[std::string(sink.signalName())];
            drawTraceOverlays(traces, f.xValues, f.yValues, f.nBins, static_cast<double>(decay_tau_frames), traceBase, show_max_hold, show_min_hold, show_average);

            if (show_current_overlay.value) {
                plotTrace("##current", f.xValues, f.yValues, f.nBins, ImVec4(traceBase.x, traceBase.y, traceBase.z, 1.0f));
            }

            return false; // density display uses first valid sink only
        });
    }

    void drawBottomPane(ImPlotFlags plotFlags, bool showGrid) {
        ImGui::PushID("bottom");
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2{0.0f, 0.05f});

        if (ImPlot::BeginPlot("##waterfall", ImVec2(0, 0), plotFlags | ImPlotFlags_NoLegend)) {
            setupBottomFrequencyAxis(showGrid);
            setupWaterfallYAxis(showGrid);

            auto renderInfo = fetchAndPushData();

            auto [tOldest, tNewest] = _waterfall.rawTimeBounds();
            auto [yLo, yHi]         = transformedYBounds(tOldest, tNewest);

            if (_waterfall.filledRows() > 0 && yHi > yLo) {
                ImPlot::SetupAxisLimits(ImAxis_Y1, yLo, yHi, ImPlotCond_Always);
            }

            ImPlot::SetupFinish();

            constexpr bool newestAtTop = true;
            if (renderInfo) {
                _waterfall.render(renderInfo->freqMin, renderInfo->freqMax, yLo, yHi, newestAtTop);
            } else if (_waterfall.filledRows() > 0 && _lastRenderInfo) {
                _waterfall.render(_lastRenderInfo->freqMin, _lastRenderInfo->freqMax, yLo, yHi, newestAtTop);
            }

            tooltip::showPlotMouseTooltip();
            drawContextMenu("SpectrumViewBottom");
            handlePlotDropTarget();
            ImPlot::EndPlot();
        }

        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor();
        ImGui::PopID();
    }

    void setupBottomFrequencyAxis(bool showGrid) {
        double     minLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_min.value;
        double     maxLimit = x_auto_scale.value ? std::numeric_limits<double>::quiet_NaN() : x_max.value;
        const auto dashCfg  = parseAxisConfig(this->ui_constraints.value, true);
        if (dashCfg && x_auto_scale.value) {
            minLimit = std::isfinite(dashCfg->min) ? static_cast<double>(dashCfg->min) : minLimit;
            maxLimit = std::isfinite(dashCfg->max) ? static_cast<double>(dashCfg->max) : maxLimit;
        }

        auto [xQuantity, xUnit] = sinkAxisInfo(true);
        AxisCategory               xCat{.quantity = xQuantity, .unit = xUnit};
        std::array<std::string, 6> unitStore{};
        axis::setupAxis(ImAxis_X1, xCat, LabelFormat::None, 100.f, minLimit, maxLimit, 1, AxisScale::Linear, unitStore, showGrid, /*foreground=*/true, _sharedXCond);
    }

    void setupWaterfallYAxis(bool showGrid) {
        ImPlotAxisFlags yFlags = (showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines) | ImPlotAxisFlags_Foreground | ImPlotAxisFlags_NoTickLabels;
        ImPlot::SetupAxis(ImAxis_Y1, "", yFlags);
        ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
    }

    [[nodiscard]] static std::pair<double, double> transformedYBounds(double tOldest, double tNewest) {
        double duration = tNewest - tOldest;
        return {-duration, 0.0}; // newest at top (LinearReverse equivalent)
    }

    [[nodiscard]] std::optional<RenderInfo> fetchAndPushData() {
        std::optional<RenderInfo> result;
        forEachValidSpectrum(_signalSinks, [&](const auto& /*sink*/, const SpectrumFrame& f) -> bool {
            if (f.timestamp == _lastPushedTimestamp && _lastPushedTimestamp != 0) {
                return false;
            }
            _lastPushedTimestamp = f.timestamp;

            if (_lastSpectrumSize != f.nBins) {
                _waterfall.init(f.nBins, static_cast<std::size_t>(n_history), gpu_acceleration);
                _lastSpectrumSize = f.nBins;
            }

            _waterfall.updateAutoScale(f.yValues, f.nBins);
            _waterfall.pushRow(f.yValues, f.nBins, _topPaneYMin, _topPaneYMax, timestampFromNanos(f.timestamp), colormap.value);

            _lastRenderInfo = RenderInfo{.freqMin = static_cast<double>(f.xValues.front()), .freqMax = static_cast<double>(f.xValues.back())};
            result          = _lastRenderInfo;
            return false;
        });
        return result;
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::SpectrumView", opendigitizer::charts::SpectrumView)
inline auto registerSpectrumView = gr::registerBlock<opendigitizer::charts::SpectrumView>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_SPECTRUMVIEW_HPP
