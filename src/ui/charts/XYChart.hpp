#ifndef OPENDIGITIZER_CHARTS_XYCHART_HPP
#define OPENDIGITIZER_CHARTS_XYCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

struct PlotLineContext {
    const SignalSink* sink;
    AxisScale         axis_scale;
    double            x_min;
    double            x_max;
    float             y_offset{0.0f};
    std::size_t       offset{0};
};

struct DataSetPlotContext {
    const gr::DataSet<float>* data_set;
    std::size_t               signal_index{0}; // which signal within the DataSet to plot
};

/// XYChart - standard X-Y line/scatter chart as a GR4 block.
/// Plots Y-values against a common X-axis with support for multiple axes, automatic signal grouping,
/// various X-axis modes (UTC/relative time, sample index), tag rendering, and DataSet history fading.
struct XYChart : gr::Block<XYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    // annotated type alias for cleaner declarations
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    A<std::string, "chart name", gr::Visible>                                       chart_name;
    A<std::string, "chart title">                                                   chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                          data_sinks              = {};
    A<int, "X-axis mode", gr::Visible>                                              x_axis_mode             = static_cast<int>(XAxisMode::RelativeTime);
    A<bool, "show legend", gr::Visible>                                             show_legend             = false;
    A<bool, "show tags", gr::Visible>                                               show_tags               = true;
    A<bool, "show grid", gr::Visible>                                               show_grid               = true;
    A<bool, "anti-aliasing">                                                        anti_aliasing           = true;
    A<gr::Size_t, "max history count", gr::Limits<1U, 128U>>                        max_history_count       = 3U;
    A<float, "history opacity decay", gr::Limits<0.001f, 1.f>>                      history_opacity_decay   = 0.3f;
    A<float, "history vertical offset", gr::Limits<0.f, 100.f>>                     history_vertical_offset = 0.0f;
    A<std::array<bool, 3UZ>, "X auto-scale">                                        x_auto_scale            = std::array{true, true, true};
    A<std::array<bool, 3UZ>, "Y auto-scale">                                        y_auto_scale            = std::array{true, true, true};
    A<std::array<double, 3UZ>, "X-axis min">                                        x_min                   = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "X-axis max">                                        x_max                   = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis min">                                        y_min                   = std::array{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<std::array<double, 3UZ>, "Y-axis max">                                        y_max                   = std::array{std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    A<gr::Size_t, "history depth", gr::Unit<"samples">, gr::Limits<4U, 2'000'000U>> n_history               = kDefaultHistorySize;

    GR_MAKE_REFLECTABLE(XYChart, chart_name, chart_title, data_sinks, x_axis_mode, show_legend, show_tags, show_grid, anti_aliasing, max_history_count, history_opacity_decay, history_vertical_offset, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max, n_history);

    std::array<std::optional<AxisCategory>, 3UZ> _xCategories{};
    std::array<std::optional<AxisCategory>, 3UZ> _yCategories{};
    std::array<std::vector<std::string>, 3UZ>    _xAxisGroups{};
    std::array<std::vector<std::string>, 3UZ>    _yAxisGroups{};
    mutable std::array<std::string, 6UZ>         _unitStringStorage{};

    static constexpr std::string_view kChartTypeName = "XYChart";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    explicit XYChart(gr::property_map initParameters = {}) : gr::Block<XYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>(std::move(initParameters)) {}

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, layoutMode, showGrid] = prepareDrawPrologue(config);

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize);
            return gr::work::Status::OK;
        }

        buildAxisCategoriesWithFallback();

        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags)) {
            return gr::work::Status::OK;
        }

        setupAxes(showGrid);
        ImPlot::SetupFinish();
        drawSignals();
        tooltip::showPlotMouseTooltip();
        handleCommonInteractions();
        DigitizerUi::TouchHandler<>::EndZoomablePlot();

        return gr::work::Status::OK;
    }

    void buildAxisCategoriesWithFallback() {
        axis::buildAxisCategories(_signalSinks, _xCategories, _yCategories, _xAxisGroups, _yAxisGroups);

        // Set default categories if none were created (fallback for XYChart)
        if (!_signalSinks.empty() && !_xCategories[0].has_value()) {
            _xCategories[0] = AxisCategory{"time", "s", _signalSinks[0]->color()};
            for (const auto& sink : _signalSinks) {
                _xAxisGroups[0].push_back(std::string(sink->uniqueName()));
            }
        }
        if (!_signalSinks.empty() && !_yCategories[0].has_value()) {
            _yCategories[0] = AxisCategory{"signal", "", _signalSinks[0]->color()};
            for (const auto& sink : _signalSinks) {
                _yAxisGroups[0].push_back(std::string(sink->uniqueName()));
            }
        }
    }

    void setupAxes(bool showGrid = true) {
        constexpr float   xAxisWidth = 100.f;
        constexpr float   yAxisWidth = 100.f;
        const std::size_t nAxesX     = axis::activeAxisCount(_xCategories);
        const std::size_t nAxesY     = axis::activeAxisCount(_yCategories);

        for (std::size_t i = 0; i < _xCategories.size(); ++i) {
            const auto dashCfg = parseAxisConfig(this->ui_constraints.value, true, i);

            double minLimit, maxLimit;
            if (!x_auto_scale.value[i]) {
                minLimit = x_min.value[i];
                maxLimit = x_max.value[i];
            } else {
                minLimit = dashCfg ? static_cast<double>(dashCfg->min) : std::numeric_limits<double>::quiet_NaN();
                maxLimit = dashCfg ? static_cast<double>(dashCfg->max) : std::numeric_limits<double>::quiet_NaN();
            }

            const LabelFormat format = dashCfg ? dashCfg->format : LabelFormat::Auto;
            const float       width  = dashCfg && std::isfinite(dashCfg->width) ? dashCfg->width : xAxisWidth;
            const AxisScale   scale  = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;

            if (_xCategories[i].has_value()) {
                _xCategories[i]->scale = scale;
            }

            auto xCond = trackLimitsCond(true, minLimit, maxLimit, i);
            axis::setupAxis(ImAxis_X1 + static_cast<int>(i), _xCategories[i], format, width, minLimit, maxLimit, nAxesX, scale, _unitStringStorage, showGrid, /*foreground=*/false, xCond);
        }

        for (std::size_t i = 0; i < _yCategories.size(); ++i) {
            const auto dashCfg = parseAxisConfig(this->ui_constraints.value, false, i);

            double minLimit, maxLimit;
            if (!y_auto_scale.value[i]) {
                minLimit = y_min.value[i];
                maxLimit = y_max.value[i];
            } else {
                minLimit = dashCfg ? static_cast<double>(dashCfg->min) : std::numeric_limits<double>::quiet_NaN();
                maxLimit = dashCfg ? static_cast<double>(dashCfg->max) : std::numeric_limits<double>::quiet_NaN();
            }

            const LabelFormat format = dashCfg ? dashCfg->format : LabelFormat::Auto;
            const float       width  = dashCfg && std::isfinite(dashCfg->width) ? dashCfg->width : yAxisWidth;
            const AxisScale   scale  = dashCfg ? dashCfg->scale.value_or(AxisScale::Linear) : AxisScale::Linear;

            if (_yCategories[i].has_value()) {
                _yCategories[i]->scale = scale;
            }

            auto yCond = trackLimitsCond(false, minLimit, maxLimit, i);
            axis::setupAxis(ImAxis_Y1 + static_cast<int>(i), _yCategories[i], format, width, minLimit, maxLimit, nAxesY, scale, _unitStringStorage, showGrid, /*foreground=*/false, yCond);
        }
    }

    void drawSignals() {
        bool tagsDrawnForFirstSink = false;

        for (std::size_t i = 0; i < _signalSinks.size(); ++i) {
            const std::shared_ptr<SignalSink>& sink = _signalSinks[i];

            // Skip drawing if signal is hidden (data consumption is handled elsewhere)
            if (!sink->drawEnabled()) {
                continue;
            }

            std::string_view sinkUniqueName = sink->uniqueName();
            std::size_t      xAxisIdx       = axis::findAxisForSink(sinkUniqueName, true, _xAxisGroups, _yAxisGroups);
            std::size_t      yAxisIdx       = axis::findAxisForSink(sinkUniqueName, false, _xAxisGroups, _yAxisGroups);

            ImPlot::SetAxes(static_cast<ImAxis>(ImAxis_X1 + xAxisIdx), static_cast<ImAxis>(ImAxis_Y1 + yAxisIdx));

            // Acquire lock for thread-safe data access during rendering
            auto dataLock = sink->dataGuard();

            if (sink->size() > 0) {
                ImVec4 baseColor = sinkColor(sink->color());

                if (sink->hasDataSets()) {
                    drawDataSetSignal(*sink);

                    // Draw timing events for DataSets (only for first sink to avoid clutter)
                    if (show_tags.value && !tagsDrawnForFirstSink) {
                        auto allDataSets = sink->dataSets();
                        if (!allDataSets.empty()) {
                            tags::drawDataSetTimingEvents(allDataSets[0], _xCategories[xAxisIdx].has_value() ? _xCategories[xAxisIdx]->scale : AxisScale::Linear, baseColor);
                        }
                        tagsDrawnForFirstSink = true;
                    }
                } else {
                    drawStreamingSignal(*sink);

                    // Draw streaming tags (only for first sink to avoid clutter)
                    if (show_tags.value && !tagsDrawnForFirstSink) {
                        std::size_t totalCount = sink->size();
                        std::size_t dataCount  = std::min(totalCount, static_cast<std::size_t>(n_history.value));
                        std::size_t offset     = totalCount - dataCount;
                        double      xMin       = sink->xAt(offset);
                        double      xMax       = sink->xAt(offset + dataCount - 1);
                        AxisScale   xAxisScale = _xCategories[xAxisIdx].has_value() ? _xCategories[xAxisIdx]->scale : AxisScale::Linear;
                        ImVec4      tagColor   = baseColor;
                        tagColor.w *= 0.35f;
                        tags::drawTags([&](auto&& fn) { sink->forEachTag(fn); }, xAxisScale, xMin, xMax, tagColor);
                        sink->pruneTags(std::min(xMin, xMax));
                        tagsDrawnForFirstSink = true;
                    }
                }
            }
        }
    }

    void drawStreamingSignal(const SignalSink& sink) {
        // Note: Caller must hold data lock via sink.dataGuard()
        std::size_t totalCount = sink.size();
        if (totalCount == 0) {
            return;
        }

        // clamp to n_history: show only the most recent samples
        std::size_t dataCount = totalCount;
        std::size_t offset    = 0;
        if (dataCount > static_cast<std::size_t>(n_history.value)) {
            dataCount = static_cast<std::size_t>(n_history.value);
            offset    = totalCount - dataCount;
        }

        ImVec4 lineColor = sinkColor(sink.color());
        ImPlot::SetNextLineStyle(lineColor);

        double xMin = sink.xAt(offset);
        double xMax = sink.xAt(offset + dataCount - 1);

        AxisScale       xAxisScale = _xCategories[0].has_value() ? _xCategories[0]->scale : AxisScale::Linear;
        PlotLineContext ctx{&sink, xAxisScale, xMin, xMax, 0.0f, offset};

        std::string signalLabel{sink.signalName()};
        ImPlot::PlotLineG(
            signalLabel.c_str(),
            [](int idx, void* user_data) -> ImPlotPoint {
                auto*       context   = static_cast<PlotLineContext*>(user_data);
                std::size_t actualIdx = context->offset + static_cast<std::size_t>(idx);
                double      xVal      = context->sink->xAt(actualIdx);
                double      yVal      = static_cast<double>(context->sink->yAt(actualIdx)) + static_cast<double>(context->y_offset);

                switch (context->axis_scale) {
                case AxisScale::Time: break;
                case AxisScale::LinearReverse: xVal = xVal - context->x_max; break;
                default: xVal = xVal - context->x_min; break;
                }

                return ImPlotPoint(xVal, yVal);
            },
            &ctx, static_cast<int>(dataCount));
    }

    void drawDataSetSignal(const SignalSink& sink) {
        // DataSet signals: x values are absolute (e.g., frequency), no transformation needed
        // Supports history rendering with fading opacity for older DataSets
        auto allDataSets = sink.dataSets();
        if (allDataSets.empty()) {
            return;
        }

        ImVec4      baseColor   = sinkColor(sink.color());
        std::size_t historySize = std::min(allDataSets.size(), static_cast<std::size_t>(max_history_count.value));
        std::string baseName    = std::string(sink.signalName());

        // Draw from oldest to newest (so newest renders on top)
        // DataSets are ordered oldest-first in the span (push_back appends newest at the end)
        const std::size_t spanSize = allDataSets.size();
        const std::size_t offset   = spanSize - historySize; // skip older entries
        for (std::size_t i = 0; i < historySize; ++i) {
            const auto&        ds = allDataSets[offset + i]; // i=0 is oldest of selected, i=historySize-1 is newest
            DataSetPlotContext ctx{&ds, 0};

            const bool isNewest = (i == historySize - 1);
            float      opacity  = isNewest ? 1.0f : (0.2f + (static_cast<float>(i) / static_cast<float>(std::max(historySize, std::size_t{2}) - 1)) * 0.8f);

            ImVec4 lineColor = baseColor;
            lineColor.w      = opacity;
            ImPlot::SetNextLineStyle(lineColor);

            std::string label = isNewest ? baseName : ("##" + baseName + "_hist_" + std::to_string(i));

            // Get data count from DataSet's axis_values
            if (ds.axis_values.empty() || ds.axis_values[0].empty()) {
                continue;
            }
            int dataCount = static_cast<int>(ds.axis_values[0].size());

            ImPlot::PlotLineG(
                label.c_str(),
                [](int idx, void* user_data) -> ImPlotPoint {
                    auto*       context  = static_cast<DataSetPlotContext*>(user_data);
                    const auto& data_set = *context->data_set;
                    double      xVal     = static_cast<double>(data_set.axis_values[0][static_cast<std::size_t>(idx)]);
                    double      yVal     = 0.0;
                    auto        sigVals  = data_set.signalValues(context->signal_index);
                    if (static_cast<std::size_t>(idx) < sigVals.size()) {
                        yVal = static_cast<double>(sigVals[static_cast<std::size_t>(idx)]);
                    }
                    return ImPlotPoint(xVal, yVal);
                },
                &ctx, dataCount);
        }
    }
};

} // namespace opendigitizer::charts

// register XYChart with the GR4 block registry
// GR_REGISTER_BLOCK is a marker macro for build tools; actual registration via gr::registerBlock
GR_REGISTER_BLOCK("opendigitizer::charts::XYChart", opendigitizer::charts::XYChart)
inline auto registerXYChart = gr::registerBlock<opendigitizer::charts::XYChart>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_XYCHART_HPP
