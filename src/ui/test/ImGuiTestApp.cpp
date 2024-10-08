#include "ImGuiTestApp.hpp"

#include "imgui_test_engine/imgui_te_exporters.h"
#include "imgui_test_engine/imgui_te_internal.h"
#include "imgui_test_engine/imgui_te_ui.h"
#include "shared/imgui_app.h"

using namespace DigitizerUi::test;

ImGuiTestApp::ImGuiTestApp(const TestOptions& options) : _options(options) {}

ImGuiTestApp::~ImGuiTestApp() {
    ImGuiTestEngine_Stop(_engine);
    _app->ShutdownBackends(_app);
    _app->ShutdownCloseWindow(_app);
    ImGui::DestroyContext();

    ImGuiTestEngine_DestroyContext(_engine);

    _app->Destroy(_app);
}

void ImGuiTestApp::initImGui() {
    _app = ImGuiApp_ImplSdlGL3_Create();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

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
    test_io.ScreenCaptureFunc         = ImGuiApp_ScreenCaptureFunc;
    test_io.ScreenCaptureUserData     = _app;
    test_io.ConfigLogToTTY            = true;
    // Optional: save test output in junit-compatible XML format.
    // test_io.ExportResultsFile = "./results.xml";
    // test_io.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;

    // Start test engine
    ImGuiTestEngine_Start(_engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_InstallDefaultCrashHandler();

    registerTests();
}

bool ImGuiTestApp::runTests() {
    initImGui();

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
