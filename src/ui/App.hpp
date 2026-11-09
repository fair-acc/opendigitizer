#ifndef APP_H
#define APP_H

#include <atomic>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/Events.hpp"
#include "common/FramePacer.hpp"
#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "components/AppHeader.hpp"
#include "components/ViewModeBlocker.hpp"
#include "components/YesNoPopup.hpp"

#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "FlowgraphPage.hpp"
#include "OpenDashboardPage.hpp"
#include "Setup.hpp"
#include "StatusBarView.hpp"
#include "ToolbarView.hpp"

#include "settings.hpp"
#include "utils/BrowserHistory.hpp"
#include "utils/EmscriptenHelper.hpp"

#include <IoSerialiserYaS.hpp>
#include <LoadTest.hpp>

#include <implot.h>
#include <magic_enum.hpp>

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
    static constexpr std::string_view kBuiltinSamplesSource = "example://builtin-samples";

    std::string executable;

    std::unique_ptr<Dashboard>                   dashboard;
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_unique<opencmw::client::RestClient>(opencmw::client::VerifyServerCertificates(Digitizer::Settings::instance().checkCertificates));

    Dashboard*                     loadedDashboard = nullptr;
    std::unique_ptr<DashboardPage> dashboardPage;
    ToolbarView                    toolbarView;
    StatusBarView                  statusBarView;

    FlowgraphPage     flowgraphPage;
    OpenDashboardPage openDashboardPage;

    std::atomic<bool> isRunning        = true;
    ViewMode          mainViewMode     = ViewMode::INTERACTION;
    ViewMode          previousViewMode = ViewMode::INTERACTION;

    // Since loading a dashboard blocks the main thread,
    // we want to have two steps, one which will prepare the
    // window to show the "Loading..." status, and the
    // next step to load the dashboard will hapen in the
    // next frame.
    bool                                        prepareForANewDashboardToLoad = false;
    std::shared_ptr<const DashboardDescription> dashboardToLoad;
    std::optional<std::string>                  deferredDashboardUrl;

    components::AppHeader header;

    BrowserHistory      history;
    bool                locationPublished = false;
    Page                publishedPage     = Page::Chart;
    std::optional<Page> pendingPage; // page requested by the URL, applied once the dashboard it belongs to is ready

public:
    App() : openDashboardPage(restClient) {
        LookAndFeel::mutableInstance().theme = magic_enum::enum_cast<LookAndFeel::Theme>(Digitizer::Settings::instance().theme, magic_enum::case_insensitive).value_or(LookAndFeel::Theme::Classic);
        setStyle(Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light);
        if (!Digitizer::Settings::instance().editableMode) {
            // start in view mode without application-authoring controls, while retaining chart interaction
            mainViewMode     = ViewMode::VIEW;
            previousViewMode = ViewMode::VIEW;
        }
    }

    void openNewWindow() {
#ifdef __EMSCRIPTEN__
        std::string script = std::format("window.open('{}').focus()", executable);
        emscripten_run_script(script.c_str());
#else
        if (fork() == 0) {
            execl(executable.c_str(), executable.c_str(), nullptr);
        }
#endif
    }

    void loadEmptyDashboard() { loadDashboard(DashboardDescription::createEmpty("New dashboard")); }

    void loadDashboard(const std::shared_ptr<const DashboardDescription>& desc) {
        auto startedAt = std::chrono::system_clock::now();

        if (dashboard) {
            while (true) {
                bool wait = false;
                if (dashboard->isInUse) {
                    wait = true;
                } else if (dashboard->session && dashboard->session.state() != gr::lifecycle::State::STOPPED) {
                    dashboard->session.stop();
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

        flowgraphPage.setDashboard(nullptr);
        dashboard = Dashboard::create(restClient, desc);
        dashboard->load();
        dashboard->requestClose = [this](Dashboard*) { closeDashboard(); };

        flowgraphPage.setDashboard(dashboard.get());

        publishLocation();
    }

    /// only dashboards a recipient can resolve get a link: the ones served next to the UI and the built-in samples
    [[nodiscard]] static bool isLinkableStorage(const DashboardStorageInfo& storage) { return !storage.isInMemoryDashboardStorage() && (storage.path == kBuiltinSamplesSource || storage.path == serviceDashboardsPath()); }

    [[nodiscard]] static std::string serviceDashboardsPath() { return Digitizer::Settings::instance().serviceUrl().path("/dashboards").build().str(); }

    [[nodiscard]] static std::optional<std::string> builtinSampleUrl([[maybe_unused]] std::string_view name) {
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
        if (cmrc::sample_dashboards::get_filesystem().is_file(std::format("assets/sampleDashboards/{}.grc", name))) {
            return std::format("{}/{}", kBuiltinSamplesSource, name);
        }
#endif
        return std::nullopt;
    }

    [[nodiscard]] static std::string resolveServiceDashboardPath(std::string_view dashboardName) {
        const Digitizer::Settings& settings = Digitizer::Settings::instance();
        // TODO: add subscription to remote dashboard worker if needed
        std::string dashboardPath{dashboardName};
        if (!dashboardPath.starts_with("http://") and !dashboardPath.starts_with("https://")) { // if the default dashboard does not contain a host, use the default
            std::string basePath = settings.basePath.empty() ? "" : settings.basePath;          // needed in case the dashboard page is loaded via a redirect
            if (!basePath.empty() && !basePath.ends_with("/")) {
                basePath += "/";
            }
            for (const std::string& bindAddress : settings.bindAddresses) {
                if (bindAddress.starts_with("https://") || bindAddress.starts_with("http://")) {
                    const auto bindUri = opencmw::URI<>(std::string(bindAddress));
                    dashboardPath      = std::format("{}://{}:{}/{}dashboards/{}", bindUri.scheme().value(), settings.hostname, bindUri.port().value(), basePath, dashboardPath);
                    break;
                }
            }
        }
        return dashboardPath;
    }

    /// reflect the loaded dashboard and page in the browser URL: the first publication replaces the initial entry, later ones add one
    void publishLocation() {
        const std::shared_ptr<const DashboardDescription> desc = dashboard ? dashboard->description : nullptr;
        // filename, not name: name is the stem, so `v1.2` would otherwise be linked as `v1`
        const bool linkable = desc && desc->storageInfo && isLinkableStorage(*desc->storageInfo) && Digitizer::AppLocation::isValidDashboardName(desc->filename);
        const Page page     = pendingPage.value_or(pageFor(mainViewMode));
        // keeps flags such as darkMode; drops the dashboard when it cannot be linked
        const auto& query = history.current().query;

        const bool                   listsDashboards = page == Page::Dashboards;
        const Digitizer::AppLocation location        = listsDashboards ? Digitizer::AppLocation::forDashboardList(query) : linkable ? Digitizer::AppLocation::forDashboard(desc->filename, query, page) : Digitizer::AppLocation{.segments = {}, .query = query};
        if (locationPublished && (listsDashboards || linkable)) {
            history.push(location);
        } else {
            history.replace(location);
        }
        locationPublished = true;
        publishedPage     = page;
    }

    [[nodiscard]] bool isLocationChangePending() const noexcept { return prepareForANewDashboardToLoad || deferredDashboardUrl || dashboardToLoad || pendingPage; }

    /// apply a location the browser navigated to (hand-edited URL, back, forward) without reloading the page
    void applyBrowserLocation(const Digitizer::AppLocation& location) {
        if (const auto darkMode = location.flag("darkMode")) {
            setStyle(*darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light);
        }
        if (const auto editable = location.flag("editable"); editable && *editable != Digitizer::Settings::instance().editableMode) {
            Digitizer::Settings::instance().editableMode = *editable;
            mainViewMode                                 = *editable ? ViewMode::INTERACTION : ViewMode::VIEW;
            previousViewMode                             = mainViewMode;
        }
        if (const auto name = location.dashboardName(); name && (!dashboard || dashboard->description->filename != *name)) {
            deferredDashboardUrl          = builtinSampleUrl(*name).value_or(resolveServiceDashboardPath(*name));
            prepareForANewDashboardToLoad = true;
        }
        if (const auto page = location.page()) {
            pendingPage = page;
        }
    }

    /// the editor needs its dashboard initialised: previousViewMode is overwritten every frame and the first
    /// FlowgraphPage update is requested on the transition into FLOWGRAPH, so switching earlier would leave it empty
    void applyPendingPage() {
        if (!pendingPage || prepareForANewDashboardToLoad || deferredDashboardUrl || dashboardToLoad) {
            return;
        }
        const Page page = *pendingPage;
        if (page != Page::Dashboards && dashboard && !(dashboard->isInitialised && loadedDashboard == dashboard.get())) {
            return; // wait for the dashboard
        }
        pendingPage.reset();
        const bool reachable = dashboard || page == Page::Dashboards;
        mainViewMode         = viewModeFor(reachable ? page : Page::Chart, mainViewMode, Digitizer::Settings::instance().editableMode);
        if (pageFor(mainViewMode) != page) { // not reachable (not editable, or no dashboard): correct the URL
            publishLocation();
        }
    }

    void loadDashboard(std::string_view url) {
        namespace fs = std::filesystem;
        fs::path path(url);

        auto storageInfo = DashboardStorageInfo::get(path.parent_path().native());
        DashboardDescription::loadAndThen(restClient, storageInfo, path.filename(), [this, storageInfo](std::shared_ptr<const DashboardDescription>&& desc) {
            if (desc) {
                loadDashboard(desc);
                openDashboardPage.addDashboard(storageInfo->path);
            }
        });
    }

    void closeDashboard() {
        if (dashboard && dashboard->session && dashboard->session.state() != gr::lifecycle::State::STOPPED) {
            dashboard->session.stop();
        }
        dashboardPage.reset();
        flowgraphPage.setDashboard(nullptr);
        loadedDashboard = nullptr;
        dashboard       = {};
    }

    void setStyle(LookAndFeel::Style style) {
        applyStyle(style);
        flowgraphPage.updateStyle();
    }

    void setTheme(LookAndFeel::Theme theme) {
        applyTheme(theme);
        flowgraphPage.updateStyle();
    }

    void init(int argc, char** argv) {
        Digitizer::Settings& settings = Digitizer::Settings::instance();

        history.onBrowserChange = [] { globalFramePacer().requestFrame(); };
        history.install();
        pendingPage = history.current().page();

        // Init openDashboardPage
        openDashboardPage.requestCloseDashboard = [&] { closeDashboard(); };
        openDashboardPage.requestLoadDashboard  = [&](const auto& desc) {
            if (!desc) {
                loadEmptyDashboard();
            } else {
                prepareForANewDashboardToLoad = true;
                dashboardToLoad               = desc;
            }
        };
        openDashboardPage.shareLinkFor = [this](std::string_view name, const DashboardStorageInfo& storage) -> std::optional<std::string> {
            if (!Digitizer::AppLocation::isValidDashboardName(name) || !isLinkableStorage(storage)) {
                return std::nullopt;
            }
            return history.linkTo(Digitizer::AppLocation::forDashboard(name));
        };
        openDashboardPage.addDashboard(serviceDashboardsPath());
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
        openDashboardPage.addDashboard(kBuiltinSamplesSource);
#endif

        // Flowgraph page
        flowgraphPage.requestBlockControlsPanel = [&](components::BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& size, bool horizontalSplit) {
            components::BlockControlsPanel(panelContext, pos, size, horizontalSplit);
            //
        };

        // Init header
        header.requestApplicationSwitchMode = [this](ViewMode mode) { mainViewMode = mode; };
        // use executeLater to avoid a potential bug where the style gets set in the scope of a push/restore of a style var from a different theme
        header.requestApplicationSwitchStyle = [this](LookAndFeel::Style style) {
            EventLoop::instance().executeLater([this, style] {
                setStyle(style);
                globalFramePacer().requestFrame();
            });
        };
        header.requestApplicationSwitchTheme = [this](LookAndFeel::Theme theme) {
            EventLoop::instance().executeLater([this, theme] {
                setTheme(theme);
                globalFramePacer().requestFrame();
            });
        };
        header.requestApplicationStop = [this] { isRunning = false; };
        header.loadAssets();

#ifdef __EMSCRIPTEN__
        // Defer argv / #/dashboard/<name> loads until after the first ImGui frame so SIDE_MODULE
        // dlopen (~60MB) does not race the first paint. Skip stock RemoteStream auto-load
        // against the static file server (404 → noisy fetch); open the built-in demo instead.
        if (argc > 1) {
            const char* url = argv[1];
            if (strlen(url) > 0) {
                std::print("Loading dashboard from '{}' (deferred)\n", url);
                deferredDashboardUrl          = url;
                prepareForANewDashboardToLoad = true;
            }
        } else if (settings.dashboardFromUrlFragment && !settings.defaultDashboard.empty()) {
            deferredDashboardUrl          = builtinSampleUrl(settings.defaultDashboard).value_or(resolveServiceDashboardPath(settings.defaultDashboard));
            prepareForANewDashboardToLoad = true;
            std::print("Loading dashboard from '{}' (deferred)\n", *deferredDashboardUrl);
        } else if (auto demoUrl = builtinSampleUrl("DemoDashboard")) {
            deferredDashboardUrl          = std::move(demoUrl);
            prepareForANewDashboardToLoad = true;
            std::print("Loading dashboard from '{}' (deferred)\n", *deferredDashboardUrl);
        } else if (dashboard == nullptr) {
            mainViewMode = ViewMode::OPEN_SAVE_DASHBOARD;
        }
#else
        if (argc > 1) { // load dashboard if specified on the command line/query parameter
            const char* url = argv[1];
            if (strlen(url) > 0) {
                std::print("Loading dashboard from '{}'\n", url);
                loadDashboard(url);
            }
        } else if (!settings.defaultDashboard.empty()) {
            loadDashboard(resolveServiceDashboardPath(settings.defaultDashboard));
        }
        if (auto firstDashboard = openDashboardPage.get(0); dashboard == nullptr && firstDashboard != nullptr) { // load first dashboard if there is a dashboard available
            loadDashboard(firstDashboard);
        }
#endif
    }

    [[nodiscard]] bool drawViewModeBlockerAndCheckExit(float startHeight) const noexcept {
        if (!Digitizer::Settings::instance().editableMode) {
            return false;
        }
        const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        return components::drawViewModeBlocker(ImRect({0.f, startHeight}, {viewportSize.x, viewportSize.y - StatusBarView::height()}));
    }

    void processAndRender() {
        {
            IMW::Window window("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

            const char* title = prepareForANewDashboardToLoad ? "Loading..." : dashboard ? dashboard->description->name.data() : "OpenDigitizer";
            header.draw(title, LookAndFeel::instance().fontLarge(), LookAndFeel::instance().style);

            if (mainViewMode != ViewMode::OPEN_SAVE_DASHBOARD && dashboard && dashboard->isInitialised && !prepareForANewDashboardToLoad) {
                toolbarView.draw(dashboard->session, dashboard->schedulerUi);
            }

            const float lockedModeBlockerStart = ImGui::GetCursorScreenPos().y;

            {
                IMW::Child pageArea("##pageArea", ImVec2(0.f, -StatusBarView::height()), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
                drawPage(lockedModeBlockerStart);
            }
            const bool withGraph = dashboard && dashboard->isInitialised;
            statusBarView.draw(withGraph ? &dashboard->session : nullptr);
        }

        if (pageFor(mainViewMode) != publishedPage && !isLocationChangePending()) {
            publishLocation();
        }

        previousViewMode = mainViewMode;
    }

    void drawPage(float lockedModeBlockerStart) {
        if (const auto browserChange = history.takeBrowserChange()) {
            applyBrowserLocation(*browserChange);
        }

        if (prepareForANewDashboardToLoad) {
            prepareForANewDashboardToLoad = false;
            return;
        }

        if (deferredDashboardUrl) {
            const std::string url = std::move(*deferredDashboardUrl);
            deferredDashboardUrl.reset();
            loadDashboard(url);
        }

        if (dashboardToLoad) {
            mainViewMode = ViewMode::VIEW; // before loading: loadDashboard publishes the page
            loadDashboard(dashboardToLoad);
            dashboardToLoad = nullptr;
        }

        if (dashboard != nullptr) {
            dashboard->handleMessages();
            if (loadedDashboard != dashboard.get() && dashboard->isInitialised) {
                // Are we in the process of changing the dashboard?
                loadedDashboard = dashboard.get();
                dashboardPage   = std::make_unique<DashboardPage>();
                dashboardPage->setDashboard(*dashboard.get());
                dashboardPage->setRequestViewOnlyModeHandler([this] { mainViewMode = ViewMode::VIEW; });
                dashboardPage->setRequestSetLayoutModeHandler([this](bool isLayout) { mainViewMode = isLayout ? ViewMode::LAYOUT : ViewMode::INTERACTION; });
                flowgraphPage.reset();
            }
        }

        applyPendingPage();

        if (mainViewMode == ViewMode::VIEW || mainViewMode == ViewMode::INTERACTION || mainViewMode == ViewMode::LAYOUT) {
            if (dashboard != nullptr && dashboard->isInitialised) {
                const auto mode = [&] {
                    using enum DashboardPage::Mode;
                    switch (mainViewMode) {
                    case ViewMode::VIEW: return View;
                    case ViewMode::INTERACTION: return Interaction;
                    case ViewMode::LAYOUT: return Layout;
                    default: assert(false); return View;
                    }
                }();
                dashboardPage->draw(mode);
            }
        } else if (mainViewMode == ViewMode::FLOWGRAPH) {
            if (dashboard != nullptr && dashboard->isInitialised) {
                if (previousViewMode != ViewMode::FLOWGRAPH) {
                    dashboard->session.graphModel.requestFullUpdate();
                    dashboard->session.graphModel.requestAvailableBlocksTypesUpdate();
                }

                flowgraphPage.draw();
            }
        } else if (mainViewMode == ViewMode::OPEN_SAVE_DASHBOARD) {
            openDashboardPage.draw(dashboard.get(), dashboardPage.get());
        } else {
            auto msg = std::format("unknown view mode {}", static_cast<int>(mainViewMode));
            components::Notification::warning(msg);
        }

        if (mainViewMode == ViewMode::VIEW && this->drawViewModeBlockerAndCheckExit(lockedModeBlockerStart)) {
            mainViewMode = ViewMode::INTERACTION;
        }
    }
};

} // namespace DigitizerUi

#endif
