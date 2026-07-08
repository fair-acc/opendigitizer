#ifndef OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP
#define OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <imgui.h>

namespace DigitizerUi {

/**
 * A description of how charts are expected to be laid out, based on yaml.
 * Can be drawn, which is used to show previews in the dashboard page before
 * charts are loaded.
 */
struct DashboardPreview {
    struct Rect {
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
    };

    // the known chart types, named as their registered types; Chart2D covers XYChart and YYChart
    enum class ChartType { SpectrumDensity, SpectrumPlot, SpectrumView, WaterfallPlot, SurfacePlot, Chart2D, Other };

    // aspect ratio that usually looks good
    static constexpr float preferredAspectRatio = 1.6f;

    struct Signal {
        std::uint32_t color     = 0U; // rgb, 0 == unknown
        bool          isDataSet = false;

        bool operator==(const Signal&) const = default;
    };

    struct Chart {
        ChartType           type = ChartType::Other;
        std::vector<Signal> signals;
        /// if the chart is in a floating window then this rect is absolute pixels,
        /// otherwised it is normalized / a percentage of the screen
        std::optional<Rect> rect;
        bool                isFloating = false;
    };

    std::vector<Chart> charts;

    /// returns null if the yaml was invalid
    [[nodiscard]] static std::optional<DashboardPreview> fromFlowgraphYaml(std::string_view flowgraphYaml);

    /// draws the preview using the current window's draw list. clipped to topLeft + bottomRight
    void draw(ImVec2 topLeft, ImVec2 bottomRight) const;
};
} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP
