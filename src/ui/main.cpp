#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/basic/ConverterBlocks.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>

#include <fmt/format.h>

#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/ClockSource.hpp>
#include <gnuradio-4.0/basic/FunctionGenerator.hpp>
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

#include "App.hpp"
#include "Dashboard.hpp"
#include "DashboardPage.hpp"
#include "FlowgraphItem.hpp"

#include "common/AppDefinitions.hpp"
#include "common/TouchHandler.hpp"

#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TagToSample.hpp"

namespace DigitizerUi {

struct SDLState {
    SDL_Window*   window    = nullptr;
    SDL_GLContext glContext = nullptr;
};

} // namespace DigitizerUi

using namespace DigitizerUi;

static void main_loop(void*);

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

    // TODO: Remove when GR gets proper blocks library
    auto& registry = gr::globalBlockRegistry();
    gr::registerBlock<gr::blocks::type::converter::Convert, gr::BlockParameters<double, float>, gr::BlockParameters<float, double>>(registry);
    gr::registerBlock<"gr::blocks::fft::DefaultFFT", gr::blocks::fft::DefaultFFT, float>(registry);
    gr::registerBlock<gr::basic::DefaultClockSource, std::uint8_t, float>(registry);
    registry.template addBlockType<gr::basic::DefaultClockSource<std::uint8_t>>("gr::basic::ClockSource");
    gr::registerBlock<gr::basic::FunctionGenerator, float, double>(registry);
    gr::registerBlock<gr::basic::SignalGenerator, float>(registry);

    fmt::print("providedBlocks:\n");
    for (auto& blockName : gr::globalBlockRegistry().providedBlocks()) {
        fmt::print("  - {}\n", blockName);
    }

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

    // UI init
    LookAndFeel::mutableInstance().verticalDPI = []() -> float {
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

    auto& app = App::instance();
#ifdef EMSCRIPTEN
    app.executable = "index.html";
#else
    app.executable = argv[0];
#endif
    app.sdlState = &sdlState;

    LookAndFeel::mutableInstance().loadFonts();

    app.init(argc, argv);

    // This function call won't return, and will engage in an infinite loop, processing events from the browser, and dispatching them.
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    SDL_GL_SetSwapInterval(1); // Enable vsync

    while (app.isRunning) {
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

    // Poll and handle events (inputs, window resize, etc.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        switch (event.type) {
        case SDL_QUIT: app->isRunning = false; break;
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
            case SDL_WINDOWEVENT_CLOSE: app->isRunning = false; break;
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

    EventLoop::instance().fireCallbacks();
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

    app->processAndRender();

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
