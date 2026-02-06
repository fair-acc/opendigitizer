#include "common/ImGuiHelperSDL.hpp"

#include "utils/EmscriptenHelper.hpp"

#define FORCE_PERIODIC_TIMERS true
#include "common/FramePacer.hpp"
#include <PeriodicTimer.hpp>

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
#include "blocks/TestSpectrumGenerator.hpp"

#include <version.hpp>

// static inline gr::profiling::Profiler profiler{gr::profiling::Options{}}; // TODO: use this once GR4 is bumped, then remove
static inline auto profiler = gr::profiling::null::Profiler{};

void registerDefaultThreadPool() {
    using namespace gr::thread_pool;
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, 1U, 1U), "CPU"));
}

// Process SDL events and mark frame dirty on relevant input
static bool processEventsWithPacer(DigitizerUi::FramePacer& pacer) {
    SDL_Event event;
    bool      hasInputEvent = false;

    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (imgui_helper::isWindowEventForOtherWindow(event, imgui_helper::g_Window)) {
            continue;
        }

        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return false;

        case SDL_EVENT_WINDOW_RESTORED:
            DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::RESTORED;
            hasInputEvent                                          = true;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED: DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::MINIMISED; break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::MAXIMISED;
            hasInputEvent                                          = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            const int width            = event.window.data1;
            const int height           = event.window.data2;
            ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
            glViewport(0, 0, width, height);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
            hasInputEvent = true;
            break;
        }

        // Input events that should trigger a frame
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION: hasInputEvent = true; break;

        default: break;
        }

        DigitizerUi::TouchHandler<>::processSDLEvent(event);
    }

    // Fire application callbacks (may also request frames)
    DigitizerUi::EventLoop::instance().fireCallbacks();
    DigitizerUi::TouchHandler<>::updateGestures();

    if (hasInputEvent) {
        pacer.requestFrame();
    }

    return true;
}

// Render a single frame (no event processing)
static void renderFrameOnly(App* app, gr::profiling::PeriodicTimer& tim) {
    tim.begin();

    imgui_helper::newFrame();
    TouchHandler<>::applyToImGui();

    app->processAndRender();
    tim.snapshot("processAndRender");

    components::Notification::render();

    imgui_helper::renderFrame();
    tim.snapshot("renderFrame");
    tim.snapshot("total", gr::profiling::kBegin);

    const auto  now                                      = std::chrono::high_resolution_clock::now();
    static auto lastFrame                                = now;
    DigitizerUi::LookAndFeel::mutableInstance().execTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame);
    lastFrame                                            = now;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// Track visibility for power saving
static bool g_isVisible = true;

static EM_BOOL emVisibilityCallback(int eventType, const EmscriptenVisibilityChangeEvent* event, void* /*userData*/) {
    g_isVisible = !event->hidden;
    if (g_isVisible) {
        // Waking up from hidden: request a frame
        DigitizerUi::globalFramePacer().requestFrame();
    }
    return EM_TRUE;
}

// Emscripten callback wrapper (uses global pacer)
// Browser calls this at up to 60 Hz via requestAnimationFrame
static void emscriptenMainLoop(void* arg) {
    using namespace std::chrono_literals;
    using namespace gr::profiling;

    thread_local static PeriodicTimer tim{profiler.forThisThread(), "renderFrame-Loop", "diag", 2000ms, true};

    auto* app   = static_cast<App*>(arg);
    auto& pacer = DigitizerUi::globalFramePacer();

    // Process SDL events (marks pacer dirty on input)
    app->isRunning = processEventsWithPacer(pacer);

    if (!app->isRunning) {
        emscripten_cancel_main_loop();
        return;
    }

    // Skip rendering if:
    // 1. Tab is hidden (browser throttles to 1 Hz anyway, but we can skip entirely)
    // 2. FramePacer says no render needed
    if (!g_isVisible && !pacer.shouldRender()) {
        return; // Skip this frame entirely
    }

    if (pacer.shouldRender()) {
        renderFrameOnly(app, tim);
        pacer.rendered();
    }
    // If shouldRender() is false, we just return without rendering
    // Browser called us, but nothing changed — save GPU work
}
#endif

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    registerDefaultThreadPool();

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

    Digitizer::Settings::instance();
    opendigitizer::ColourManager::instance();

    // Register blocks
    auto* registry = grGlobalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(*registry);
    gr::blocklib::initGrElectricalBlocks(*registry);
    gr::blocklib::initGrFileIoBlocks(*registry);
    gr::blocklib::initGrFilterBlocks(*registry);
    gr::blocklib::initGrFourierBlocks(*registry);
    gr::blocklib::initGrHttpBlocks(*registry);
    gr::blocklib::initGrMathBlocks(*registry);
    gr::blocklib::initGrTestingBlocks(*registry);

    // Register schedulers
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>>();

    static App app;
#ifdef EMSCRIPTEN
    app.executable = "index.html";
#else
    app.executable = argv[0];
#endif

    DigitizerUi::LookAndFeel::mutableInstance().loadFonts();
    app.init(argc, argv);

#ifdef __EMSCRIPTEN__
    // Configure pacer for Emscripten
    auto& pacer = DigitizerUi::globalFramePacer();
    pacer.setMinRate(1.0);  // min 1 Hz (idle/hidden)
    pacer.setMaxRate(60.0); // max 60 Hz

    // register visibility callback (for tab hidden/shown)
    emscripten_set_visibilitychange_callback(nullptr, EM_FALSE, emVisibilityCallback);

    // Use 0 for fps to let browser control via requestAnimationFrame (most efficient)
    // Third param: simulate_infinite_loop must be true to not return from main
    emscripten_set_main_loop_arg(emscriptenMainLoop, &app, 0, EM_TRUE);
#else
    // native: disable driver vsync (we pace ourselves)
    SDL_GL_SetSwapInterval(0);

    // Use the GLOBAL FramePacer - same instance that data sources call requestFrame() on
    auto& pacer = DigitizerUi::globalFramePacer();
    pacer.setMinRate(1.0);  // min 1 Hz (idle refresh for clock)
    pacer.setMaxRate(60.0); // max 60 Hz

    std::println("[Main] Event-driven rendering: min {:.1f}Hz, max {:.1f}Hz", pacer.minRateHz(), pacer.maxRateHz());

    thread_local static gr::profiling::PeriodicTimer tim{profiler.forThisThread(), "renderFrame-Loop", "diag", 2000ms, true};

    pacer.resetMeasurement();

    while (app.isRunning) {
        // wait for events OR timeout (true sleep, not busy-wait)
        const int timeout = pacer.getWaitTimeoutMs();
        SDL_WaitEventTimeout(nullptr, timeout);

        // process all pending events
        app.isRunning = processEventsWithPacer(pacer);

        // render if needed (event-driven or forced refresh)
        if (pacer.shouldRender()) {
            renderFrameOnly(&app, tim);
            pacer.rendered();
        }
    }
#endif

    app.closeDashboard();
    app.restClient.reset();
    imgui_helper::teardownSDL();
    return 0;
}
