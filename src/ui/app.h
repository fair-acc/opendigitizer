#ifndef APP_H
#define APP_H

#include "dashboard.h"
#include "dashboardpage.h"
#include "flowgraph.h"
#include "flowgraphitem.h"
#include "opendashboardpage.h"

struct ImFont;

namespace DigitizerUi {

struct SDLState;

class App {
public:
    void                       openNewWindow();
    void                       loadEmptyDashboard();
    void                       loadDashboard(const std::shared_ptr<DashboardDescription> &desc);
    void                       loadDashboard(std::string_view url);
    void                       closeDashboard();

    std::string                executable;
    FlowGraph                  flowGraph;
    FlowGraphItem              fgItem;
    DashboardPage              dashboardPage;
    std::unique_ptr<Dashboard> dashboard;
    OpenDashboardPage          openDashboardPage;
    SDLState                  *sdlState;
    bool                       running = true;

    ImFont                    *font12  = nullptr;
    ImFont                    *font14  = nullptr;
    ImFont                    *font16  = nullptr;
    ImFont                    *fontIcons;
    ImFont                    *fontIconsSolid;
};

} // namespace DigitizerUi

#endif
