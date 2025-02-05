#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>

#include <fmt/format.h>

#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/clock_source.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/testing/Delay.hpp>

#include "common/Events.hpp"
#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "components/ImGuiNotify.hpp"

#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <implot.h>
#include <misc/cpp/imgui_stdlib.h>

#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include "settings.hpp"

#include "App.hpp"
#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "Flowgraph.hpp"
#include "FlowgraphItem.hpp"

#include "common/AppDefinitions.hpp"
#include "common/TouchHandler.hpp"

#include "components/AppHeader.hpp"
#include "components/Toolbar.hpp"

#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TagToSample.hpp"

CMRC_DECLARE(ui_assets);
CMRC_DECLARE(fonts);

namespace DigitizerUi {

struct SDLState {
    SDL_Window*   window    = nullptr;
    SDL_GLContext glContext = nullptr;
};

} // namespace DigitizerUi

using namespace DigitizerUi;

static void main_loop(void*);

static void loadFonts(App& app) {
    static const ImWchar fullRange[] = {
        0x0020, 0XFFFF, 0, 0 // '0', '0' are the end marker
        // N.B. a bit unsafe but in imgui_draw.cpp::ImFontAtlasBuildWithStbTruetype break condition is:
        // 'for (const ImWchar* src_range = src_tmp.SrcRanges; src_range[0] && src_range[1]; src_range += 2)'
    };
    static const std::vector<ImWchar> rangeLatin             = {0x0020, 0x0080, // Basic Latin
                    0, 0};
    static const std::vector<ImWchar> rangeLatinExtended     = {0x80, 0xFFFF, 0}; // Latin-1 Supplement
    static const std::vector<ImWchar> rangeLatinPlusExtended = {0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement (standard + extended ASCII)
        0, 0};
    static const ImWchar              glyphRanges[]          = {// pick individual glyphs and specific sub-ranges rather than full range
        0XF005, 0XF2ED,                   // 0xf005 is "", 0xf2ed is "trash can"
        0X2B, 0X2B,                       // plus
        0XF055, 0XF055,                   // circle-plus
        0XF201, 0XF83E,                   // fa-chart-line, fa-wave-square
        0xF58D, 0xF58D,                   // grid layout
        0XF7A5, 0XF7A5,                   // horizontal layout,
        0xF248, 0xF248,                   // free layout,
        0XF7A4, 0XF7A4,                   // vertical layout
        0XEF808D, 0XEF808D,               // notification ICON_FA_XMARK
        0XEF8198, 0XEF8198,               // notification ICON_FA_CIRCLE_CHECK
        0XEF81B1, 0XEF81B1,               // notification ICON_FA_TRIANGLE_EXCLAMATION
        0XEF81AA, 0XEF81AA,               // notification ICON_FA_CIRCLE_EXCLAMATION
        0XEF819A, 0XEF819A,               // notification ICON_FA_CIRCLE_INFO
        0, 0};

    static const auto fontSize = []() -> std::array<float, 4> {
        if (std::abs(LookAndFeel::instance().verticalDPI - LookAndFeel::instance().defaultDPI) < 8.f) {
            return {20, 24, 28, 46}; // 28" monitor
        } else if (LookAndFeel::instance().verticalDPI > 200) {
            return {16, 22, 23, 38}; // likely mobile monitor
        } else if (std::abs(LookAndFeel::instance().defaultDPI - LookAndFeel::instance().verticalDPI) >= 8.f) {
            return {22, 26, 30, 46}; // likely large fixed display monitor
        }
        return {18, 24, 26, 46}; // default
    }();

    static ImFontConfig config;
    // Originally oversampling of 4 was used to ensure good looking text for all zoom levels, but this led to huge texture atlas sizes, which did not work on mobile
    config.OversampleH          = 2;
    config.OversampleV          = 2;
    config.PixelSnapH           = true;
    config.FontDataOwnedByAtlas = false;

    auto loadDefaultFont = [&app](auto primaryFont, auto secondaryFont, std::size_t index, const std::vector<ImWchar>& ranges = {}) {
        auto loadFont = [&primaryFont, &secondaryFont, &ranges](float fontSize) {
            const auto loadFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(primaryFont.begin()), int(primaryFont.size()), fontSize, &config);
            if (!ranges.empty()) {
                config.MergeMode = true;
                ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(secondaryFont.begin()), int(secondaryFont.size()), fontSize, &config, ranges.data());
                config.MergeMode = false;
            }
            return loadFont;
        };

        auto& lookAndFeel             = LookAndFeel::mutableInstance();
        lookAndFeel.fontNormal[index] = loadFont(fontSize[0]);
        lookAndFeel.fontBig[index]    = loadFont(fontSize[1]);
        lookAndFeel.fontBigger[index] = loadFont(fontSize[2]);
        lookAndFeel.fontLarge[index]  = loadFont(fontSize[3]);
    };

    loadDefaultFont(cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 0);
    loadDefaultFont(cmrc::ui_assets::get_filesystem().open("assets/xkcd/xkcd-script.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 1, rangeLatinExtended);
    ImGui::GetIO().FontDefault = LookAndFeel::instance().fontNormal[LookAndFeel::instance().prototypeMode];

    auto loadIconsFont = [](auto name, float fontSize) {
        auto file = cmrc::ui_assets::get_filesystem().open(name);
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(file.begin()), static_cast<int>(file.size()), fontSize, &config, glyphRanges); // alt: fullRange
    };

    auto& lookAndFeel               = LookAndFeel::mutableInstance();
    lookAndFeel.fontIcons           = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 12);
    lookAndFeel.fontIconsBig        = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 18);
    lookAndFeel.fontIconsLarge      = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 36);
    lookAndFeel.fontIconsSolid      = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 12);
    lookAndFeel.fontIconsSolidBig   = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 18);
    lookAndFeel.fontIconsSolidLarge = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 36);
}

void setWindowMode(SDL_Window* window, const WindowMode& state) {
    using enum WindowMode;
    const Uint32 flags        = SDL_GetWindowFlags(window);
    const bool   isMaximised  = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    const bool   isMinimised  = (flags & SDL_WINDOW_MINIMIZED) != 0;
    const bool   isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if ((isMaximised && state == MAXIMISED) || (isMinimised && state == MINIMISED) || (isFullscreen && state == FULLSCREEN)) {
        return;
    }
    switch (state) {
    case FULLSCREEN: SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP); return;
    case MAXIMISED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_MaximizeWindow(window);
        return;
    case MINIMISED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_MinimizeWindow(window);
        return;
    case RESTORED: SDL_SetWindowFullscreen(window, 0); SDL_RestoreWindow(window);
    }
}

int main(int argc, char** argv) {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    Digitizer::Settings& settings = Digitizer::Settings::instance();

#if defined(IMGUI_IMPL_OPENGL_ES2)
    // GL ES 2.0 + GLSL 100
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    const auto window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDLState   sdlState;
    sdlState.window = SDL_CreateWindow("opendigitizer UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    if (!sdlState.window) {
        fmt::print(stderr, "Failed to create SDL window: {}!\n", SDL_GetError());
        return 1;
    }
    sdlState.glContext = SDL_GL_CreateContext(sdlState.window);
    if (!sdlState.glContext) {
        fmt::print(stderr, "Failed to initialize GL context!\n");
        return 1;
    }
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io                  = ImGui::GetIO();
    ImPlot::GetInputMap().Select = ImGuiPopupFlags_MouseButtonLeft;
    ImPlot::GetInputMap().Pan    = ImGuiPopupFlags_MouseButtonMiddle;

    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(sdlState.window, sdlState.glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    auto& app = App::instance();

    // Init openDashboardPage
    app.openDashboardPage.requestCloseDashboard = [&] { app.closeDashboard(); };
    app.openDashboardPage.requestLoadDashboard  = [&](const auto& desc) {
        if (!desc) {
            app.loadEmptyDashboard();
        } else {
            app.loadDashboard(desc);
            app.mainViewMode = ViewMode::VIEW;
        }
    };
    app.openDashboardPage.addSource(settings.serviceUrl().path("/dashboards").build().str());
    app.openDashboardPage.addSource("example://builtin-samples");
#ifdef EMSCRIPTEN
    app.executable = "index.html";
#else
    app.executable = argv[0];
#endif
    app.sdlState = &sdlState;

    app.fgItem.requestBlockControlsPanel = [&](components::BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& size, bool horizontalSplit) {
        auto& dashboard     = app.dashboard;
        auto& dashboardPage = app.dashboardPage;
        components::BlockControlsPanel(*dashboard, dashboardPage, panelContext, pos, size, horizontalSplit);
    };
    app.fgItem.newSinkCallback = [&](FlowGraph*) mutable {
        auto newSink = app.dashboard->createSink();
        app.dashboardPage.newPlot(*app.dashboard);
        auto& plot = app.dashboard->plots().back();
        plot.sourceNames.push_back(newSink->name);
        return newSink;
    };

    LookAndFeel::mutableInstance().verticalDPI = [&app]() -> float {
        float diagonalDPI   = LookAndFeel::instance().defaultDPI;
        float horizontalDPI = diagonalDPI;
        float verticalDPI   = diagonalDPI;
        if (SDL_GetDisplayDPI(0, &diagonalDPI, &horizontalDPI, &verticalDPI) != 0) {
            auto msg = fmt::format("Failed to obtain DPI information for display 0: {}", SDL_GetError());
            components::Notification::error(msg);
            return LookAndFeel::instance().defaultDPI;
        }
        return verticalDPI;
    }();

    loadFonts(app);

    // Init header
    app.header.requestApplicationSwitchMode  = [&app](ViewMode mode) { app.mainViewMode = mode; };
    app.header.requestApplicationSwitchTheme = [&app](LookAndFeel::Style style) { app.setStyle(style); };
    app.header.requestApplicationStop        = [&app] { app.running = false; };
    app.header.loadAssets();

    if (argc > 1) { // load dashboard if specified on the command line/query parameter
        const char* url = argv[1];
        if (strlen(url) > 0) {
            fmt::print("Loading dashboard from '{}'\n", url);
            app.loadDashboard(url);
        }
    } else if (!settings.defaultDashboard.empty()) {
        // TODO: add subscription to remote dashboard worker if needed
        std::string dashboard = settings.defaultDashboard;
        if (!dashboard.starts_with("http://") and !dashboard.starts_with("https://")) { // if the default dashboard does not contain a host, use the default
            dashboard = fmt::format("{}://{}:{}/dashboards/{}", settings.disableHttps ? "http" : "https", settings.hostname, settings.port, dashboard);
        }
        app.loadDashboard(dashboard);
    }
    if (auto first_dashboard = app.openDashboardPage.get(0); app.dashboard == nullptr && first_dashboard != nullptr) { // load first dashboard if there is a dashboard available
        app.loadDashboard(first_dashboard);
    }

    // This function call won't return, and will engage in an infinite loop, processing events from the browser, and dispatching them.
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    SDL_GL_SetSwapInterval(1); // Enable vsync

    while (app.running) {
        main_loop(&app);
    }
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(sdlState.glContext);
    SDL_DestroyWindow(sdlState.window);
    SDL_Quit();
#endif
    // emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 10);
}

namespace {

std::string localFlowgraphGrc = {};

}

static void main_loop(void* arg) {
    const auto startLoop = std::chrono::high_resolution_clock::now();
    auto*      app       = static_cast<App*>(arg);
    ImGuiIO&   io        = ImGui::GetIO();

    EventLoop::instance().fireCallbacks();

    if (app->dashboard && app->dashboard->localFlowGraph.graphChanged()) {
        // create the graph and the scheduler
        auto execution = app->dashboard->localFlowGraph.createExecutionContext();
        app->dashboard->localFlowGraph.setPlotSinkGrBlocks(std::move(execution.plotSinkGrBlocks));
        app->toolbarBlocks = std::move(execution.toolbarBlocks);
        app->dashboard->loadPlotSources();
        app->assignScheduler(std::move(execution.grGraph));
        localFlowgraphGrc = app->dashboard->localFlowGraph.grc();
    }

    if (app->dashboard) {
        app->handleMessages(app->dashboard->localFlowGraph);
    }

    // Poll and handle events (inputs, window resize, etc.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        switch (event.type) {
        case SDL_QUIT: app->running = false; break;
        case SDL_WINDOWEVENT:
            if (event.window.windowID != SDL_GetWindowID(app->sdlState->window)) {
                // break // TODO: why was this aborted here?
            } else {
                const int width  = event.window.data1;
                const int height = event.window.data2;

                ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
                glViewport(0, 0, width, height);
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
            }
            switch (event.window.event) {
            case SDL_WINDOWEVENT_CLOSE: app->running = false; break;
            case SDL_WINDOWEVENT_RESTORED: LookAndFeel::mutableInstance().windowMode = WindowMode::RESTORED; break;
            case SDL_WINDOWEVENT_MINIMIZED: LookAndFeel::mutableInstance().windowMode = WindowMode::MINIMISED; break;
            case SDL_WINDOWEVENT_MAXIMIZED: LookAndFeel::mutableInstance().windowMode = WindowMode::MAXIMISED; break;
            case SDL_WINDOWEVENT_SIZE_CHANGED: break;
            }
            break;
        }
        TouchHandler<>::processSDLEvent(event);
        // Capture events here, based on io.WantCaptureMouse and io.WantCaptureKeyboard
    }
    TouchHandler<>::updateGestures();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    int width, height;
    SDL_GetWindowSize(app->sdlState->window, &width, &height);
    ImGui::SetNextWindowSize({float(width), float(height)});
    TouchHandler<>::applyToImGui();

    {
        IMW::Window window("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        const char* title = app->dashboard ? app->dashboard->description()->name.data() : "OpenDigitizer";
        app->header.draw(title, LookAndFeel::instance().fontLarge[LookAndFeel::instance().prototypeMode], LookAndFeel::instance().style);

        IMW::Disabled disabled(app->dashboard == nullptr);

        if (app->mainViewMode != ViewMode::OPEN_SAVE_DASHBOARD) {
            components::Toolbar(app->toolbarBlocks);
        }

        if (app->mainViewMode == ViewMode::VIEW) {
            if (app->dashboard != nullptr) {
                app->dashboardPage.draw(*app->dashboard);
            }
        } else if (app->mainViewMode == ViewMode::LAYOUT) {
            if (app->dashboard != nullptr) {
                app->dashboardPage.draw(*app->dashboard, DashboardPage::Mode::Layout);
            }
        } else if (app->mainViewMode == ViewMode::FLOWGRAPH) {
            if (app->previousViewMode != ViewMode::FLOWGRAPH) {
                app->dashboard->localFlowGraph.graphModel.requestGraphUpdate();
            }

            if (app->dashboard != nullptr) {
                // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
                IMW::TabBar tabBar("maintabbar", 0);
                if (auto item = IMW::TabItem("Local", nullptr, 0)) {
                    auto contentRegion = ImGui::GetContentRegionAvail();
                    app->fgItem.draw(&app->dashboard->localFlowGraph, contentRegion);
                }
                if (auto item = IMW::TabItem("Local - YAML", nullptr, 0)) {
                    if (ImGui::Button("Reset")) {
                        localFlowgraphGrc = app->dashboard->localFlowGraph.grc();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Apply")) {
                        auto sinkNames = [](const auto& blocks) {
                            using namespace std;
                            auto isPlotSink = [](const auto& b) { return b->type().isPlotSink(); };
                            auto getName    = [](const auto& b) { return b->name; };
                            auto namesView  = blocks | views::filter(isPlotSink) | views::transform(getName);
                            auto names      = std::vector(namesView.begin(), namesView.end());
                            ranges::sort(names);
                            names.erase(std::unique(names.begin(), names.end()), names.end());
                            return names;
                        };

                        const auto oldNames = sinkNames(app->dashboard->localFlowGraph.blocks());

                        try {
                            app->dashboard->localFlowGraph.parse(localFlowgraphGrc);
                            const auto               newNames = sinkNames(app->dashboard->localFlowGraph.blocks());
                            std::vector<std::string> toRemove;
                            std::ranges::set_difference(oldNames, newNames, std::back_inserter(toRemove));
                            std::vector<std::string> toAdd;
                            std::ranges::set_difference(newNames, oldNames, std::back_inserter(toAdd));
                            for (const auto& name : toRemove) {
                                app->dashboard->removeSinkFromPlots(name);
                            }
                            for (const auto& newName : toAdd) {
                                app->dashboardPage.newPlot(*app->dashboard);
                                app->dashboard->plots().back().sourceNames.push_back(newName);
                            }
                        } catch (const std::exception& e) {
                            // TODO show error message
                            auto msg = fmt::format("Error parsing YAML: {}", e.what());
                            components::Notification::error(msg);
                        }
                    }

                    ImGui::InputTextMultiline("##grc", &localFlowgraphGrc, ImGui::GetContentRegionAvail());
                }

                for (auto& s : app->dashboard->remoteServices()) {
                    std::string title = "Remote YAML for " + s.name;
                    if (auto item = IMW::TabItem(title.c_str(), nullptr, 0)) {
                        if (ImGui::Button("Reload from service")) {
                            s.reload();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Execute on service")) {
                            s.execute();
                        }

                        // TODO: For demonstration purposes only, remove
                        // once we have a proper server-side graph editor
                        // if (::getenv("DIGITIZER_UI_SHOW_SERVER_TEST_BUTTONS")) {
                        ImGui::SameLine();
                        if (ImGui::Button("Create a block")) {
                            s.emplaceBlock("gr::basic::DataSink", "float");
                        }
                        // }

                        ImGui::InputTextMultiline("##grc", &s.grc, ImGui::GetContentRegionAvail());
                    }
                }
            }
        } else if (app->mainViewMode == ViewMode::OPEN_SAVE_DASHBOARD) {
            app->openDashboardPage.draw(app->dashboard);
        } else {
            auto msg = fmt::format("unknown view mode {}", static_cast<int>(app->mainViewMode));
            components::Notification::warning(msg);
        }
    }

    app->previousViewMode = app->mainViewMode;

    components::Notification::render();

    // Rendering
    ImGui::Render();
    SDL_GL_MakeCurrent(app->sdlState->window, app->sdlState->glContext);
    glViewport(0, 0, int(io.DisplaySize.x), int(io.DisplaySize.y));
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    const auto stopLoop                     = std::chrono::high_resolution_clock::now();
    LookAndFeel::mutableInstance().execTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopLoop - startLoop);
    SDL_GL_SwapWindow(app->sdlState->window);
    setWindowMode(app->sdlState->window, LookAndFeel::instance().windowMode);
}
