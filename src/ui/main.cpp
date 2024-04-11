#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_internal.h>
#include <implot.h>
#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>

#include <fmt/format.h>
#include <gnuradio-4.0/basic/clock_source.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/Delay.hpp>

#include "app.hpp"
#include "dashboard.hpp"
#include "dashboardpage.hpp"
#include "fair_header.hpp"
#include "flowgraph.hpp"
#include "flowgraph/datasink.hpp"
#include "flowgraphitem.hpp"
#include "settings.hpp"
#include "toolbar.hpp"
#include "toolbar_block.hpp"

#include "utils/TouchHandler.hpp"

#include "blocks/Arithmetic.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/SineSource.hpp"

CMRC_DECLARE(ui_assets);
CMRC_DECLARE(fonts);

namespace DigitizerUi {

struct SDLState {
    SDL_Window   *window    = nullptr;
    SDL_GLContext glContext = nullptr;
};

} // namespace DigitizerUi

static void main_loop(void *);

static void loadFonts(DigitizerUi::App &app) {
    static const ImWchar fullRange[] = {
        0x0020, 0XFFFF, 0, 0 // '0', '0' are the end marker
        // N.B. a bit unsafe but in imgui_draw.cpp::ImFontAtlasBuildWithStbTruetype break condition is:
        // 'for (const ImWchar* src_range = src_tmp.SrcRanges; src_range[0] && src_range[1]; src_range += 2)'
    };
    static const std::vector<ImWchar> rangeLatin = {
        0x0020, 0x0080, // Basic Latin
        0, 0
    };
    static const std::vector<ImWchar> rangeLatinExtended     = { 0x80, 0xFFFF, 0 }; // Latin-1 Supplement
    static const std::vector<ImWchar> rangeLatinPlusExtended = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement (standard + extended ASCII)
        0, 0
    };
    static const ImWchar glyphRanges[] = { // pick individual glyphs and specific sub-ranges rather than full range
        0XF005, 0XF2ED,                    // 0xf005 is "", 0xf2ed is "trash can"
        0X2B, 0X2B,                        // plus
        0XF055, 0XF055,                    // circle-plus
        0XF201, 0XF83E,                    // fa-chart-line, fa-wave-square
        0xF58D, 0xF58D,                    // grid layout
        0XF7A5, 0XF7A5,                    // horizontal layout,
        0xF248, 0xF248,                    // free layout,
        0XF7A4, 0XF7A4,                    // vertical layout
        0, 0
    };

    static const auto fontSize = [&app]() -> std::array<float, 4> {
        if (std::abs(app.verticalDPI - app.defaultDPI) < 8.f) {
            return { 20, 24, 28, 46 }; // 28" monitor
        } else if (app.verticalDPI > 200) {
            return { 16, 22, 23, 38 }; // likely mobile monitor
        } else if (std::abs(app.defaultDPI - app.verticalDPI) >= 8.f) {
            return { 22, 26, 30, 46 }; // likely large fixed display monitor
        }
        return { 18, 24, 26, 46 }; // default
    }();

    static ImFontConfig config;
    // high oversample to have better looking text when zooming in on the flow graph
    config.OversampleH          = 4;
    config.OversampleV          = 4;
    config.PixelSnapH           = true;
    config.FontDataOwnedByAtlas = false;

    auto loadDefaultFont        = [&app](auto primaryFont, auto secondaryFont, std::size_t index, const std::vector<ImWchar> &ranges = {}) {
        auto loadFont = [&primaryFont, &secondaryFont, &ranges](float fontSize) {
            const auto loadFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char *>(primaryFont.begin()), int(primaryFont.size()), fontSize, &config);
            if (!ranges.empty()) {
                config.MergeMode = true;
                ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char *>(secondaryFont.begin()), int(secondaryFont.size()), fontSize, &config, ranges.data());
                config.MergeMode = false;
            }
            return loadFont;
        };

        app.fontNormal[index] = loadFont(fontSize[0]);
        app.fontBig[index]    = loadFont(fontSize[1]);
        app.fontBigger[index] = loadFont(fontSize[2]);
        app.fontLarge[index]  = loadFont(fontSize[3]);
    };

    loadDefaultFont(cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 0);
    loadDefaultFont(cmrc::ui_assets::get_filesystem().open("assets/xkcd/xkcd-script.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 1, rangeLatinExtended);
    ImGui::GetIO().FontDefault = app.fontNormal[app.prototypeMode];

    auto loadIconsFont         = [](auto name, float fontSize) {
        auto file = cmrc::ui_assets::get_filesystem().open(name);
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char *>(file.begin()), static_cast<int>(file.size()), fontSize, &config, glyphRanges); // alt: fullRange
    };

    app.fontIcons           = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 12);
    app.fontIconsBig        = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 18);
    app.fontIconsLarge      = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 36);
    app.fontIconsSolid      = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 12);
    app.fontIconsSolidBig   = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 18);
    app.fontIconsSolidLarge = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 36);
}

void setWindowMode(SDL_Window *window, DigitizerUi::WindowMode &state) {
    using enum DigitizerUi::WindowMode;
    const Uint32 flags        = SDL_GetWindowFlags(window);
    const bool   isMaximised  = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    const bool   isMinimised  = (flags & SDL_WINDOW_MINIMIZED) != 0;
    const bool   isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if ((isMaximised && state == MAXIMISED) || (isMinimised && state == MINIMISED) || (isFullscreen && state == FULLSCREEN)) {
        return;
    }
    switch (state) {
    case FULLSCREEN:
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        return;
    case MAXIMISED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_MaximizeWindow(window);
        return;
    case MINIMISED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_MinimizeWindow(window);
        return;
    case RESTORED:
        SDL_SetWindowFullscreen(window, 0);
        SDL_RestoreWindow(window);
    }
}

int main(int argc, char **argv) {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    Digitizer::Settings settings;

    // For the browser using Emscripten, we are going to use WebGL1 with GL ES2.
    // It is very likely the generated file won't work in many browsers.
    // Firefox is the only sure bet, but I have successfully run this code on
    // Chrome for Android for example.
    const char *glsl_version = "#version 100";
    // const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    const auto            window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    DigitizerUi::SDLState sdlState;
    sdlState.window    = SDL_CreateWindow("opendigitizer UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    sdlState.glContext = SDL_GL_CreateContext(sdlState.window);
    if (!sdlState.glContext) {
        fprintf(stderr, "Failed to initialize WebGL context!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io                  = ImGui::GetIO();
    ImPlot::GetInputMap().Select = ImGuiPopupFlags_MouseButtonLeft;
    ImPlot::GetInputMap().Pan    = ImGuiPopupFlags_MouseButtonMiddle;

    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(sdlState.window, sdlState.glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    auto &app = DigitizerUi::App::instance();
    app.openDashboardPage.addSource(settings.serviceUrl().path("/dashboards").build().str());
    app.openDashboardPage.addSource("example://builtin-samples");
#ifdef EMSCRIPTEN
    app.executable = "index.html";
#else
    app.executable = argv[0];
#endif
    app.sdlState               = &sdlState;

    app.fgItem.newSinkCallback = [&](DigitizerUi::FlowGraph *) mutable {
        auto newSink = app.dashboard->createSink();
        app.dashboardPage.newPlot(app.dashboard.get());
        auto &plot = app.dashboard->plots().back();
        plot.sourceNames.push_back(newSink->name);
        return newSink;
    };

    app.verticalDPI = [&app]() -> float {
        float diagonalDPI   = app.defaultDPI;
        float horizontalDPI = diagonalDPI;
        float verticalDPI   = diagonalDPI;
        if (SDL_GetDisplayDPI(0, &diagonalDPI, &horizontalDPI, &verticalDPI) != 0) {
            fmt::print("Failed to obtain DPI information for display 0: {}\n", SDL_GetError());
            return app.defaultDPI;
        }
        return verticalDPI;
    }();

#ifndef EMSCRIPTEN
    DigitizerUi::BlockType::registry().loadBlockDefinitions(BLOCKS_DIR);
#endif

    gr::registerBlock<opendigitizer::DSSink, float, double>(gr::globalBlockRegistry());
    gr::registerBlock<opendigitizer::PlotSink, float, double>(gr::globalBlockRegistry());

    DigitizerUi::DataSink::registerBlockType();

    // TODO populate these from the gr::globalBlockRegistry()
    DigitizerUi::BlockType::registry().addBlockType<opendigitizer::SineSource>("opendigitizer::SineSource");
    DigitizerUi::BlockType::registry().addBlockType<opendigitizer::RemoteSource>("opendigitizer::RemoteSource");
    DigitizerUi::BlockType::registry().addBlockType<opendigitizer::Arithmetic>("opendigitizer::Arithmetic");
    // DigitizerUi::BlockType::registry().addBlockType<gr::basic::DefaultClockSource>("gr::basic::ClockSource");
    DigitizerUi::BlockType::registry().addBlockType<gr::blocks::fft::DefaultFFT>("gr::blocks::fft::FFT");
    DigitizerUi::BlockType::registry().addBlockType<gr::testing::Delay>("gr::testing::Delay");
    DigitizerUi::BlockType::registry().addBlockType<DigitizerUi::PlayStopToolbarBlock>("toolbar_playstop_block");
    DigitizerUi::BlockType::registry().addBlockType<DigitizerUi::LabelToolbarBlock>("toolbar_label_block");

    loadFonts(app);

    app_header::load_header_assets();

    if (argc > 1) { // load dashboard if specified on the command line/query parameter
        const char *url = argv[1];
        if (strlen(url) > 0) {
            fmt::print("Loading dashboard from '{}'\n", url);
            app.loadDashboard(url);
        }
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

static void main_loop(void *arg) {
    const auto startLoop = std::chrono::high_resolution_clock::now();
    auto      *app       = static_cast<DigitizerUi::App *>(arg);
    ImGuiIO   &io        = ImGui::GetIO();

    app->fireCallbacks();

    if (app->dashboard->localFlowGraph.graphChanged()) {
        // create the graph and the scheduler
        auto execution = app->dashboard->localFlowGraph.createExecutionContext();
        app->dashboard->loadPlotSources();
        app->_toolbarBlocks = std::move(execution.toolbarBlocks);
        app->_plotBlocks    = std::move(execution.plotBlocks);
        app->assignScheduler(std::move(execution.graph));
        localFlowgraphGrc = app->dashboard->localFlowGraph.grc();
    }

    app->handleMessages(app->dashboard->localFlowGraph);

    // Poll and handle events (inputs, window resize, etc.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        switch (event.type) {
        case SDL_QUIT:
            app->running = false;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.windowID != SDL_GetWindowID(app->sdlState->window)) {
                // break // TODO: why was this aborted here?
            } else {
                const int width            = event.window.data1;
                const int height           = event.window.data2;

                ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
                glViewport(0, 0, width, height);
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
            }
            switch (event.window.event) {
            case SDL_WINDOWEVENT_CLOSE:
                app->running = false;
                break;
            case SDL_WINDOWEVENT_RESTORED:
                app->windowMode = DigitizerUi::WindowMode::RESTORED;
                break;
            case SDL_WINDOWEVENT_MINIMIZED:
                app->windowMode = DigitizerUi::WindowMode::MINIMISED;
                break;
            case SDL_WINDOWEVENT_MAXIMIZED:
                app->windowMode = DigitizerUi::WindowMode::MAXIMISED;
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                break;
            }
            break;
        }
        fair::TouchHandler<>::processSDLEvent(event);
        // Capture events here, based on io.WantCaptureMouse and io.WantCaptureKeyboard
    }
    fair::TouchHandler<>::updateGestures();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({ 0, 0 });
    int width, height;
    SDL_GetWindowSize(app->sdlState->window, &width, &height);
    ImGui::SetNextWindowSize({ float(width), float(height) });
    fair::TouchHandler<>::applyToImGui();

    ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const char *title = app->dashboard ? app->dashboard->description()->name.data() : "OpenDigitizer";
    app_header::draw_header_bar(title, app->fontLarge[app->prototypeMode],
            app->style() == DigitizerUi::Style::Light ? app_header::Style::Light : app_header::Style::Dark);

    if (app->dashboard == nullptr) {
        ImGui::BeginDisabled();
    }

    if (app->mainViewMode == "View" || app->mainViewMode == "Layout" || app->mainViewMode == "FlowGraph" || app->mainViewMode.empty()) {
        DigitizerUi::drawToolbar();
    }

    ImGuiID viewId = 0;
    if (app->mainViewMode == "View" || app->mainViewMode.empty()) {
        if (app->dashboard != nullptr) {
            app->dashboardPage.draw(app, app->dashboard.get());
        }
    } else if (app->mainViewMode == "Layout") {
        // The ID of this tab is different from the ID of the view tab. That means that the plots in the two tabs
        // are considered to be different plots, so changing e.g. the zoom level of a plot in the view tab would
        // not reflect in the layout tab.
        // To fix that we use the PushOverrideID() function to force the ID of this tab to be the same as the ID
        // of the view tab.
        ImGui::PushOverrideID(viewId);
        if (app->dashboard != nullptr) {
            app->dashboardPage.draw(app, app->dashboard.get(), DigitizerUi::DashboardPage::Mode::Layout);
        }
        ImGui::PopID();
    } else if (app->mainViewMode == "FlowGraph") {
        if (app->dashboard != nullptr) {
            // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
            ImGui::BeginTabBar("maintabbar");
            if (ImGui::BeginTabItem("Local")) {
                auto contentRegion = ImGui::GetContentRegionAvail();
                app->fgItem.draw(&app->dashboard->localFlowGraph, contentRegion);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Local - YAML")) {
                if (ImGui::Button("Reset")) {
                    localFlowgraphGrc = app->dashboard->localFlowGraph.grc();
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply")) {
                    auto sinkNames = [](const auto &sinks) {
                        std::vector<std::string> names;
                        std::ranges::transform(sinks, std::back_inserter(names), [](const auto &s) { return s->name; });
                        std::ranges::sort(names);
                        names.erase(std::unique(names.begin(), names.end()), names.end());
                        return names;
                    };
                    const auto oldNames = sinkNames(app->dashboard->localFlowGraph.sinkBlocks());
                    try {
                        app->dashboard->localFlowGraph.parse(localFlowgraphGrc);
                        const auto               newNames = sinkNames(app->dashboard->localFlowGraph.sinkBlocks());
                        std::vector<std::string> toRemove;
                        std::ranges::set_difference(oldNames, newNames, std::back_inserter(toRemove));
                        std::vector<std::string> toAdd;
                        std::ranges::set_difference(newNames, oldNames, std::back_inserter(toAdd));
                        for (const auto &name : toRemove) {
                            app->dashboard->removeSinkFromPlots(name);
                        }
                        for (const auto &newName : toAdd) {
                            app->dashboardPage.newPlot(app->dashboard.get());
                            app->dashboard->plots().back().sourceNames.push_back(newName);
                        }
                    } catch (const std::exception &e) {
                        // TODO show error message
                        fmt::print(std::cerr, "Error parsing YAML: {}\n", e.what());
                    }
                }

                ImGui::InputTextMultiline("##grc", &localFlowgraphGrc, ImGui::GetContentRegionAvail());
                ImGui::EndTabItem();
            }

            for (auto &s : app->dashboard->remoteServices()) {
                if (ImGui::BeginTabItem(s.name.c_str())) {
                    if (ImGui::Button("Reload from service")) {
                        s.reload();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Execute on service")) {
                        s.execute();
                    }
                    ImGui::InputTextMultiline("##grc", &s.grc, ImGui::GetContentRegionAvail());
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    } else if (app->mainViewMode == "OpenSaveDashboard") {
        app->openDashboardPage.draw(app);
    } else {
        fmt::print("unknown view mode {}\n", app->mainViewMode);
    }

    if (app->dashboard == nullptr) {
        ImGui::EndDisabled();
    }

    ImGui::End();

    // Rendering
    ImGui::Render();
    SDL_GL_MakeCurrent(app->sdlState->window, app->sdlState->glContext);
    glViewport(0, 0, int(io.DisplaySize.x), int(io.DisplaySize.y));
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    const auto stopLoop = std::chrono::high_resolution_clock::now();
    app->execTime       = std::chrono::duration_cast<std::chrono::milliseconds>(stopLoop - startLoop);
    SDL_GL_SwapWindow(app->sdlState->window);
    setWindowMode(app->sdlState->window, app->windowMode);
}
