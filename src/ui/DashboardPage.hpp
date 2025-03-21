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

    void draw(Mode mode = Mode::View) noexcept;
    void newPlot();
    void setLayoutType(DockingLayoutType);

    void setDashboard(Dashboard& dashboard) {
        m_dashboard = std::addressof(dashboard);
        if (m_signalSelector) {
            m_signalSelector->setGraphModel(dashboard.graphModel());
        }
    }

private:
    void drawPlots(DigitizerUi::DashboardPage::Mode mode);
    void drawGrid(float w, float h);
    void drawLegend(const Mode& mode) noexcept;
    void drawPlot(DigitizerUi::Dashboard::Plot& plot) noexcept;

    DockSpace                             m_dockSpace;
    components::BlockControlsPanelContext m_editPane;
    std::unique_ptr<SignalSelector>       m_signalSelector;

    Dashboard* m_dashboard = nullptr;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
