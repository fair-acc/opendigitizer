#ifndef OPENDIGITIZER_UI_DASHBOARDVIEW_HPP
#define OPENDIGITIZER_UI_DASHBOARDVIEW_HPP

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Dashboard.hpp"
#include "charts/Chart.hpp"
#include "components/Docking.hpp"
#include "components/GlobalSignalLegend.hpp"

namespace DigitizerUi {

/// Draws a dashboard: its charts, docked as the dashboard's layout describes, and the signal legend in a bar below
/// them, inside a child window of the given size. Used by the App's DashboardPage and by applications that embed a
/// dashboard; the App adds its own controls through the bar slots and extra dock windows.
/// @code
/// DigitizerUi::DashboardView view;            // one per drawn dashboard; receives the charts' context-menu requests
/// view.draw(*dashboard, DigitizerUi::DashboardView::Mode::View);
/// @endcode
class DashboardView {
public:
    using Mode = opendigitizer::charts::ChartMode;

    enum class LegendPosition { Bottom, Top, None };

    struct Options {
        ImVec2                                   size{0.f, 0.f}; // 0: the available region
        LegendPosition                           legend = LegendPosition::Bottom;
        std::function<void()>                    barLeading;     // drawn before the legend
        std::function<void()>                    barCentre;      // drawn instead of the legend
        std::function<void()>                    barTrailing;    // drawn after the legend
        std::function<void(DockSpace::Windows&)> addDockWindows; // further windows docked with the charts
    };

    struct Result {
        ImVec2      chartPaneSize;
        std::string rightClickedSinkName; // a legend entry right-clicked in Interaction mode
        bool        backgroundClicked = false;
    };

    DashboardView();
    ~DashboardView();
    DashboardView(const DashboardView&)            = delete; // g_chartRequests points into this object
    DashboardView& operator=(const DashboardView&) = delete;

    Result draw(Dashboard& dashboard, Mode mode);
    Result draw(Dashboard& dashboard, Mode mode, const Options& options);

    /// overrides the layout stored in the dashboard, which is applied otherwise when the dashboard is first drawn
    void setLayout(DockingLayoutType type, const std::optional<gr::property_map>& freeLayoutDescription);

    [[nodiscard]] DockSpace&       dockSpace() noexcept { return _dockSpace; }
    [[nodiscard]] const DockSpace& dockSpace() const noexcept { return _dockSpace; }

private:
    struct PendingTransmutation {
        std::string chartId;
        std::string newChartType;
    };

    DockSpace                           _dockSpace;
    GlobalSignalLegend                  _signalLegend;
    ImVec2                              _legendBox{500, 40}; // size of the bar's centre part in the last frame
    Dashboard*                          _dashboard     = nullptr;
    bool                                _layoutApplied = false;
    std::optional<PendingTransmutation> _pendingTransmutation;
    std::vector<std::string>            _pendingRemovals;

    // transmutation and removal are deferred to the next frame: a chart must not be replaced while it draws
    opendigitizer::charts::ChartRequests _chartRequests{
        .transmute =
            [this](std::string_view chartId, std::string_view newChartType) {
                _pendingTransmutation = PendingTransmutation{std::string(chartId), std::string(newChartType)};
                return true;
            },
        .duplicate = [this](std::string_view chartId) { _dashboard->copyChart(chartId); },
        .remove    = [this](std::string_view chartId) { _pendingRemovals.emplace_back(chartId); },
    };

    void processPendingRequests();
    void drawCharts(Mode mode, const Options& options, ImVec2 paneSize);
    void drawBar(Mode mode, ImVec2 chartPaneSize, const Options& options, Result& result);
};

/// moves the cursor so that an item of the given width is aligned within the remaining width (0: left, 1: right)
void alignForWidth(float width, float alignment = 0.5f) noexcept;

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_DASHBOARDVIEW_HPP
