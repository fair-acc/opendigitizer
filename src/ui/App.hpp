#ifndef APP_H
#define APP_H

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/AppHeader.hpp"

#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "FlowgraphPage.hpp"
#include "OpenDashboardPage.hpp"

#include "settings.hpp"

#include "components/AppHeader.hpp"
#include "components/Toolbar.hpp"

#include <implot.h>

#include <gnuradio-4.0/CircularBuffer.hpp>
#include <gnuradio-4.0/Message.hpp>

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

    FlowgraphPage     flowgraphPage;
    OpenDashboardPage openDashboardPage;

    SDLState* sdlState         = nullptr;
    bool      isRunning        = true;
    ViewMode  mainViewMode     = ViewMode::VIEW;
    ViewMode  previousViewMode = ViewMode::VIEW;

    std::vector<gr::BlockModel*> toolbarBlocks;

    components::AppHeader header;

public:
    App() { setStyle(Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light); }

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
        auto startedAt = std::chrono::system_clock::now();

        if (dashboard) {
            while (true) {
                bool wait = false;
                if (dashboard->isInUse) {
                    wait = true;
                } else if (dashboard->scheduler() && dashboard->scheduler()->state() != gr::lifecycle::State::STOPPED) {
                    dashboard->scheduler()->stop();
                    wait = true;
                }

                if (!wait) {
                    break;
                }

                if (std::chrono::system_clock::now() - startedAt > 1s) {
                    components::Notification::error("Failed to stop current flowgraph, can not load the dashboard.");
                    return;
                }
            }
        }

        dashboard = Dashboard::create(desc);
        dashboard->load();
        dashboard->requestClose = [this](Dashboard*) { closeDashboard(); };

        flowgraphPage.setDashboard(dashboard.get());
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
        header.requestApplicationStop        = [this] { isRunning = false; };
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

    void processAndRender() {
        {
            if (dashboard) {
                dashboard->handleMessages();
            }

            IMW::Window window("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

            const char* title = dashboard ? dashboard->description()->name.data() : "OpenDigitizer";
            header.draw(title, LookAndFeel::instance().fontLarge[LookAndFeel::instance().prototypeMode], LookAndFeel::instance().style);

            IMW::Disabled disabled(dashboard == nullptr);

            if (mainViewMode != ViewMode::OPEN_SAVE_DASHBOARD) {
                components::Toolbar(toolbarBlocks);
            }

            if (dashboard != nullptr) {
                if (loadedDashboard != dashboard.get()) {
                    // Are we in the process of changing the dashboard?
                    loadedDashboard = dashboard.get();
                    dashboardPage   = std::make_unique<DashboardPage>();
                    dashboardPage->setDashboard(*dashboard.get());
                    dashboardPage->setLayoutType(loadedDashboard->layout());
                    flowgraphPage.reset();
                }
            }

            if (mainViewMode == ViewMode::VIEW || mainViewMode == ViewMode::LAYOUT) {
                if (dashboard != nullptr) {
                    dashboardPage->draw(mainViewMode == ViewMode::VIEW ? DashboardPage::Mode::View : DashboardPage::Mode::Layout);
                }
            } else if (mainViewMode == ViewMode::FLOWGRAPH) {
                if (dashboard != nullptr) {
                    if (previousViewMode != ViewMode::FLOWGRAPH) {
                        dashboard->graphModel().requestGraphUpdate();
                        dashboard->graphModel().requestAvailableBlocksTypesUpdate();
                    }

                    flowgraphPage.draw();
                }
            } else if (mainViewMode == ViewMode::OPEN_SAVE_DASHBOARD) {
                openDashboardPage.draw(dashboard.get());
            } else {
                auto msg = fmt::format("unknown view mode {}", static_cast<int>(mainViewMode));
                components::Notification::warning(msg);
            }
        }

        previousViewMode = mainViewMode;
    }
};

} // namespace DigitizerUi

#endif
