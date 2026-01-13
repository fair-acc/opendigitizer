#ifndef OPENDIGITIZER_CHARTS_YYCHART_HPP
#define OPENDIGITIZER_CHARTS_YYCHART_HPP

#include "ChartInterface.hpp"
#include "ChartUtils.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

/**
 * @brief YYChart - Correlation plot (Y1 vs Y2) as a GR4 block.
 *
 * Plots Y-values from one signal against Y-values from another signal.
 * Use cases: Lissajous figures, phase plots, I-Q diagrams.
 *
 * Signal interpretation:
 *   - 1 signal: Falls back to XY mode (time vs Y)
 *   - 2 signals: Y1 vs Y2 correlation
 *   - 3+ signals: First signal is X axis, remaining signals share Y axis
 */
struct YYChart : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>, ChartInterface {
    // Annotated type alias for cleaner declarations
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // --- Reflectable settings (GR_MAKE_REFLECTABLE) ---
    A<std::string, "chart name", gr::Visible> chart_name;
    A<std::string, "chart title">             chart_title;
    A<bool, "show legend", gr::Visible>       show_legend   = true;
    A<bool, "show grid", gr::Visible>         show_grid     = true;
    A<bool, "anti-aliasing">                  anti_aliasing = true;
    A<int, "X-axis scale">                    x_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<int, "Y-axis scale">                    y_axis_scale  = static_cast<int>(AxisScale::Linear);
    A<bool, "X auto-scale">                   x_auto_scale  = true;
    A<bool, "Y auto-scale">                   y_auto_scale  = true;
    A<float, "X-axis min">                    x_min         = std::numeric_limits<float>::lowest();
    A<float, "X-axis max">                    x_max         = std::numeric_limits<float>::max();
    A<float, "Y-axis min">                    y_min         = std::numeric_limits<float>::lowest();
    A<float, "Y-axis max">                    y_max         = std::numeric_limits<float>::max();

    GR_MAKE_REFLECTABLE(YYChart, chart_name, chart_title, show_legend, show_grid, anti_aliasing, x_axis_scale, y_axis_scale, x_auto_scale, y_auto_scale, x_min, x_max, y_min, y_max);

    // Constructor (required for gr::Block)
    explicit YYChart(gr::property_map initParameters = {}) : gr::Block<YYChart, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>(std::move(initParameters)) {}

    // --- Non-reflectable runtime state ---
    std::vector<std::shared_ptr<SignalSink>> _signalSinks;
    std::string                              _uniqueId = generateUniqueId();

    // Drag & drop callbacks
    DropCallback       _dropCallback;
    std::string        _dropPayloadType;
    DragSourceCallback _dragSourceCallback;

    // --- Signal sink management ---

    void addSignalSink(std::shared_ptr<SignalSink> sink) override {
        if (!sink) {
            return;
        }
        auto it = std::find(_signalSinks.begin(), _signalSinks.end(), sink);
        if (it == _signalSinks.end()) {
            sink->requestCapacity(this, defaultHistorySize());
            _signalSinks.push_back(std::move(sink));
        }
    }

    void removeSignalSink(std::string_view uniqueName) override {
        auto it = std::find_if(_signalSinks.begin(), _signalSinks.end(), [uniqueName](const auto& s) { return s->uniqueName() == uniqueName; });
        if (it != _signalSinks.end()) {
            (*it)->releaseCapacity(this);
            _signalSinks.erase(it);
        }
    }

    void clearSignalSinks() override {
        for (auto& sink : _signalSinks) {
            sink->releaseCapacity(this);
        }
        _signalSinks.clear();
    }

    [[nodiscard]] const std::vector<std::shared_ptr<SignalSink>>& signalSinks() const noexcept override { return _signalSinks; }
    [[nodiscard]] std::size_t                                              signalSinkCount() const noexcept override { return _signalSinks.size(); }

    void setDashboardAxisConfig(std::vector<DashboardAxisConfig> /*configs*/) override { /* YYChart uses its own axis config */ }
    void clearDashboardAxisConfig() override { /* No-op for YYChart */ }

    // --- Drag & Drop ---
    void setDropCallback(DropCallback callback, const char* payloadType) override {
        _dropCallback    = std::move(callback);
        _dropPayloadType = payloadType ? payloadType : "";
    }
    void clearDropCallback() override {
        _dropCallback = nullptr;
        _dropPayloadType.clear();
    }
    void setDragSourceCallback(DragSourceCallback callback) override { _dragSourceCallback = std::move(callback); }
    void clearDragSourceCallback() override { _dragSourceCallback = nullptr; }

    // --- Public axis management (for DashboardPage integration) ---

    /**
     * @brief Build axis categories (no-op for YYChart - uses signal-based axes).
     */
    void buildAxisCategories() override { /* YYChart determines axes from signals */ }

    /**
     * @brief Setup all ImPlot axes (no-op - handled in draw()).
     */
    void setupAllAxes() override { /* Handled in draw() */ }

    // --- Drawable interface ---

    gr::work::Status draw(const gr::property_map& /*config*/ = {}) {
        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals");
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 1) {
            drawXYFallback();
            return gr::work::Status::OK;
        }

        if (_signalSinks.size() == 2) {
            drawCorrelation();
            return gr::work::Status::OK;
        }

        drawMultiCorrelation();
        return gr::work::Status::OK;
    }

    // --- ChartInterface rendering ---

    /**
     * @brief YYChart handles its own ImPlot context.
     */
    [[nodiscard]] bool handlesOwnPlotContext() const noexcept override { return true; }

    /**
     * @brief For YYChart, drawContent() calls the full draw() method.
     * This is because YYChart manages its own BeginPlot/EndPlot.
     */
    void drawContent() override { draw(); }

    // --- Identity ---
    [[nodiscard]] std::string_view chartTypeName() const noexcept override { return "YYChart"; }
    [[nodiscard]] std::string_view uniqueId() const noexcept override { return _uniqueId; }

private:
    static constexpr std::size_t defaultHistorySize() noexcept { return 4096; }

    static std::string generateUniqueId() {
        static std::atomic<std::size_t> counter{0};
        return "YYChart_" + std::to_string(counter++);
    }

    void drawEmptyPlot(const char* message) {
        if (ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), ImPlotFlags_NoTitle)) {
            ImPlot::SetupAxes("X", "Y");
            ImPlot::SetupFinish();
            auto limits = ImPlot::GetPlotLimits();
            ImPlot::PlotText(message, (limits.X.Min + limits.X.Max) / 2, (limits.Y.Min + limits.Y.Max) / 2);
            ImPlot::EndPlot();
        }
    }

    void drawXYFallback() {
        const auto* sink = _signalSinks[0].get();
        if (!sink) {
            drawEmptyPlot("No data");
            return;
        }

        // Skip if signal is hidden
        if (!sink->drawEnabled()) {
            drawEmptyPlot("Signal hidden");
            return;
        }

        // Acquire lock for thread-safe data access
        auto dataLock = sink->acquireDataLock();
        if (sink->size() == 0) {
            drawEmptyPlot("No data");
            return;
        }

        const ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), plotFlags)) {
            return;
        }

        ImPlot::SetupAxes("Time", std::string(sink->signalName()).c_str());
        setupAxisScales();
        ImPlot::SetupFinish();

        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sink->color()));
        ImPlot::SetNextLineStyle(lineColor);

        struct XYContext {
            const SignalSink* sink;
        };
        XYContext ctx{sink};

        ImPlot::PlotLineG(
            std::string(sink->signalName()).c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  c = static_cast<XYContext*>(userData);
                double x = c->sink->xAt(static_cast<std::size_t>(idx));
                double y = static_cast<double>(c->sink->yAt(static_cast<std::size_t>(idx)));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(sink->size()));

        handleInteractions();
        ImPlot::EndPlot();
    }

    void drawCorrelation() {
        const auto* sinkX = _signalSinks[0].get();
        const auto* sinkY = _signalSinks[1].get();

        if (!sinkX || !sinkY) {
            drawEmptyPlot("Invalid sinks");
            return;
        }

        // Skip if any signal is hidden
        if (!sinkX->drawEnabled() || !sinkY->drawEnabled()) {
            drawEmptyPlot("Signal hidden");
            return;
        }

        // Acquire locks for thread-safe data access
        auto lockX = sinkX->acquireDataLock();
        auto lockY = sinkY->acquireDataLock();

        std::size_t count = std::min(sinkX->size(), sinkY->size());
        if (count == 0) {
            drawEmptyPlot("No data");
            return;
        }

        const ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), plotFlags)) {
            return;
        }

        std::string xLabel = std::string(sinkX->signalName());
        std::string yLabel = std::string(sinkY->signalName());
        ImPlot::SetupAxes(xLabel.c_str(), yLabel.c_str());

        setupAxisScales();
        ImPlot::SetupFinish();

        ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkX->color()));
        ImPlot::SetNextLineStyle(lineColor);

        struct CorrelationContext {
            const SignalSink* sinkX;
            const SignalSink* sinkY;
        };
        CorrelationContext ctx{sinkX, sinkY};

        std::string label = std::string(sinkX->signalName()) + " vs " + std::string(sinkY->signalName());

        ImPlot::PlotLineG(
            label.c_str(),
            [](int idx, void* userData) -> ImPlotPoint {
                auto*  c = static_cast<CorrelationContext*>(userData);
                double x = static_cast<double>(c->sinkX->yAt(static_cast<std::size_t>(idx)));
                double y = static_cast<double>(c->sinkY->yAt(static_cast<std::size_t>(idx)));
                return ImPlotPoint(x, y);
            },
            &ctx, static_cast<int>(count));

        handleInteractions();
        ImPlot::EndPlot();
    }

    void drawMultiCorrelation() {
        const auto* sinkX = _signalSinks[0].get();
        if (!sinkX) {
            drawEmptyPlot("No X data");
            return;
        }

        // Skip if X signal is hidden (required for correlation)
        if (!sinkX->drawEnabled()) {
            drawEmptyPlot("X signal hidden");
            return;
        }

        // Acquire lock for X sink
        auto lockX = sinkX->acquireDataLock();
        if (sinkX->size() == 0) {
            drawEmptyPlot("No X data");
            return;
        }

        const ImPlotFlags plotFlags = ImPlotFlags_NoTitle;
        if (!ImPlot::BeginPlot(chart_name.value.c_str(), ImVec2(-1, -1), plotFlags)) {
            return;
        }

        std::string xLabel = std::string(sinkX->signalName());
        ImPlot::SetupAxes(xLabel.c_str(), "Y");

        setupAxisScales();
        ImPlot::SetupFinish();

        for (std::size_t i = 1; i < _signalSinks.size(); ++i) {
            const auto* sinkY = _signalSinks[i].get();
            if (!sinkY || !sinkY->drawEnabled()) {
                continue;
            }

            // Acquire lock for Y sink
            auto lockY = sinkY->acquireDataLock();

            std::size_t count = std::min(sinkX->size(), sinkY->size());
            if (count == 0) {
                continue;
            }

            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(sinkY->color()));
            ImPlot::SetNextLineStyle(lineColor);

            struct MultiContext {
                const SignalSink* sinkX;
                const SignalSink* sinkY;
            };
            MultiContext ctx{sinkX, sinkY};

            ImPlot::PlotLineG(
                std::string(sinkY->signalName()).c_str(),
                [](int idx, void* userData) -> ImPlotPoint {
                    auto*  c = static_cast<MultiContext*>(userData);
                    double x = static_cast<double>(c->sinkX->yAt(static_cast<std::size_t>(idx)));
                    double y = static_cast<double>(c->sinkY->yAt(static_cast<std::size_t>(idx)));
                    return ImPlotPoint(x, y);
                },
                &ctx, static_cast<int>(count));
        }

        handleInteractions();
        ImPlot::EndPlot();
    }

    void setupAxisScales() {
        if (static_cast<AxisScale>(x_axis_scale.value) == AxisScale::Log10) {
            ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
        }
        if (static_cast<AxisScale>(y_axis_scale.value) == AxisScale::Log10) {
            ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
        }

        if (!x_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_X1, static_cast<double>(x_min.value), static_cast<double>(x_max.value));
        }
        if (!y_auto_scale) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, static_cast<double>(y_min.value), static_cast<double>(y_max.value));
        }
    }

    void handleInteractions() {
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("YYChartContextMenu");
        }

        if (ImGui::BeginPopup("YYChartContextMenu")) {
            if (ImGui::MenuItem("Show Legend", nullptr, show_legend.value)) {
                show_legend = !show_legend.value;
            }
            if (ImGui::MenuItem("Show Grid", nullptr, show_grid.value)) {
                show_grid = !show_grid.value;
            }
            ImGui::EndPopup();
        }

        // Drag source handling - set up drag sources for legend items
        for (const auto& sink : _signalSinks) {
            std::string signalNameStr(sink->signalName());
            if (ImPlot::BeginDragDropSourceItem(signalNameStr.c_str())) {
                if (_dragSourceCallback) {
                    // Use external callback to set up payload (DashboardPage integration)
                    _dragSourceCallback(sink->uniqueName(), _uniqueId);
                } else {
                    // Internal drag source for standalone chart use
                    std::string uniqueNameStr(sink->uniqueName());
                    ImGui::SetDragDropPayload("SIGNAL_SINK", uniqueNameStr.data(), uniqueNameStr.size() + 1);
                }
                ImPlot::ItemIcon(sink->color());
                ImGui::SameLine();
                ImGui::TextUnformatted(signalNameStr.c_str());
                ImPlot::EndDragDropSource();
            }
        }

        // Handle drag & drop target
        if (ImPlot::BeginDragDropTargetPlot()) {
            // Check for external drop callback (e.g., from DashboardPage)
            if (_dropCallback && !_dropPayloadType.empty()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(_dropPayloadType.c_str())) {
                    _dropCallback(payload->Data, static_cast<std::size_t>(payload->DataSize), _dropPayloadType.c_str());
                }
            }
            ImPlot::EndDragDropTarget();
        }
    }
};

} // namespace opendigitizer::charts

// Register YYChart with the GR4 block registry
GR_REGISTER_BLOCK("opendigitizer::charts::YYChart", opendigitizer::charts::YYChart)

#endif // OPENDIGITIZER_CHARTS_YYCHART_HPP
