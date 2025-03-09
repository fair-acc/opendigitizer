#ifndef APP_H
#define APP_H

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/AppHeader.hpp"

#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "FlowgraphItem.hpp"
#include "OpenDashboardPage.hpp"

#include "settings.hpp"

#include <implot.h>

#include <gnuradio-4.0/CircularBuffer.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "common/AppDefinitions.hpp"

namespace DigitizerUi {

struct SDLState;

class App {
public:
    std::string executable;

    std::unique_ptr<Dashboard> dashboard;

    Dashboard*                     loadedDashboard = nullptr;
    std::unique_ptr<DashboardPage> dashboardPage;

    FlowGraphItem     flowgraphPage;
    OpenDashboardPage openDashboardPage;

    SDLState* sdlState         = nullptr;
    bool      running          = true;
    ViewMode  mainViewMode     = ViewMode::VIEW;
    ViewMode  previousViewMode = ViewMode::VIEW;

    std::vector<gr::BlockModel*> toolbarBlocks;

    components::AppHeader header;

    // The thread limit here is mainly for emscripten because the default thread pool will exhaust the browser's limits and be recreated for every new scheduler
    std::shared_ptr<gr::thread_pool::BasicThreadPool> schedulerThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>("scheduler-pool", gr::thread_pool::CPU_BOUND, 1, 1);

    struct SchedWrapper {
        template<typename T, typename... Args>
        void emplace(Args&&... args) {
            handler = std::make_unique<HandlerImpl<T>>(std::forward<Args>(args)...);
        }

        explicit operator bool() const { return handler != nullptr; };

        std::string_view uniqueName() const { return handler ? handler->uniqueName() : ""; }

        void sendMessage(const gr::Message& msg) { handler->sendMessage(msg); }
        void handleMessages(UiGraphModel& graphModel) { handler->handleMessages(graphModel); }

    private:
        struct Handler {
            virtual ~Handler()                                           = default;
            virtual std::string_view uniqueName() const                  = 0;
            virtual void             sendMessage(const gr::Message& msg) = 0;
            virtual void             handleMessages(UiGraphModel& fg)    = 0;
        };

        template<typename TScheduler>
        struct HandlerImpl : Handler {
            TScheduler  _scheduler;
            std::thread _thread;

            gr::MsgPortIn  _fromScheduler;
            gr::MsgPortOut _toScheduler;

            template<typename... Args>
            explicit HandlerImpl(Args&&... args) : _scheduler(std::forward<Args>(args)...) {
                if (_toScheduler.connect(_scheduler.msgIn) != gr::ConnectionResult::SUCCESS) {
                    throw fmt::format("Failed to connect _toScheduler -> _scheduler.msgIn\n");
                }
                if (_scheduler.msgOut.connect(_fromScheduler) != gr::ConnectionResult::SUCCESS) {
                    throw fmt::format("Failed to connect _scheduler.msgOut -> _fromScheduler\n");
                }
                gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {}, "UI");
                gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");
                gr::sendMessage<gr::message::Command::Get>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");

                _thread = std::thread([this]() {
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                        throw fmt::format("Failed to initialize flowgraph");
                    }
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                        throw fmt::format("Failed to start flowgraph processing");
                    }
                    // NOTE: the single threaded scheduler runs its main loop inside its start() function and only returns after its state changes to non-active
                    // We once have to directly change the state to running, after this, all further state updates are performed via the msg API
                });
            }

            std::string_view uniqueName() const override { return _scheduler.unique_name; }

            void sendMessage(const gr::Message& msg) final {
                auto output = _toScheduler.streamWriter().reserve<gr::SpanReleasePolicy::ProcessAll>(1UZ);
                output[0]   = msg;
            }

            void handleMessages(UiGraphModel& graphModel) final {
                const auto available = _fromScheduler.streamReader().available();
                if (available > 0) {
                    auto messages = _fromScheduler.streamReader().get(available);

                    for (const auto& msg : messages) {
                        graphModel.processMessage(msg);
                    }
                    std::ignore = messages.consume(available);
                }
            }

            ~HandlerImpl() {
                gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_STOP))}}, "UI");
                _thread.join();
            }
        };

        std::unique_ptr<Handler> handler;
    };

    SchedWrapper _scheduler;

public:
    App() { setStyle(Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light); }

    static App& instance() {
        static App app;
        return app;
    }

    void openNewWindow() {
#ifdef EMSCRIPTEN
        std::string script = fmt::format("window.open('{}').focus()", executable);
        emscripten_run_script(script.c_str());
#else
        if (fork() == 0) {
            execl(executable.c_str(), executable.c_str(), nullptr);
        }
#endif
    }

    void loadEmptyDashboard() { loadDashboard(DashboardDescription::createEmpty("New dashboard")); }

    void loadDashboard(const std::shared_ptr<DashboardDescription>& desc) {
        if (dashboard && dashboard->inUse) {
            fmt::print("The current dashboard can not yet be disposed of\n");
            std::this_thread::sleep_for(500ms);
        }
        dashboard = Dashboard::create(&flowgraphPage, desc);
        dashboard->load();
    }

    void loadDashboard(std::string_view url) {
        namespace fs = std::filesystem;
        fs::path path(url);

        auto storageInfo = DashboardStorageInfo::get(path.parent_path().native());
        DashboardDescription::loadAndThen(storageInfo, path.filename(), [this, storageInfo](std::shared_ptr<DashboardDescription>&& desc) {
            if (desc) {
                loadDashboard(desc);
                openDashboardPage.addDashboard(storageInfo->path);
            }
        });
    }

    void closeDashboard() { dashboard = {}; }

    static void setImGuiStyle(LookAndFeel::Style style) {
        switch (style) {
        case LookAndFeel::Style::Dark: ImGui::StyleColorsDark(); break;
        case LookAndFeel::Style::Light: ImGui::StyleColorsLight(); break;
        }
        LookAndFeel::mutableInstance().style = style;

        // with the dark style the plot frame would have the same color as a button. make it have the
        // same color as the window background instead.
        ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    }

    void setStyle(LookAndFeel::Style style) {
        setImGuiStyle(style);
        flowgraphPage.setStyle(style);
    }

    template<typename Graph>
    void assignScheduler(Graph&& graph) {
        using Scheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>;

        _scheduler.emplace<Scheduler>(std::forward<Graph>(graph), schedulerThreadPool);
    }

    std ::string_view schedulerUniqueName() const { return _scheduler.uniqueName(); }

    void sendMessage(const gr::Message& msg) {
        if (_scheduler) {
            _scheduler.sendMessage(msg);
        }
    }

    void handleMessages(UiGraphModel& fg) {
        if (_scheduler) {
            _scheduler.handleMessages(fg);
        }
    }

    void init(int argc, char** argv) {
        Digitizer::Settings& settings = Digitizer::Settings::instance();

        // Init openDashboardPage
        openDashboardPage.requestCloseDashboard = [&] { closeDashboard(); };
        openDashboardPage.requestLoadDashboard  = [&](const auto& desc) {
            if (!desc) {
                loadEmptyDashboard();
            } else {
                loadDashboard(desc);
                mainViewMode = ViewMode::VIEW;
            }
        };
        openDashboardPage.addDashboard(settings.serviceUrl().path("/dashboards").build().str());
        openDashboardPage.addDashboard("example://builtin-samples");

        // Flowgraph page
        flowgraphPage.requestBlockControlsPanel = [&](components::BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& size, bool horizontalSplit) {
            components::BlockControlsPanel(panelContext, pos, size, horizontalSplit);
            //
        };

        // Init header
        header.requestApplicationSwitchMode  = [this](ViewMode mode) { mainViewMode = mode; };
        header.requestApplicationSwitchTheme = [this](LookAndFeel::Style style) { setStyle(style); };
        header.requestApplicationStop        = [this] { running = false; };
        header.loadAssets();

        if (argc > 1) { // load dashboard if specified on the command line/query parameter
            const char* url = argv[1];
            if (strlen(url) > 0) {
                fmt::print("Loading dashboard from '{}'\n", url);
                loadDashboard(url);
            }
        } else if (!settings.defaultDashboard.empty()) {
            // TODO: add subscription to remote dashboard worker if needed
            std::string dashboardPath = settings.defaultDashboard;
            if (!dashboardPath.starts_with("http://") and !dashboardPath.starts_with("https://")) { // if the default dashboard does not contain a host, use the default
                dashboardPath = fmt::format("{}://{}:{}/dashboards/{}", settings.disableHttps ? "http" : "https", settings.hostname, settings.port, dashboardPath);
            }
            loadDashboard(dashboardPath);
        }
        if (auto firstDashboard = openDashboardPage.get(0); dashboard == nullptr && firstDashboard != nullptr) { // load first dashboard if there is a dashboard available
            loadDashboard(firstDashboard);
        }
    }

    void mainLoop() {}
};

} // namespace DigitizerUi

#endif
