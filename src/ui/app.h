#ifndef APP_H
#define APP_H

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#include "common.h"
#include "dashboard.h"
#include "dashboardpage.h"
#include "flowgraphitem.h"
#include "opendashboardpage.h"

#include <gnuradio-4.0/Scheduler.hpp>

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
    bool                       running    = true;
    WindowMode                 windowMode = WindowMode::RESTORED;
    std::string                mainViewMode{};

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
    // The thread limit here is mainly for emscripten
    std::shared_ptr<gr::thread_pool::BasicThreadPool> schedulerThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>("scheduler-pool", gr::thread_pool::CPU_BOUND, 2, 4);

    template<typename Graph>
    void assignScheduler(Graph &&graph) {
        using Scheduler = gr::scheduler::Simple<gr::scheduler::multiThreaded>;

        m_scheduler.emplace<Scheduler>(std::forward<Graph>(graph), schedulerThreadPool);
    }

private:
    struct SchedWrapper {
        template<typename T, typename... Args>
        void emplace(Args &&...args) {
            handler = std::make_unique<HandlerImpl<T>>(std::forward<Args>(args)...);
        }
        explicit operator bool() const { return handler != nullptr; };

    private:
        struct Handler {
            virtual ~Handler() = default;
        };
        template<typename T>
        struct HandlerImpl : Handler {
            T                 data;
            std::thread       thread;
            std::atomic<bool> stopRequested = false;

            template<typename... Args>
            explicit HandlerImpl(Args &&...args)
                : data(std::forward<Args>(args)...) {
                thread = std::thread([this]() {
                    data.init();
                    data.start();
                    while (!stopRequested && data.isProcessing()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    data.stop();
                });
            }
            ~HandlerImpl() {
                stopRequested = true;
                thread.join();
            }
        };

        std::unique_ptr<Handler> handler;
    };

    SchedWrapper m_scheduler;

    App();

    Style                              m_style = Style::Light;
    std::vector<std::function<void()>> m_activeCallbacks;
    std::vector<std::function<void()>> m_garbageCallbacks; // TODO: Cleaning up callbacks
    std::mutex                         m_callbacksMutex;
};

} // namespace DigitizerUi

#endif
