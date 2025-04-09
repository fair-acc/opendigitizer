#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <deque>
#include <stack>
#include <string>
#include <unordered_map>

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

    static constexpr inline auto* dnd_type = "DND_SOURCE";

    // Signals which are schedulerd to be added
    // (source block creation requested)
    std::unordered_map<std::string, SignalData> _addingRemoteSignals;

    // Source blocks which are added, and are
    // waiting for the plot sinks to be created
    struct SourceBlockInWaiting {
        SignalData  signalData;
        std::string sourceBlockName;
    };
    std::unordered_map<std::string, SourceBlockInWaiting> _addedSourceBlocksWaitingForSink;

public:
    enum class Mode { View, Layout };
    struct DndItem {
        Dashboard::Plot*                plot;
        opendigitizer::ImPlotSinkModel* plotSource;
    };

public:
    DashboardPage();
    ~DashboardPage();

    void draw(Mode mode = Mode::View) noexcept;
    void setLayoutType(DockingLayoutType);

    /* no optional of ref yet */ DigitizerUi::Dashboard::Plot* newPlot();

    void setDashboard(Dashboard& dashboard) {
        m_dashboard = std::addressof(dashboard);
#ifndef OPENDIGITIZER_TEST
        // SignalSelector triggers RemoteSignalSources which uses opencmw::client::ClientContext
        // making self-contained tests difficult to write. Everything is tightly coupled and
        // this is the best place to break the dependency.
        m_remoteSignalSelector = std::make_unique<SignalSelector>(dashboard.graphModel());
#endif
    }

private:
    void drawPlots(DigitizerUi::DashboardPage::Mode mode);
    void drawGrid(float w, float h);
    void drawGlobalLegend(const Mode& mode) noexcept;
    void drawPlot(DigitizerUi::Dashboard::Plot& plot) noexcept;

    DockSpace                             m_dockSpace;
    components::BlockControlsPanelContext m_editPane;
    std::unique_ptr<SignalSelector>       m_remoteSignalSelector;

    Dashboard* m_dashboard = nullptr;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
