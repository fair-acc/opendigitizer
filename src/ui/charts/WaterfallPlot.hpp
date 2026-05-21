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

    static constexpr SignalKind supportedSignals = SignalKind::Dataset1D;

    GR_MAKE_REFLECTABLE(WaterfallPlot, chart_name, chart_title, data_sinks, show_legend, show_grid, n_history, colormap, gpu_acceleration, orientation, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    struct RenderInfo {
        double freqMin;
        double freqMax;
    };

    WaterfallBuffer            _waterfall;
    std::size_t                _lastInitWidth = 0; // allocated texture width (nBins, or kLogSpectrumColumns in log mode)
    std::array<std::string, 6> _unitStore{};
    std::size_t                _lastPushedSampleCount = std::numeric_limits<std::size_t>::max();
    std::optional<RenderInfo>  _lastRenderInfo;
    std::vector<float>         _logRow; // scratch: linear spectrum re-binned onto log-spaced columns

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
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid] = prepareDrawPrologue(config);

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
        setupAxes(showGrid);

        // phase 2: fetch new data and push into waterfall (skips duplicate frames)
        auto renderInfo = fetchAndPushData();

        // phase 3: compute the time-axis bounds; in horizontal mode time is on X, otherwise on Y
        const bool      horizontal = orientation.value == WaterfallOrientation::Horizontal;
        const ImAxis    timeAxis   = horizontal ? ImAxis_X1 : ImAxis_Y1;
        const AxisScale timeScale  = getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse);
        auto [tOldest, tNewest]    = _waterfall.rawTimeBounds();
        auto [timeLo, timeHi]      = transformedYBounds(tOldest, tNewest, timeScale);

        if (_waterfall.filledRows() > 0 && timeHi > timeLo) {
            ImPlot::SetupAxisLimits(timeAxis, timeLo, timeHi, ImPlotCond_Always);
        }

        ImPlot::SetupFinish();

        // register legend entries for each sink (enables legend display and D&D)
        for (const auto& sink : _signalSinks) {
            ImVec4 color = sinkColor(sink->color());
            ImPlot::SetNextLineStyle(color);
            ImPlot::PlotDummy(std::string(sink->signalName()).c_str());
        }

        // phase 4: render waterfall image (always, even when no new data this frame)
        const bool newestLeading = (timeScale == AxisScale::LinearReverse);
        if (renderInfo) {
            _waterfall.render(renderInfo->freqMin, renderInfo->freqMax, timeLo, timeHi, newestLeading, horizontal);
        } else if (_waterfall.filledRows() > 0 && _lastRenderInfo) {
            _waterfall.render(_lastRenderInfo->freqMin, _lastRenderInfo->freqMax, timeLo, timeHi, newestLeading, horizontal);
        }

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
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
        // frequency keeps the "X" dashboard entry, time the "Y" entry; orientation only rotates which screen axis each uses
        const bool   horizontal = orientation.value == WaterfallOrientation::Horizontal;
        const ImAxis freqAxis   = horizontal ? ImAxis_Y1 : ImAxis_X1;
        const ImAxis timeAxis   = horizontal ? ImAxis_X1 : ImAxis_Y1;

        // frequency axis
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
            axis::setupAxis(freqAxis, xCat, format, 100.f, minLimit, maxLimit, 1, scale, _unitStore, showGrid, /*foreground=*/true);
        }

        // time axis — limits are set in draw() after data push so axis and image stay synchronised.
        // ImPlotScale_Time is X-axis-only; we use Linear + a custom formatter uniformly.
        {
            AxisScale       scale  = getAxisScale(AxisKind::Y).value_or(AxisScale::LinearReverse);
            ImPlotAxisFlags tFlags = (showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines) | ImPlotAxisFlags_Foreground;
            ImPlot::SetupAxis(timeAxis, "", tFlags);

            if (scale == AxisScale::Time) {
                ImPlot::SetupAxisFormat(timeAxis, formatTimeAxis, nullptr);
                ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Linear);
            } else {
                _unitStore[static_cast<std::size_t>(timeAxis)] = "s";
                ImPlot::SetupAxisFormat(timeAxis, axis::formatMetric, const_cast<void*>(static_cast<const void*>(_unitStore[static_cast<std::size_t>(timeAxis)].c_str())));
                switch (scale) {
                case AxisScale::Log10: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Log10); break;
                case AxisScale::SymLog: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_SymLog); break;
                default: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Linear); break;
                }
            }
        }
    }

    [[nodiscard]] std::optional<RenderInfo> fetchAndPushData() {
        std::optional<RenderInfo> result;
        const auto                logRange = logFreqRange(parseAxisConfig(this->ui_constraints.value, AxisKind::X));

        forEachValidSpectrum(_signalSinks, [&](const auto& sink, const SpectrumFrame& f) -> bool {
            if (!consumeNewData(_lastPushedSampleCount, sink.totalSampleCount())) {
                return false;
            }

            const std::size_t width = logRange ? kLogSpectrumColumns : f.nBins;
            if (_lastInitWidth != width) {
                _waterfall.init(width, static_cast<std::size_t>(n_history), gpu_acceleration);
                _lastInitWidth = width;
            }

            _waterfall.updateAutoScale(f.yValues, f.nBins);
            auto [cMin, cMax] = effectiveColourRange(this->ui_constraints.value, _waterfall.scaleMin(), _waterfall.scaleMax());

            if (logRange) {
                _logRow.resize(kLogSpectrumColumns);
                buildLogBinnedRow(f.xValues, f.yValues, f.nBins, logRange->min, logRange->max, _logRow);
                _waterfall.pushRow(_logRow, kLogSpectrumColumns, cMin, cMax, timestampFromNanos(f.timestamp), colormap.value);
                _lastRenderInfo = RenderInfo{.freqMin = logRange->min, .freqMax = logRange->max};
            } else {
                _waterfall.pushRow(f.yValues, f.nBins, cMin, cMax, timestampFromNanos(f.timestamp), colormap.value);
                _lastRenderInfo = RenderInfo{.freqMin = static_cast<double>(f.xValues.front()), .freqMax = static_cast<double>(f.xValues.back())};
            }
            result = _lastRenderInfo;
            return false;
        });
        return result;
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::WaterfallPlot", opendigitizer::charts::WaterfallPlot)
inline auto registerWaterfallPlot                = gr::registerBlock<opendigitizer::charts::WaterfallPlot>(gr::globalBlockRegistry());
inline auto registerWaterfallPlotCompatibilities = opendigitizer::charts::registerChartSignalCompatibility<opendigitizer::charts::WaterfallPlot>();

#endif // OPENDIGITIZER_CHARTS_WATERFALLPLOT_HPP
