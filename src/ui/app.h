#ifndef APP_H
#define APP_H

#include <function2/function2.hpp>

#include "common.h"
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
    static App  &instance();
    void         openNewWindow();
    void         loadEmptyDashboard();
    void         loadDashboard(const std::shared_ptr<DashboardDescription> &desc);
    void         loadDashboard(std::string_view url);
    void         closeDashboard();

    void         setStyle(Style style);
    inline Style style() const { return m_style; }

    // schedule a function to be called at the next opportunity on the main thread
    void                       schedule(fu2::unique_function<void()> &&callback);

    void                       fireCallbacks();

    std::string                executable;
    FlowGraphItem              fgItem;
    DashboardPage              dashboardPage;
    std::unique_ptr<Dashboard> dashboard;
    OpenDashboardPage          openDashboardPage;
    SDLState                  *sdlState;
    bool                       running    = true;

    float                      defaultDPI= 76.2f;
    float                      verticalDPI= defaultDPI;
    ImFont                    *fontNormal = nullptr;
    ImFont                    *fontBig    = nullptr;
    ImFont                    *fontBigger = nullptr;
    ImFont                    *fontLarge  = nullptr;
    ImFont                    *fontIcons;
    ImFont                    *fontIconsSolid;

private:
    App();

    Style                                     m_style = Style::Light;
    std::vector<fu2::unique_function<void()>> m_callbacks[2];
    std::mutex                                m_callbacksMutex;
};

} // namespace DigitizerUi

#endif
