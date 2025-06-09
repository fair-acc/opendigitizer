#include "common/ImGuiHelperSDL.hpp"

#include "utils/EmscriptenHelper.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

#include "App.hpp"
#include "common/AppDefinitions.hpp"
#include "common/Events.hpp"
#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "common/TouchHandler.hpp"
#include "components/ImGuiNotify.hpp"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <GrBasicBlocks.hpp>
#include <GrElectricalBlocks.hpp>
#include <GrFileIoBlocks.hpp>
#include <GrFilterBlocks.hpp>
#include <GrFourierBlocks.hpp>
#include <GrHttpBlocks.hpp>
#include <GrMathBlocks.hpp>
#include <GrTestingBlocks.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/ConverterBlocks.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>

#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TagToSample.hpp"

#include <version.hpp>

static void renderFrame(void* arg) {
    const auto startLoop = std::chrono::high_resolution_clock::now();
    auto*      app       = static_cast<App*>(arg);

    // Poll and handle events (inputs, window resize, etc.)
    app->isRunning = imgui_helper::processEvents();

    EventLoop::instance().fireCallbacks();
    TouchHandler<>::updateGestures();

    // Start the Dear ImGui frame
    imgui_helper::newFrame();
    TouchHandler<>::applyToImGui();

    app->processAndRender();

    components::Notification::render();

    // Rendering
    imgui_helper::renderFrame();

    const auto stopLoop                     = std::chrono::high_resolution_clock::now();
    LookAndFeel::mutableInstance().execTime = std::chrono::duration_cast<std::chrono::milliseconds>(stopLoop - startLoop);
}

int main(int argc, char** argv) {
    auto basename = [](std::string_view path) {
        if (std::size_t pos = path.rfind('/'); pos != std::string_view::npos) {
            return path.substr(pos + 1);
        }
        return path;
    };
    std::println("[Main] started: {} - {}", basename(argv[0]), kOpendigitizerVersion);

    std::string glslVersion;
    if (!imgui_helper::initSDL(glslVersion, "OpenDigitizer UI", ImVec2{1280.f, 720.f})) {
        std::println(stderr, "[Main] SDL3 initialisation failed.");
        return 1;
    }

    if (!imgui_helper::initImGui(glslVersion)) {
        std::println(stderr, "[Main] ImGui initialisation failed.");
        return 1;
    }

    Digitizer::Settings::instance();          // TODO do we need this here?
    opendigitizer::ColourManager::instance(); // rstein: possibly -> both are singletons

    // TODO: Remove when GR gets proper blocks library
    auto* registry = grGlobalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(*registry);
    gr::blocklib::initGrElectricalBlocks(*registry);
    gr::blocklib::initGrFileIoBlocks(*registry);
    gr::blocklib::initGrFilterBlocks(*registry);
    gr::blocklib::initGrFourierBlocks(*registry);
    gr::blocklib::initGrHttpBlocks(*registry);
    gr::blocklib::initGrMathBlocks(*registry);
    gr::blocklib::initGrTestingBlocks(*registry);

    // UI init
    LookAndFeel::mutableInstance().verticalDPI = []() {
        if (float scale = SDL_GetDisplayContentScale(0); scale > 0.f) {
            return LookAndFeel::instance().defaultDPI * scale;
        }
        auto msg = std::format("[Main] Failed to obtain content scale for display 0: {}", SDL_GetError());
        components::Notification::error(msg);
        return LookAndFeel::instance().defaultDPI;
    }();

    // EMSCRIPTEN ends main before it enters the main loop,
    // app needs to live forever, so it is static
    static App app;
#ifdef EMSCRIPTEN
    app.executable = "index.html";
#else
    app.executable = argv[0];
#endif

    LookAndFeel::mutableInstance().loadFonts();

    app.init(argc, argv);

#ifdef __EMSCRIPTEN__
    emscripten_set_visibilitychange_callback(nullptr, false, em_visibilitychange_callback); // keep rendering alive even on hidden browser tabs or minimised windows
    emscripten_set_main_loop_arg(renderFrame, &app, 0, true);                               // main UI-loop (EMSCRIPTEN) - blocks
#else
    SDL_GL_SetSwapInterval(1); // Enable vsync

    while (app.isRunning) { // main UI-loop - blocks
        renderFrame(&app);
    }
#endif

    imgui_helper::teardownSDL();

    return 0;
}
