#ifndef APP_H
#define APP_H

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/AppHeader.hpp"

#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "FlowgraphItem.hpp"
#include "OpenDashboardPage.hpp"

#include <implot.h>

#include <gnuradio-4.0/CircularBuffer.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "common/AppDefinitions.hpp"

struct ImFont;

namespace DigitizerUi {

struct SDLState;

static constexpr LookAndFeel::Style kDefaultStyle = LookAndFeel::Style::Dark;

class App {
public:
    std::string                       executable;
    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        std::vector<std::filesystem::path> pluginPaths;
#ifndef __EMSCRIPTEN__
        // TODO set correct paths
        pluginPaths.push_back(std::filesystem::current_path() / "plugins");
#endif
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), pluginPaths);
    }();

    FlowGraphItem                fgItem;
    DashboardPage                dashboardPage;
    std::shared_ptr<Dashboard>   dashboard;
    OpenDashboardPage            openDashboardPage;
    SDLState*                    sdlState         = nullptr;
    bool                         running          = true;
    ViewMode                     mainViewMode     = ViewMode::VIEW;
    ViewMode                     previousViewMode = ViewMode::VIEW;
    std::vector<gr::BlockModel*> toolbarBlocks;

    components::AppHeader header;

    // The thread limit here is mainly for emscripten becaue the default thread pool will exhaust the browser's limits and be recreated for every new scheduler
    std::shared_ptr<gr::thread_pool::BasicThreadPool> schedulerThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>("scheduler-pool", gr::thread_pool::CPU_BOUND, 1, 1);

    struct SchedWrapper {
        template<typename T, typename... Args>
        void emplace(Args&&... args) {
            handler = std::make_unique<HandlerImpl<T>>(std::forward<Args>(args)...);
        }

        explicit operator bool() const { return handler != nullptr; };

        std::string_view uniqueName() const { return handler ? handler->uniqueName() : ""; }

        void sendMessage(const gr::Message& msg) { handler->sendMessage(msg); }
        void handleMessages(FlowGraph& fg) { handler->handleMessages(fg); }

    private:
        struct Handler {
            virtual ~Handler()                                           = default;
            virtual std::string_view uniqueName() const                  = 0;
            virtual void             sendMessage(const gr::Message& msg) = 0;
            virtual void             handleMessages(FlowGraph& fg)       = 0;
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

            void handleMessages(FlowGraph& fg) final {
                const auto available = _fromScheduler.streamReader().available();
                if (available > 0) {
                    auto messages = _fromScheduler.streamReader().get(available);

                    for (const auto& msg : messages) {
                        fg.handleMessage(msg);
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
    App() {
        BlockRegistry::instance().addBlockDefinitionsFromPluginLoader(*pluginLoader);
        setStyle(kDefaultStyle);
    }

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
        fgItem.clear();
        dashboard = Dashboard::create(&fgItem, desc);
        dashboard->setPluginLoader(pluginLoader);
        dashboard->load();
    }

    void loadDashboard(std::string_view url) {
        namespace fs = std::filesystem;
        fs::path path(url);

        auto source = DashboardSource::get(path.parent_path().native());
        DashboardDescription::load(source, path.filename(), [this, source](std::shared_ptr<DashboardDescription>&& desc) {
            if (desc) {
                loadDashboard(desc);
                openDashboardPage.addSource(source->path);
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
        fgItem.setStyle(style);
    }

    template<typename Graph>
    void assignScheduler(Graph&& graph) {
        using Scheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>;

        _scheduler.emplace<Scheduler>(std::forward<Graph>(graph), schedulerThreadPool);
    }

    std ::string_view schedulerUniqueName() const { return _scheduler.uniqueName(); }

    void sendMessage(const gr::Message& msg) {
        if (_scheduler) {
            _scheduler.sendMessage(msg);
        }
    }

    void handleMessages(FlowGraph& fg) {
        if (_scheduler) {
            _scheduler.handleMessages(fg);
        }
    }
};

} // namespace DigitizerUi

#endif
