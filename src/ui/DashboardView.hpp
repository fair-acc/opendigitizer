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

namespace gr::od {

/// draws a dashboard's charts, docked as its layout describes, and the signal legend below them
struct DashboardView {
    using Mode           = charts::ChartMode;
    using LegendPosition = DashboardStyle::LegendPosition;

    /// all items of the bar, including those drawn by the Options callbacks, are submitted inside this ID scope
    static constexpr const char* kBarIdScope = "Bar";

    struct Options {
        ImVec2                                   size{0.f, 0.f}; // 0: the available region
        std::function<void()>                    barLeading;
        std::function<void()>                    barCentre;
        std::function<void()>                    barTrailing;
        float                                    barTrailingWidth = 0.f; // how much space to reserve for buttons to the right of the legend, in the bottom bar
        std::optional<float>                     addSignalButtonHidden;  // how hidden the Add Signal button is, or null if no button is being drawn
        std::function<void(DockSpace::Windows&)> addDockWindows;
    };

    struct Result {
        std::string rightClickedSinkName;
        bool        backgroundClicked        = false;
        bool        isAddSignalButtonPressed = false;
        ImRect      addSignalButtonRect; // refers to where the button *would* be if not hidden
    };

    struct PendingTransmutation {
        std::string chartId;
        std::string newChartType;
    };

    DockSpace                           _dockSpace;
    GlobalSignalLegend                  _signalLegend;
    ImVec2                              _legendBox{500, 40};
    float                               _legendColumnWidth = 150.f;
    Dashboard*                          _dashboard         = nullptr;
    bool                                _layoutApplied     = false;
    std::optional<PendingTransmutation> _pendingTransmutation;
    std::vector<std::string>            _pendingRemovals;

    // deferred: a chart must not be replaced while it draws
    charts::ChartRequests _chartRequests{
        .transmute = [this](std::string_view chartId, std::string_view newChartType) { _pendingTransmutation = PendingTransmutation{std::string(chartId), std::string(newChartType)}; },
        .duplicate = [this](std::string_view chartId) { _dashboard->copyChart(chartId); },
        .remove    = [this](std::string_view chartId) { _pendingRemovals.emplace_back(chartId); },
    };

    DashboardView();
    ~DashboardView();
    DashboardView(const DashboardView&)            = delete; // gChartRequests points into this object
    DashboardView& operator=(const DashboardView&) = delete;

    Result draw(Dashboard& dashboard, Mode mode);
    Result draw(Dashboard& dashboard, Mode mode, const Options& options);

    [[nodiscard]] const DockSpace& dockSpace() const noexcept { return _dockSpace; }

    void setLayout(DockingLayoutType type, const std::optional<gr::property_map>& freeLayoutDescription);

    [[nodiscard]] static DockingLayoutType shownLayoutType(DockingLayoutType stored);

    void        applyChartRequests();
    void        drawCharts(Mode mode, const Options& options, ImVec2 paneSize);
    void        drawBar(Mode mode, const Options& options, Result& result);
    void        drawLegendColumn(Mode mode, float height, const Options& options, Result& result);
    static void takeLegendResult(Mode mode, const GlobalSignalLegend::DrawResult& legendResult, Result& result);
};

void alignForWidth(float width, float alignment = 0.5f) noexcept;

} // namespace gr::od

#endif // OPENDIGITIZER_UI_DASHBOARDVIEW_HPP
