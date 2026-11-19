#include "common/ImGuiHelperSDL.hpp"

#include "utils/Browser.hpp"

#define FORCE_PERIODIC_TIMERS true
#include "common/FramePacer.hpp"
#include <PeriodicTimer.hpp>

#include <algorithm>
#include <array>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

#include "App.hpp"
#include "common/Events.hpp"
#include "common/Imw.hpp"
#include "common/LookAndFeel.hpp"
#include "common/TouchHandler.hpp"
#include "common/ViewMode.hpp"
#include "components/ImGuiNotify.hpp"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#ifndef __EMSCRIPTEN__
// gr-digitizers is a native-only run-time dependency (PicoScope SDK, USB); unavailable under emscripten.
#include <fair/picoscope/Picoscope.hpp>
#include <fair/picoscope/Picoscope4000a.hpp>
#endif

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/ConverterBlocks.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>

#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
#include <gnuradio-4.0/GrAnalogBlocks.hpp>
#include <gnuradio-4.0/GrAudioBlocks.hpp>
#if defined(OD_ACOUSTIC_CAMERA)
#include <gnuradio-4.0/GrLocalisationBlocks.hpp>
#endif
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrDigitalBlocks.hpp>
#include <gnuradio-4.0/GrElectricalBlocks.hpp>
#include <gnuradio-4.0/GrFileIoBlocks.hpp>
#include <gnuradio-4.0/GrFilterBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrHttpBlocks.hpp>
#include <gnuradio-4.0/GrMathBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/sdr/RTL2832Source.hpp>
#endif

#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
#include "blocks/ImControlRTL2832Connect.hpp"
#include "charts/RDSView.hpp"
#endif

#include <version.hpp>

using namespace gr::od;

static inline auto profiler = gr::profiling::null::Profiler{};

void registerDefaultThreadPool() {
    using namespace gr::thread_pool;
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, 1U, 1U), "CPU"));
}

static bool pollEventsAndPace(FramePacer& pacer) {
    const auto [running, hadInput] = sdl::pollEvents();
    if (running && hadInput) {
        pacer.requestFrame();
    }
    return running;
}

static void renderFrameOnly(App* app, gr::profiling::PeriodicTimer& tim) {
    tim.begin();

    sdl::newFrame();
    TouchHandler<>::endFrame();

    app->drawFrame();
    tim.snapshot("drawFrame");

    components::Notification::render();

    sdl::renderFrame();
    tim.snapshot("renderFrame");
    tim.snapshot("total", gr::profiling::kBegin);

    const auto  now       = std::chrono::high_resolution_clock::now();
    static auto lastFrame = now;
    gAppState.execTime    = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame);
    lastFrame             = now;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

static bool gIsVisible = true;

static EM_BOOL emVisibilityCallback([[maybe_unused]] int eventType, const EmscriptenVisibilityChangeEvent* event, void* /*userData*/) {
    gIsVisible = !event->hidden;
    if (gIsVisible) {
        globalFramePacer().requestFrame();
    }
    return EM_TRUE;
}

// the browser calls this at up to 60 Hz via requestAnimationFrame
static void emscriptenMainLoop(void* arg) {
    using namespace std::chrono_literals;
    using namespace gr::profiling;

    thread_local static PeriodicTimer tim{profiler.forThisThread(), "renderFrame-Loop", "diag", 2000ms, true};

    auto* app   = static_cast<App*>(arg);
    auto& pacer = globalFramePacer();

    app->isRunning = pollEventsAndPace(pacer);

    if (!app->isRunning) {
        emscripten_cancel_main_loop();
        return;
    }

    if (!gIsVisible && !pacer.shouldRender()) {
        return;
    }

    if (pacer.shouldRender()) {
        renderFrameOnly(app, tim);
        pacer.rendered();
    }
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
    if (!sdl::initSDL(glslVersion, "OpenDigitizer UI", ImVec2{1280.f, 720.f})) {
        std::println(stderr, "[Main] SDL3 initialisation failed.");
        return 1;
    }

    if (!sdl::initImGui(glslVersion)) {
        std::println(stderr, "[Main] ImGui initialisation failed.");
        return 1;
    }

    Digitizer::Settings::instance();
    initialise();

    // on WASM dynload the Gr* blocklibs arrive as SIDE_MODULE plugins; natively and WASM-static they are linked in and registered here
    auto* registry = grGlobalBlockRegistry();
#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
    gr::blocklib::initGrBasicBlocks(*registry);
    gr::blocklib::initGrElectricalBlocks(*registry);
    gr::blocklib::initGrFileIoBlocks(*registry);
    gr::blocklib::initGrFilterBlocks(*registry);
    gr::blocklib::initGrFourierBlocks(*registry);
    gr::blocklib::initGrHttpBlocks(*registry);
    gr::blocklib::initGrMathBlocks(*registry);
    gr::blocklib::initGrTestingBlocks(*registry);
    gr::blocklib::initGrAnalogBlocks(*registry);
    gr::blocklib::initGrAudioBlocks(*registry);
    gr::blocklib::initGrDigitalBlocks(*registry);
    charts::registerChart<charts::RDSView>(*registry);
    registerWithIdPrefix<ImControlRTL2832Connect>(*registry, kDigitizerUiIdPrefix);
    gr::registerBlock<gr::blocks::sdr::RTL2832Source<std::complex<float>>, "">(*registry);
#endif
#if defined(OD_ACOUSTIC_CAMERA)
    gr::blocklib::initGrLocalisationBlocks(*registry);
#endif

    // decimating BasicFilterProto variants: the ratio is the Resampling<in,out> template integers, and the gr::filter blocklib registers only the <1,1> identity (used by PulsedPowerDemo.grc)
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<100UZ, 1UZ, false>>, "">(*registry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<40UZ, 1UZ, false>>, "">(*registry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<1UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<float32, gr::Resampling<1, 1, false>>">(*registry);

#ifndef __EMSCRIPTEN__
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope4000a>, "">(*registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope4000a>, "">(*registry);
#endif

    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::BreadthFirst<gr::scheduler::ExecutionPolicy::singleThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::BreadthFirst<gr::scheduler::ExecutionPolicy::multiThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::DepthFirst<gr::scheduler::ExecutionPolicy::singleThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::DepthFirst<gr::scheduler::ExecutionPolicy::multiThreaded>>();

    static App app;

    app.init(argc, argv);
    app.statusBarView.versionDetails = std::format("{}\ngrc: ?", kOpendigitizerVersion);

#ifdef __EMSCRIPTEN__
    auto& pacer = globalFramePacer();
    pacer.setMinRate(1.0); // min 1 Hz (idle/hidden)
    pacer.setMaxRate(LookAndFeel::instance().maxFrameRate);

    emscripten_set_visibilitychange_callback(nullptr, EM_FALSE, emVisibilityCallback);

    // 0 fps: the browser paces via requestAnimationFrame; simulate_infinite_loop must be true so that main does not return
    emscripten_set_main_loop_arg(emscriptenMainLoop, &app, 0, EM_TRUE);
#else
    // native: disable driver vsync (we pace ourselves)
    SDL_GL_SetSwapInterval(0);

    // the global pacer, on which data sources call requestFrame()
    auto& pacer = globalFramePacer();
    pacer.setMinRate(1.0); // min 1 Hz (idle refresh for clock)
    pacer.setMaxRate(LookAndFeel::instance().maxFrameRate);

    std::println("[Main] Event-driven rendering: min {:.1f}Hz, max {:.1f}Hz", pacer.minRateHz(), pacer.maxRateHz());

    thread_local static gr::profiling::PeriodicTimer tim{profiler.forThisThread(), "renderFrame-Loop", "diag", 2000ms, true};

    pacer.resetMeasurement();

    while (app.isRunning) {
        const int timeout = pacer.getWaitTimeoutMs();
        SDL_WaitEventTimeout(nullptr, timeout);

        app.isRunning = pollEventsAndPace(pacer);

        if (pacer.shouldRender()) {
            renderFrameOnly(&app, tim);
            pacer.rendered();
        }
    }
#endif

    app.closeDashboard();
    app.restClient.reset();
    sdl::teardownSDL();
    return 0;
}
