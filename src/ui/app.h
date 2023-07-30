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

enum class WindowMode {
    FULLSCREEN,
    MAXIMISED,
    MINIMISED,
    RESTORED
};

struct App {
    static App  &instance();
    void         openNewWindow();
    void         loadEmptyDashboard();
    void         loadDashboard(const std::shared_ptr<DashboardDescription> &desc);
    void         loadDashboard(std::string_view url);
    void         closeDashboard();

    void         setStyle(Style style);
    inline Style style() const { return m_style; }

    // schedule a function to be called at the next opportunity on the main thread
    void schedule(fu2::unique_function<void()> &&callback);

    void fireCallbacks();

#ifdef __EMSCRIPTEN__
    const bool isDesktop = false;
#else
    const bool isDesktop = true;
#endif
    std::string                executable;
    FlowGraphItem              fgItem;
    DashboardPage              dashboardPage;
    std::unique_ptr<Dashboard> dashboard;
    OpenDashboardPage          openDashboardPage;
    SDLState                  *sdlState;
    bool                       running          = true;
    WindowMode                 windowMode       = WindowMode::RESTORED;
    std::string                mainViewMode     = "";

    bool                       prototypeMode    = true;
    bool                       touchDiagnostics = false;
    std::chrono::milliseconds  execTime; /// time it took to handle events and draw one frame
    float                      defaultDPI  = 76.2f;
    float                      verticalDPI = defaultDPI;
    std::array<ImFont *, 2>    fontNormal  = { nullptr, nullptr }; /// default font [0] production [1] prototype use
    std::array<ImFont *, 2>    fontBig     = { nullptr, nullptr }; /// 0: production 1: prototype use
    std::array<ImFont *, 2>    fontBigger  = { nullptr, nullptr }; /// 0: production 1: prototype use
    std::array<ImFont *, 2>    fontLarge   = { nullptr, nullptr }; /// 0: production 1: prototype use
    ImFont                    *fontIcons;
    ImFont                    *fontIconsBig;
    ImFont                    *fontIconsLarge;
    ImFont                    *fontIconsSolid;
    ImFont                    *fontIconsSolidBig;
    ImFont                    *fontIconsSolidLarge;
    std::chrono::seconds       editPaneCloseDelay{ 15 };

private:
    App();

    Style                                     m_style = Style::Light;
    std::vector<fu2::unique_function<void()>> m_callbacks[2];
    std::mutex                                m_callbacksMutex;
};

} // namespace DigitizerUi

#endif
