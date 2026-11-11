#ifndef OPENDIGITIZER_CHARTS_TENSORROWSCHART_HPP
#define OPENDIGITIZER_CHARTS_TENSORROWSCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include <implot.h>

namespace opendigitizer::charts {

[[nodiscard]] inline std::string axisTitle(const ImageAxis& axis, std::string_view fallback) {
    if (axis.name.empty()) {
        return std::string(fallback);
    }
    return axis.unit.empty() ? axis.name : std::format("{} [{}]", axis.name, axis.unit);
}

[[nodiscard]] inline std::string rowLabel(const ImageAxis& axis, std::size_t row) { return std::format("{} {:g}", axis.name.empty() ? std::string_view("row") : std::string_view(axis.name), axisCoordinate(axis, row)); }

/// the columns of a cross-correlation frame [P, 2 max_lag + 1], e.g. gr::localisation::PairCrossCorrelation's: lag 0 in the middle, 1/sampleRate apart
[[nodiscard]] inline ImageAxis lagAxis(std::size_t columns, double sampleRate) { return {.origin = -static_cast<double>(columns > 0UZ ? columns - 1UZ : 0UZ) / (2.0 * sampleRate), .step = 1.0 / sampleRate, .name = "lag", .unit = "s"}; }

struct TensorRowsChart : gr::Block<TensorRowsChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<R""(@brief Draws each row of a [rows, columns] float or double Tensor frame as one trace, e.g. the pair lag curves [P, L] of gr::localisation::PairCrossCorrelation.
The x axis and the trace names are the frame's gr:axis_* coordinates, else the x axis is the lag with `lag_columns`, else the column index.)"">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName = "TensorRowsChart";

    A<std::string, "chart name", gr::Visible>                                                                        chart_name;
    A<std::string, "chart title">                                                                                    chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                                                           data_sinks   = {};
    A<bool, "show legend", gr::Visible>                                                                              show_legend  = true;
    A<bool, "show grid", gr::Visible>                                                                                show_grid    = true;
    A<bool, "X auto-scale">                                                                                          x_auto_scale = true;
    A<bool, "Y auto-scale">                                                                                          y_auto_scale = true;
    A<bool, "lag columns", gr::Doc<"columns are lags, lag 0 in the middle, drawn in s from the sink's sample rate">> lag_columns  = false;

    static constexpr SignalKind supportedSignals = SignalKind::Image2D;

    GR_MAKE_REFLECTABLE(TensorRowsChart, chart_name, chart_title, data_sinks, show_legend, show_grid, x_auto_scale, y_auto_scale, lag_columns);

    std::size_t _drawnTraces = 0UZ;

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }
    [[nodiscard]] std::size_t                       drawnTraces() const noexcept { return _drawnTraces; }

    void reset() { requestRenderReset(); }

    [[nodiscard]] ImageAxis columnAxis(const ImageFrame& frame, float sampleRate) const {
        const bool tagged = !frame.axes[1].name.empty();
        return !tagged && lag_columns && sampleRate > 0.f ? lagAxis(frame.columns, static_cast<double>(sampleRate)) : frame.axes[1];
    }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);
        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            return gr::work::Status::OK;
        }
        std::vector<const SignalSink*> distinct = _signalSinks | std::views::transform([](const auto& sink) { return static_cast<const SignalSink*>(sink.get()); }) | std::ranges::to<std::vector>();
        std::ranges::sort(distinct); // each sink's mutex once, in one order
        distinct.erase(std::ranges::unique(distinct).begin(), distinct.end());
        std::vector<DataGuard> guards; // the frames' pixels are the sinks' storage
        guards.reserve(distinct.size());
        for (const SignalSink* sink : distinct) {
            guards.push_back(sink->dataGuard());
        }
        std::vector<ImageFrame> frames;
        frames.reserve(_signalSinks.size());
        for (const auto& sink : _signalSinks) {
            frames.push_back(sink->imageFrame());
            frames.back().axes[1] = columnAxis(frames.back(), sink->sampleRate());
        }
        const ImageAxis&      columns   = frames.front().axes[1];
        const ImPlotAxisFlags gridFlags = showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines;
        ImPlot::SetupAxis(ImAxis_X1, axisTitle(columns, "column").c_str(), gridFlags | (x_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None));
        ImPlot::SetupAxis(ImAxis_Y1, "value", gridFlags | (y_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None));
        setupFinish();

        _drawnTraces = 0UZ;
        for (std::size_t i = 0UZ; i < frames.size(); ++i) {
            std::visit(
                [&]<typename TPixels>(const TPixels& pixels) {
                    if constexpr (std::is_same_v<TPixels, std::span<const float>> || std::is_same_v<TPixels, std::span<const double>>) {
                        drawRows(frames[i], pixels, plotLabel(*_signalSinks[i]));
                    }
                },
                frames[i].pixels);
        }

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        return gr::work::Status::OK;
    }

    void resetRenderState() { _drawnTraces = 0UZ; }

    template<typename T>
    void drawRows(const ImageFrame& frame, std::span<const T> pixels, const std::string& sinkLabel) {
        struct Row {
            std::span<const T> values;
            const ImageAxis*   columns;
        };
        const auto pointAt = [](int column, void* data) -> ImPlotPoint {
            const auto* row = static_cast<const Row*>(data);
            const auto  c   = static_cast<std::size_t>(column);
            return {axisCoordinate(*row->columns, c), static_cast<double>(row->values[c])};
        };
        for (std::size_t r = 0UZ; r < frame.rows; ++r) {
            Row               row{pixels.subspan(r * frame.columns, frame.columns), &frame.axes[1]};
            const std::string label = std::format("{}##{}", rowLabel(frame.axes[0], r), sinkLabel);
            ImPlot::PlotLineG(label.c_str(), pointAt, &row, static_cast<int>(frame.columns));
            ++_drawnTraces;
        }
    }
};

} // namespace opendigitizer::charts

inline auto registerTensorRowsChartCompatibilities = opendigitizer::charts::registerChartSignalCompatibility<opendigitizer::charts::TensorRowsChart>();

#endif // OPENDIGITIZER_CHARTS_TENSORROWSCHART_HPP
