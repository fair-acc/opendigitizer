#ifndef OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP
#define OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "Docking.hpp"

namespace gr::od {

// the chart layout of a dashboard's yaml, drawn as a preview on the dashboard page before the charts load
struct DashboardPreview {
    using Rect = LayoutRect;

    // Chart2D accounts for both XYChart and YYChart
    enum class ChartType { SpectrumDensity, SpectrumPlot, SpectrumView, WaterfallPlot, SurfacePlot, Chart2D, Other };

    static constexpr float kPreferredAspectRatio = 1.6f;

    struct Signal {
        std::uint32_t color     = 0U; // rgb, 0 == unknown
        bool          isDataSet = false;

        bool operator==(const Signal&) const = default;
    };

    struct Chart {
        ChartType           type = ChartType::Other;
        std::vector<Signal> signals;
        /// absolute pixels for a chart in a floating window, else normalised to the screen
        std::optional<Rect> rect;
        bool                isFloating = false;
    };

    std::vector<Chart> charts;

    /// returns null if the yaml was invalid
    [[nodiscard]] static std::optional<DashboardPreview> fromFlowgraphYaml(std::string_view flowgraphYaml);

    /// draws the preview using the current window's draw list. clipped to topLeft + bottomRight
    void draw(ImVec2 topLeft, ImVec2 bottomRight) const;
};
} // namespace gr::od

#endif // OPENDIGITIZER_UI_COMPONENTS_DASHBOARDPREVIEW_HPP
