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
    void executeLater(std::function<void()> &&callback);

    void fireCallbacks();

#ifdef __EMSCRIPTEN__
    const bool isDesktop = false;
#else
    const bool isDesktop = true;
#endif
    std::string                executable;
    FlowGraphItem              fgItem;
    DashboardPage              dashboardPage;
    std::shared_ptr<Dashboard> dashboard;
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

    template <typename Scheduler, typename Graph>
    void assignScheduler(Graph&& graph) {
        if (m_scheduler) {
            m_garbageSchedulers.push_back(std::move(m_scheduler));
        }
        m_scheduler.emplace<Scheduler>(std::forward<Graph>(graph));
    }

    bool runScheduler() {
        if (m_scheduler) {
            m_scheduler.run();
            return true;
        }
        return false;
    }

private:
    struct SchedWrapper {
        SchedWrapper() {}

        template<typename T, typename... Args>
        void emplace(Args &&...args) {
            handler = std::make_unique<HandlerImpl<T>>(std::forward<Args>(args)...);
        }
        void run() { handler->run(); }
        operator bool() const { return handler.get() != nullptr; };

    private:
        struct Handler {
            virtual ~Handler() = default;
            virtual void run() = 0;
        };
        template<typename T>
        struct HandlerImpl : Handler {
            T data;
            template<typename... Args>
            HandlerImpl(Args &&...args)
                : data(std::forward<Args>(args)...) {
                data.init();
            }
            void run() final { data.runAndWait(); }
        };

        std::unique_ptr<Handler> handler;
    };

    SchedWrapper m_scheduler;
    std::vector<SchedWrapper> m_garbageSchedulers; // TODO: Cleaning up schedulers needs support in opencmw to return unsubscription confirmation

    App();

    Style                              m_style = Style::Light;
    std::vector<std::function<void()>> m_activeCallbacks;
    std::vector<std::function<void()>> m_garbageCallbacks; // TODO: Cleaning up callbacks
    std::mutex                         m_callbacksMutex;
};

} // namespace DigitizerUi

#endif
