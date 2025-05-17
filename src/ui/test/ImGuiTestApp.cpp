#include "ImGuiTestApp.hpp"

#include "App.hpp"
#include "imgui_test_engine/imgui_te_exporters.h"
#include "imgui_test_engine/imgui_te_internal.h"
#include "imgui_test_engine/imgui_te_ui.h"
#include "implot.h"
#include "shared/imgui_app.h"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <string_view>

using namespace DigitizerUi::test;

namespace {

// For internal usage only. No need for now to expose singleton API.
// Many of ImGuiTestEngine's callbacks don't accept lambda captures, which
// makes it hard to access ImGuiTestApp. ImGui's suggested way is to pass in its "void *userData"
// member, but let's rather be explicit and store it in a typed variable.

ImGuiTestApp* g_testApp = nullptr;

} // namespace

ImGuiTestApp::ImGuiTestApp(const TestOptions& options) : _options(options) {
    assert(g_testApp == nullptr);
    g_testApp = this;
}

ImGuiTestApp::~ImGuiTestApp() {
    ImGuiTestEngine_Stop(_engine);
    _app->ShutdownBackends(_app);
    _app->ShutdownCloseWindow(_app);
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    ImGuiTestEngine_DestroyContext(_engine);

    _app->Destroy(_app);

    g_testApp = nullptr;
}

void ImGuiTestApp::initImGui() {
    _app = ImGuiApp_ImplSdlGL3_Create();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    // Setup application. Values copied from upstream examples.
    // If some are interesting to change, consider adding them to our TestOptions struct
    _app->DpiAware        = false;
    _app->SrgbFramebuffer = false;
    _app->ClearColor      = ImVec4(0.120f, 0.120f, 0.120f, 1.000f);
    _app->InitCreateWindow(_app, "Test", ImVec2(1600, 1000));
    _app->InitBackends(_app);

    // Setup test engine
    _engine                           = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& test_io        = ImGuiTestEngine_GetIO(_engine);
    test_io.ConfigVerboseLevel        = ImGuiTestVerboseLevel_Info;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.ConfigRunSpeed            = _options.speedMode;
    test_io.ConfigKeepGuiFunc         = _options.keepGui;
    test_io.ScreenCaptureFunc         = ImGuiApp_ScreenCaptureFunc;
    test_io.ScreenCaptureUserData     = _app;
    test_io.ConfigCaptureOnError      = true;
    test_io.ConfigLogToTTY            = true;
    test_io.ConfigWatchdogWarning     = 60.0f;  // 1 minutes until a we get a warning that a test is taking too long
    test_io.ConfigWatchdogKillTest    = 180.0f; // 3 minutes until a test gets killed

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Optional: save test output in junit-compatible XML format.
    // test_io.ExportResultsFile = "./results.xml";
    // test_io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;

    // Start test engine
    ImGuiTestEngine_Start(_engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    LookAndFeel::mutableInstance().loadFonts();
    App::setImGuiStyle(LookAndFeel::Style::Dark);

    registerTests();
}

bool ImGuiTestApp::runTests() {
    if (!_app) {
        initImGui();
    }

    bool aborted = false;

    if (!_options.useInteractiveMode) {
        // In non-interactive mode we queue the tests immediately, while in interactive mode
        // the user will click the "run" button
        ImGuiTestEngine_QueueTests(engine(), ImGuiTestGroup_Tests);
    }

    while (!aborted) {
        if (!_app->NewFrame(_app)) {
            aborted = true;
        }

        if (_app->Quit) {
            aborted = true;
        }

        if (!_options.useInteractiveMode && ImGuiTestEngine_IsTestQueueEmpty(engine())) {
            // All tests ran
            aborted = true;
        }

        if (aborted && ImGuiTestEngine_TryAbortEngine(_engine)) {
            break;
        }

        ImGui::NewFrame();

        if (_options.useInteractiveMode) {
            // Shows pretty dialog with list of test and results
            ImGuiTestEngine_ShowTestEngineWindows(_engine, nullptr);
        }

        // Render and swap
        _app->Vsync = !ImGuiTestEngine_GetIO(_engine).IsRequestingMaxAppSpeed;
        ImGui::Render();
        _app->Render(_app);

        // Post-swap handler is REQUIRED in order to support screen capture
        ImGuiTestEngine_PostSwap(_engine);
    }

    int count_tested  = 0;
    int count_success = 0;
    ImGuiTestEngine_GetResult(engine(), count_tested, count_success);

    return count_tested == count_success;
}

ImGuiTestEngine* ImGuiTestApp::engine() const { return _engine; }

/** static */
void ImGuiTestApp::captureScreenshot(ImGuiTestContext& ctx, ImGuiTestRef ref, int captureFlags) {
    ctx.CaptureReset();

    { // choose a nice name for the output file
        auto*      args          = ctx.CaptureArgs;
        static int suffixCounter = 0;
        suffixCounter++;

        ImFormatString(args->InOutputFile, IM_ARRAYSIZE(args->InOutputFile), OPENDIGITIZER_BUILD_DIRECTORY "/captures/%s_%04d%s", g_testApp->_options.screenshotPrefix, suffixCounter, ".png");
    }

    ctx.CaptureAddWindow(ref);
    ctx.CaptureScreenshot(captureFlags);
}

TestOptions TestOptions::fromArgs(int argc, char* argv[]) {
    std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    auto      hasArgument = [args](std::string_view arg) { return std::any_of(args.cbegin(), args.cend(), [arg](const char* v) { return arg == v; }); };

    if (hasArgument("--help") || hasArgument("-h")) {
        std::println(stdout, "Usage: {} [--keep-gui][--interactive]", argv[0]);
    }

    TestOptions options;
    options.keepGui            = hasArgument("--keep-gui");
    options.useInteractiveMode = hasArgument("--interactive");

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

std::shared_ptr<gr::PluginLoader> ImGuiTestApp::createPluginLoader() {
    auto loader = std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), std::span<const std::filesystem::path>());
    return loader;
}
