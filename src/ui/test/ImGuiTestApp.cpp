#include "ImGuiTestApp.hpp"

#include "App.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_exporters.h"
#include "imgui_test_engine/imgui_te_internal.h"
#include "imgui_test_engine/imgui_te_ui.h"
#include "implot.h"
#include <magic_enum.hpp>
#pragma GCC diagnostic pop

#include "shared/imgui_app.h"

#include <gnuradio-4.0/Scheduler.hpp>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>

using namespace gr::od;

using namespace test;

namespace {

// the test engine's callbacks take no lambda captures: they reach the app through this typed pointer, not ImGui's void* userData

void registerDefaultThreadPool() {
    using namespace gr::thread_pool;
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, 1U, 1U), "CPU"));
    Manager::instance().replacePool(std::string(kDefaultIoPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultIoPoolId), TaskType::IO_BOUND, 0U), "IO"));
}

inline ImGuiTestApp* gTestApp = nullptr;

inline bool createWindow(ImGuiApp* /*app*/, const char* windowTitle, ImVec2 windowSize) {
    std::string glslVersion;
    if (!sdl::initSDL(glslVersion, windowTitle, windowSize, SDL_WINDOW_HIDDEN)) {
        std::println(stderr, "[Main] SDL3 initialisation failed.");
        return false;
    }
    if (!sdl::initImGui(glslVersion)) {
        std::println(stderr, "[Main] ImGui initialisation failed.");
        return false;
    }
    return true;
}

inline void initBackends(ImGuiApp* /*app*/) {}

inline bool newFrame(ImGuiApp* /*app*/) {
    if (!sdl::pollEvents().running) {
        std::println("[TestApp] requested close");
        return false;
    }
    EventLoop::instance().fireCallbacks();
    return sdl::newFrame();
}

[[nodiscard]] bool isDrawnFrameNeeded(const ImGuiTestEngine* engine) {
    const bool isWindowHidden = (SDL_GetWindowFlags(sdl::gWindow) & SDL_WINDOW_HIDDEN) != 0;
    return !isWindowHidden || engine->CaptureCurrentArgs != nullptr;
}

inline void render(ImGuiApp*) {} // the run loop renders, so that captures happen before the swap

inline void shutdownCloseWindow(ImGuiApp*) { sdl::teardownSDL(); }
inline void shutdownBackends(ImGuiApp*) { sdl::teardownSDL(); }

inline bool readFlippedFramebuffer(ImGuiApp* /*app*/, ImGuiViewport* /*viewport*/, int x, int y, int w, int h, unsigned int* pixels, void* /*userData*/) {
#ifdef __linux__
    // workaround: the compositor needs a moment to flush for an accurate capture
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

    // OpenGL reads from the bottom-left corner; need to flip
    int yFlipped = static_cast<int>(ImGui::GetIO().DisplaySize.y) - (y + h);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, yFlipped, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    const std::size_t bytesPerPixel = 4UZ;
    const std::size_t rowStride     = static_cast<std::size_t>(w) * bytesPerPixel;
    auto*             lineA         = reinterpret_cast<unsigned char*>(pixels);
    auto*             lineB         = lineA + (rowStride * static_cast<std::size_t>(h - 1));
    while (lineA < lineB) {
        std::swap_ranges(lineA, lineA + rowStride, lineB);
        lineA += rowStride;
        lineB -= rowStride;
    }

    return true;
}

inline bool captureFramebuffer(ImGuiApp* app, ImGuiViewport* viewport, int x, int y, int w, int h, unsigned int* pixels, void* userData) {
    // assumes the right GL context is current, without the backend's internal PlatformUserData
    IM_UNUSED(viewport);

    return readFlippedFramebuffer(app, viewport, x, y, w, h, pixels, userData);
}

} // namespace

ImGuiTestApp::ImGuiTestApp(const TestOptions& options) : _options(options) {
    assert(gTestApp == nullptr);
    registerDefaultThreadPool();
    gTestApp = this;
}

ImGuiTestApp::~ImGuiTestApp() {
    ImGuiTestEngine_Stop(_engine);
    _app->ShutdownBackends(&(*_app));
    _app->ShutdownCloseWindow(&(*_app));

    ImGuiTestEngine_DestroyContext(_engine);

    _app->Destroy(&(*_app));

    gTestApp = nullptr;
}

void ImGuiTestApp::initImGui() {
    _app                      = std::make_unique<ImGuiApp>();
    _app->InitCreateWindow    = createWindow;
    _app->InitBackends        = initBackends;
    _app->NewFrame            = newFrame;
    _app->Render              = render;
    _app->ShutdownCloseWindow = shutdownCloseWindow;
    _app->ShutdownBackends    = shutdownBackends;
    _app->CaptureFramebuffer  = captureFramebuffer;
    _app->Destroy             = [](ImGuiApp* /*app*/) { sdl::teardownSDL(); };

    // values from the upstream examples
    _app->DpiAware        = false;
    _app->SrgbFramebuffer = false;
    _app->ClearColor      = ImVec4(0.120f, 0.120f, 0.120f, 1.000f);
    _app->InitCreateWindow(&(*_app), "Test", _options.windowSize);
    if (_options.useInteractiveMode || _options.keepGui || std::getenv("OPENDIGITIZER_RUN_TESTS_ON_SCREEN") != nullptr) {
        SDL_ShowWindow(sdl::gWindow);
    }
    _app->InitBackends(&(*_app));

    _engine                          = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& testIo        = ImGuiTestEngine_GetIO(_engine);
    testIo.ConfigVerboseLevel        = ImGuiTestVerboseLevel_Info;
    testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    testIo.ConfigRunSpeed            = _options.speedMode;
    testIo.ConfigKeepGuiFunc         = _options.keepGui;
    testIo.ScreenCaptureFunc         = ImGuiApp_ScreenCaptureFunc;
    testIo.ScreenCaptureUserData     = &(*_app);
    testIo.ConfigCaptureOnError      = true;
    testIo.ConfigLogToTTY            = true;
    testIo.ConfigWatchdogWarning     = 60.0f;  // s until the slow-test warning
    testIo.ConfigWatchdogKillTest    = 180.0f; // s until the test is killed

    testIo.MouseSpeed *= 2.0f; // pixels per second, only used when speedMode is not ImGuiTestRunSpeed_Fast

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGuiTestEngine_Start(_engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    if (const char* testTheme = std::getenv("OPENDIGITIZER_TEST_THEME"); testTheme != nullptr) {
        LookAndFeel::mutableInstance().theme = magic_enum::enum_cast<LookAndFeel::Theme>(testTheme, magic_enum::case_insensitive).value_or(LookAndFeel::Theme::Classic);
    }
    const char* testStyle = std::getenv("OPENDIGITIZER_TEST_STYLE");
    initialise(testStyle != nullptr && std::string_view(testStyle) == "light" ? LookAndFeel::Style::Light : LookAndFeel::Style::Dark);

    registerTests();
}

bool ImGuiTestApp::runTests() {
    if (!_app) {
        initImGui();
    }

    bool aborted = false;

    if (!_options.useInteractiveMode) {
        // queued at once, unless in interactive mode, where the user clicks "run"
        ImGuiTestEngine_QueueTests(engine(), ImGuiTestGroup_Tests, _options.testFilter.empty() ? nullptr : _options.testFilter.c_str());
    }

    while (!aborted) {
        if (!_app->NewFrame(&(*_app))) {
            aborted = true;
        }

        if (_app->Quit) {
            aborted = true;
        }

        if (!_options.useInteractiveMode && ImGuiTestEngine_IsTestQueueEmpty(engine())) {
            aborted = true;
        }

        if (aborted && ImGuiTestEngine_TryAbortEngine(_engine)) {
            break;
        }

        if (_options.useInteractiveMode) {
            ImGuiTestEngine_ShowTestEngineWindows(_engine, nullptr);
        }

        _app->Vsync = !ImGuiTestEngine_GetIO(_engine).IsRequestingMaxAppSpeed;
        ImGui::Render();
        if (isDrawnFrameNeeded(_engine)) {
            // the capture reads the back buffer: only before the swap does it hold this frame
            sdl::renderFrame([this] { ImGuiTestEngine_PostSwap(_engine); });
            SDL_GL_SetSwapInterval(_app->Vsync ? 1 : 0);
        } else {
            ImGuiTestEngine_PostSwap(_engine);
        }
    }

    int countTested  = 0;
    int countSuccess = 0;
    ImGuiTestEngine_GetResult(engine(), countTested, countSuccess);

    return countTested == countSuccess;
}

ImGuiTestEngine* ImGuiTestApp::engine() const { return _engine; }

namespace {
void resetCaptureWithNextFileName(ImGuiTestContext& ctx, const char* prefix) {
    ctx.CaptureReset();
    static int suffixCounter = 0;
    suffixCounter++;
    ImFormatString(ctx.CaptureArgs->InOutputFile, IM_ARRAYSIZE(ctx.CaptureArgs->InOutputFile), OPENDIGITIZER_BUILD_DIRECTORY "/captures/%s_%04d%s", prefix, suffixCounter, ".png");
}
} // namespace

void ImGuiTestApp::captureScreenshot(ImGuiTestContext& ctx, ImGuiTestRef ref, int captureFlags) {
    resetCaptureWithNextFileName(ctx, gTestApp->_options.screenshotPrefix);
    ctx.CaptureAddWindow(ref);
    ctx.CaptureScreenshot(captureFlags);
}

void ImGuiTestApp::captureScreenshot(ImGuiTestContext& ctx, const ImRect& screenRect) {
    resetCaptureWithNextFileName(ctx, gTestApp->_options.screenshotPrefix);
    ctx.CaptureArgs->InCaptureRect = screenRect;
    ctx.CaptureScreenshot(ImGuiCaptureFlags_Instant | ImGuiCaptureFlags_HideMouseCursor);
}

ImGuiTestApp::CapturedPixels ImGuiTestApp::capturePixels(ImGuiTestContext& ctx, const ImRect& screenRect) {
    ImGuiCaptureImageBuf image;
    ctx.CaptureReset();
    ctx.CaptureArgs->InCaptureRect    = screenRect;
    ctx.CaptureArgs->InOutputImageBuf = std::addressof(image);
    ctx.CaptureScreenshot(ImGuiCaptureFlags_Instant | ImGuiCaptureFlags_HideMouseCursor | ImGuiCaptureFlags_NoSave);
    return {.width = image.Width, .height = image.Height, .rgba = std::vector<unsigned int>(image.Data, image.Data + static_cast<std::size_t>(image.Width) * static_cast<std::size_t>(image.Height))};
}

ImGuiTestApp::CapturedPixels ImGuiTestApp::captureSettledPixels(ImGuiTestContext& ctx, const ImRect& screenRect) {
    CapturedPixels previous;
    while (true) {
        ctx.Yield();
        CapturedPixels current = capturePixels(ctx, screenRect);
        if (current.width == previous.width && current.height == previous.height && current.rgba == previous.rgba) {
            return current;
        }
        previous = std::move(current);
    }
}

TestOptions TestOptions::fromArgs(int argc, char* argv[]) {
    std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    auto      hasArgument = [args](std::string_view arg) { return std::any_of(std::cbegin(args), std::cend(args), [arg](const char* v) { return arg == v; }); };

    if (hasArgument("--help") || hasArgument("-h")) {
        std::println(stdout, "Usage: {} [--keep-gui][--interactive][--filter <test name pattern>][--window <width>x<height>]\nThe window is hidden unless one of these flags is passed or OPENDIGITIZER_RUN_TESTS_ON_SCREEN is set.", argv[0]);
    }

    TestOptions options;
    options.keepGui            = hasArgument("--keep-gui");
    options.useInteractiveMode = hasArgument("--interactive");

    for (auto it = std::cbegin(args); it != std::cend(args); ++it) {
        if (std::string_view(*it) == "--filter" && std::next(it) != std::cend(args)) {
            options.testFilter = *std::next(it);
        }
        if (std::string_view(*it) == "--window" && std::next(it) != std::cend(args)) {
            const std::string_view size      = *std::next(it);
            const std::size_t      separator = size.find('x');
            int                    width     = 0;
            int                    height    = 0;
            if (separator != std::string_view::npos) {
                std::from_chars(size.data(), size.data() + separator, width);
                std::from_chars(size.data() + separator + 1, size.data() + size.size(), height);
            }
            if (width > 0 && height > 0) {
                options.windowSize = ImVec2{static_cast<float>(width), static_cast<float>(height)};
            }
        }
    }

    return options;
}

void ImGuiTestApp::printWindows() {
    ImGuiContext& g = *GImGui;
    std::println("printWindows:");
    std::println("    popupLevel={}", g.BeginPopupStack.size());
    std::println("    openPopups={}", g.OpenPopupStack.size());

    auto flagsString = [](ImGuiWindowFlags flags) -> std::string {
        return std::format("ChildWindow={}, ToolTip={}, Popup={}, Modal={}, ChildMenu={}", //
            flags & ImGuiWindowFlags_ChildWindow,                                          //
            flags & ImGuiWindowFlags_Tooltip,                                              //
            flags & ImGuiWindowFlags_Popup,                                                //
            flags & ImGuiWindowFlags_Modal,                                                //
            flags & ImGuiWindowFlags_ChildMenu);
    };

    for (const ImGuiWindowStackData& data : g.CurrentWindowStack) {
        std::println("    window name={}; flags={}", data.Window->Name, flagsString(data.Window->Flags));
    }

    for (const ImGuiPopupData& data : g.OpenPopupStack) {
        std::println("    popup name={}; flags={}", data.Window->Name, flagsString(data.Window->Flags));
    }
}

ImGuiTestContext* ImGuiTestApp::testContext() const { return _engine ? _engine->TestContext : nullptr; }
