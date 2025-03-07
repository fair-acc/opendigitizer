#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <stack>
#include <string>
#include <vector>

#include "Dashboard.hpp"
#include "common/ImguiWrap.hpp"
#include "components/Block.hpp"
#include "components/Docking.hpp"
#include "components/SignalSelector.hpp"

#include <memory.h>

namespace DigitizerUi {

class DashboardPage {
public:
private:
    ImVec2 pane_size{0, 0};     // updated by drawPlots(...)
    ImVec2 legend_box{500, 40}; // updated by drawLegend(...)

private:
    static constexpr inline auto* dnd_type = "DND_SOURCE";

public:
    enum class Mode { View, Layout };
    struct DndItem {
        Dashboard::Plot*                plot;
        opendigitizer::ImPlotSinkModel* plotSource;
    };

public:
    DashboardPage()
#ifndef OPENDIGITIZER_TEST
        // SignalSelector triggers RemoteSignalSources which uses opencmw::client::ClientContext
        // making self-contained tests difficult to write. Everything is tightly coupled and
        // this is the best place to break the dependency.
        : m_signalSelector(std::make_unique<SignalSelector>())
#endif
    {
    }

    void draw(Dashboard& dashboard, Mode mode = Mode::View) noexcept;
    void newPlot(Dashboard& dashboard);
    void setLayoutType(DockingLayoutType);

private:
    void        drawPlots(Dashboard& dashboard, DigitizerUi::DashboardPage::Mode mode);
    void        drawGrid(float w, float h);
    void        drawLegend(Dashboard& dashboard, const Mode& mode) noexcept;
    static void drawPlot(Dashboard& dashboard, DigitizerUi::Dashboard::Plot& plot) noexcept;

    DockSpace                             m_dockSpace;
    components::BlockControlsPanelContext m_editPane;
    std::unique_ptr<SignalSelector>       m_signalSelector;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
