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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

struct ImFont;

namespace DigitizerUi {

struct SDLState;

enum class WindowMode {
    FULLSCREEN,
    MAXIMISED,
    MINIMISED,
    RESTORED
};

namespace detail {
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

    template<typename TScheduler>
    struct HandlerImpl : Handler {
        TScheduler        _scheduler;
        std::thread       _thread;
        std::atomic<bool> stopRequested = false;

        template<typename... Args>
        explicit HandlerImpl(Args &&...args)
            : _scheduler(std::forward<Args>(args)...) {
            _thread = std::thread([this]() {
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                    // TODO: handle error return message
                }
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                    // TODO: handle error return message
                }
                while (!stopRequested && _scheduler.isProcessing()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_STOP); !e) {
                    // TODO: handle error return message
                }
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::STOPPED); !e) {
                    // TODO: handle error return message
                }
            });
        }

        ~HandlerImpl() {
            stopRequested = true;
            _thread.join();
        }
    };

    std::unique_ptr<Handler> handler;
};
} // namespace detail

class App {
public:
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
    std::shared_ptr<gr::thread_pool::BasicThreadPool> schedulerThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>(
            "scheduler-pool", gr::thread_pool::CPU_BOUND, 4, 4);

    detail::SchedWrapper               _scheduler;
    Style                              _style = Style::Light;
    std::vector<std::function<void()>> _activeCallbacks;
    std::vector<std::function<void()>> _garbageCallbacks; // TODO: Cleaning up callbacks
    std::mutex                         _callbacksMutex;

public:
    App() noexcept { setStyle(Style::Light); }

    static App &instance() {
        static App app;
        return app;
    }

    void openNewWindow() {
#ifdef EMSCRIPTEN
        std::string script = fmt::format("window.open('{}').focus()", executable);
        ;
        emscripten_run_script(script.c_str());
#else
        if (fork() == 0) {
            execl(executable.c_str(), executable.c_str(), nullptr);
        }
#endif
    }

    void loadEmptyDashboard() {
        loadDashboard(DashboardDescription::createEmpty("New dashboard"));
    }

    void loadDashboard(const std::shared_ptr<DashboardDescription> &desc) {
        fgItem.clear();
        dashboard = Dashboard::create(desc);
        dashboard->load();
    }

    void loadDashboard(std::string_view url) {
        namespace fs = std::filesystem;
        fs::path path(url);

        auto     source = DashboardSource::get(path.parent_path().native());
        DashboardDescription::load(source, path.filename(),
                [this, source](std::shared_ptr<DashboardDescription> &&desc) {
                    if (desc) {
                        loadDashboard(desc);
                        openDashboardPage.addSource(source->path);
                    }
                });
    }

    void closeDashboard() { dashboard = {}; }

    void setStyle(Style style) {
        switch (style) {
        case Style::Dark:
            ImGui::StyleColorsDark();
            break;
        case Style::Light:
            ImGui::StyleColorsLight();
            break;
        }
        _style = style;
        fgItem.setStyle(style);
    }

    [[nodiscard]] inline Style style() const noexcept { return _style; }

    // schedule a function to be called at the next opportunity on the main thread
    void executeLater(std::function<void()> &&callback) {
        std::lock_guard lock(_callbacksMutex);
        _activeCallbacks.push_back(std::move(callback));
    }

    void fireCallbacks() {
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(_callbacksMutex);
            std::swap(callbacks, _activeCallbacks);
        }
        for (auto &cb : callbacks) {
            cb();
            _garbageCallbacks.push_back(std::move(cb));
        }
    }

    template<typename Graph>
    void assignScheduler(Graph &&graph) {
        using Scheduler = gr::scheduler::Simple<gr::scheduler::multiThreaded>;

        _scheduler.emplace<Scheduler>(std::forward<Graph>(graph), schedulerThreadPool);
    }
};

} // namespace DigitizerUi

#endif
