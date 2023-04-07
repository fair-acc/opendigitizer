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
    static App                &instance();
    void                       openNewWindow();
    void                       loadEmptyDashboard();
    void                       loadDashboard(const std::shared_ptr<DashboardDescription> &desc);
    void                       loadDashboard(std::string_view url);
    void                       closeDashboard();

    // schedule a function to be called at the next opportunity on the main thread
    void                               schedule(std::function<void()> &&callback);

    void                               fireCallbacks();

    std::string                executable;
    FlowGraph                  flowGraph;
    FlowGraphItem              fgItem;
    DashboardPage              dashboardPage;
    std::unique_ptr<Dashboard> dashboard;
    OpenDashboardPage          openDashboardPage;
    SDLState                  *sdlState;
    bool                       running    = true;

    ImFont                    *fontNormal = nullptr;
    ImFont                    *fontBig    = nullptr;
    ImFont                    *fontBigger = nullptr;
    ImFont                    *fontIcons;
    ImFont                    *fontIconsSolid;

    std::vector<std::function<void()>> m_callbacks[2];
    std::mutex                         m_callbacksMutex;
};

} // namespace DigitizerUi

#endif
