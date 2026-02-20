#ifndef OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP
#define OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <chrono>
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
    A<gr::Size_t, "history depth", gr::Visible, gr::Limits<16U, 4096U>>                                      n_history        = 256U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                                              colormap         = ImPlotColormap_Viridis;
    A<bool, "GPU acceleration", gr::Doc<"use GPU texture for rendering (falls back to CPU if unavailable)">> gpu_acceleration = true;
    // axis limits (Z = colour scale: NaN min/max → auto-scale from data)
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = true;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = std::numeric_limits<double>::lowest();
    A<double, "Y-axis max"> y_max        = std::numeric_limits<double>::max();

    GR_MAKE_REFLECTABLE(WaterfallPlot, chart_name, chart_title, data_sinks, show_legend, show_grid, n_history, colormap, gpu_acceleration, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    struct RenderInfo {
        double freqMin;
        double freqMax;
    };

    WaterfallBuffer            _waterfall;
    std::size_t                _lastSpectrumSize = 0;
    std::array<std::string, 6> _unitStore{};
    int64_t                    _lastPushedTimestamp = 0;
    std::optional<RenderInfo>  _lastRenderInfo;

    static constexpr std::string_view kChartTypeName = "WaterfallPlot";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    [[nodiscard]] std::optional<AxisScale> getAxisScale(AxisKind axis) const noexcept {
        if (auto cfg = parseAxisConfig(this->ui_constraints.value, axis)) {
            if (cfg->scale) {
                return cfg->scale;
            }
        }
        return (axis == AxisKind::Y) ? AxisScale::LinearReverse : AxisScale::Linear;
    }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        // sync GPU preference with setting
        _waterfall.setPreferGpu(gpu_acceleration);

        // sync waterfall depth with n_history after debounced UI changes
        // (UI modifies n_history directly, bypassing settingsChanged)
        if (_pendingResizeTime == 0.0 && _waterfall.width() > 0) {
            _waterfall.resizeHistory(static_cast<std::size_t>(n_history));
        }

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        ImPlot::PushStyleColor(ImPlotCol_AxisGrid, contrastingGridColor(colormap.value));

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            ImPlot::PopStyleColor();
            return gr::work::Status::OK;
        }

        // phase 1: set up axes (X fully, Y skeleton without limits)
        setupAxes(showGrid);

        // phase 2: fetch new data and push into waterfall (skips duplicate frames)
        auto renderInfo = fetchAndPushData();

        // phase 3: compute Y-axis bounds in the coordinate system determined by the scale
        const AxisScale yScale  = getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse);
        auto [tOldest, tNewest] = _waterfall.rawTimeBounds();
        auto [yLo, yHi]         = transformedYBounds(tOldest, tNewest, yScale);

        if (_waterfall.filledRows() > 0 && yHi > yLo) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, yLo, yHi, ImPlotCond_Always);
        }

        ImPlot::SetupFinish();

        // register legend entries for each sink (enables legend display and D&D)
        for (const auto& sink : _signalSinks) {
            ImVec4 color = sinkColor(sink->color());
            ImPlot::SetNextLineStyle(color);
            ImPlot::PlotDummy(std::string(sink->signalName()).c_str());
        }

        // phase 4: render waterfall image (always, even when no new data this frame)
        const bool newestAtTop = (yScale == AxisScale::LinearReverse);
        if (renderInfo) {
            _waterfall.render(renderInfo->freqMin, renderInfo->freqMax, yLo, yHi, newestAtTop);
        } else if (_waterfall.filledRows() > 0 && _lastRenderInfo) {
            _waterfall.render(_lastRenderInfo->freqMin, _lastRenderInfo->freqMax, yLo, yHi, newestAtTop);
        }

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        ImPlot::PopStyleColor();

        return gr::work::Status::OK;
    }

    void reset() { _waterfall.clear(); }

    [[nodiscard]] static std::pair<double, double> transformedYBounds(double tOldest, double tNewest, AxisScale scale) {
        double duration = tNewest - tOldest;
        if (scale == AxisScale::Time) {
            return {tOldest, tNewest};
        }
        if (scale == AxisScale::LinearReverse) {
            return {-duration, 0.0};
        }
        return {0.0, duration}; // Linear, Log10, SymLog — positive offsets
    }

    static int formatTimeAxis(double value, char* buff, int size, void* /*userData*/) {
        using Clock    = std::chrono::system_clock;
        auto timePoint = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(value)));
        auto secs      = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
        auto ms        = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - secs).count();
        auto result    = std::format_to_n(buff, static_cast<std::ptrdiff_t>(size) - 1, "{:%H:%M:%S}.{:02}", secs, ms / 10);
        *result.out    = '\0';
        return static_cast<int>(result.out - buff);
    }

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
            AxisCategory xCat{.quantity = xQuantity, .unit = xUnit};
            axis::setupAxis(ImAxis_X1, xCat, format, 100.f, minLimit, maxLimit, 1, scale, _unitStore, showGrid, /*foreground=*/true);
        }

        // y-axis: time — limits are set in draw() after data push so axis and image stay synchronised.
        // ImPlotScale_Time is X-axis-only; we use Linear + custom formatter.
        {
            AxisScale scale = getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse);

            ImPlotAxisFlags yFlags = (showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines) | ImPlotAxisFlags_Foreground;
            ImPlot::SetupAxis(ImAxis_Y1, "", yFlags);

            if (scale == AxisScale::Time) {
                ImPlot::SetupAxisFormat(ImAxis_Y1, formatTimeAxis, nullptr);
                ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
            } else {
                _unitStore[ImAxis_Y1] = "s";
                ImPlot::SetupAxisFormat(ImAxis_Y1, axis::formatMetric, const_cast<void*>(static_cast<const void*>(_unitStore[ImAxis_Y1].c_str())));
                switch (scale) {
                case AxisScale::Log10: ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10); break;
                case AxisScale::SymLog: ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_SymLog); break;
                default: ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear); break;
                }
            }
        }
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
            auto [cMin, cMax] = effectiveColourRange(this->ui_constraints.value, _waterfall.scaleMin(), _waterfall.scaleMax());
            _waterfall.pushRow(f.yValues, f.nBins, cMin, cMax, timestampFromNanos(f.timestamp), colormap.value);

            _lastRenderInfo = RenderInfo{.freqMin = static_cast<double>(f.xValues.front()), .freqMax = static_cast<double>(f.xValues.back())};
            result          = _lastRenderInfo;
            return false;
        });
        return result;
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::WaterfallPlot", opendigitizer::charts::WaterfallPlot)
inline auto registerWaterfallPlot = gr::registerBlock<opendigitizer::charts::WaterfallPlot>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP
