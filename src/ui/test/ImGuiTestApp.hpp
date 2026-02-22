#ifndef OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_
#define OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_

#include "../common/ImGuiHelperSDL.hpp"
#include "../common/ImguiWrap.hpp"
using namespace DigitizerUi; // TODO refactor namespaces in ImguiWrap.hpp

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_internal.h"
#pragma GCC diagnostic pop

#include <memory>

class ImGuiApp;
struct ImGuiTestEngine;
struct ImGuiTestEngineIO;

namespace gr {
class PluginLoader;
}

namespace DigitizerUi::test {

struct TestOptions {
    // If true, tests are started manually by the user, via a dialog
    // mostly used for debugging a test
    bool useInteractiveMode = false;

    // Runs GuiFunc in a loop instead of quiting the application
    // Use this for visually debug your test.
    bool keepGui = false;

    // If cursor should teleport or move at human speed
    ImGuiTestRunSpeed speedMode = ImGuiTestRunSpeed::ImGuiTestRunSpeed_Fast;

    // Screenshot filenames can be prefixed with something. For instance, your test name
    const char* screenshotPrefix = "";

    // Returns a good default for TestOptions but influenced by program arguments
    static TestOptions fromArgs(int argc, char** argv);
};

/**
   Helper to share ImGui, ImGuiTestEngine and SDL setup code through all tests

   It's responsible for setup, cleanup and main loop. All you need to do is write your
   tests in registerTests() and then call runTests().
 */
class ImGuiTestApp {
    std::unique_ptr<ImGuiApp> _app;
    ImGuiTestEngine*          _engine   = nullptr;
    ImGuiTestEngineIO*        _engineIO = nullptr;
    const TestOptions         _options  = {};

public:
    explicit ImGuiTestApp(const TestOptions& = {});

    // Frees ImGui and SDL resources
    virtual ~ImGuiTestApp();

    // Runs the gui tests and returns true on success
    bool runTests();

    ImGuiTestContext* testContext() const;

    /**
      Initializes ImGui context and graphics

      Calling this is optional, as it's implicitly called by runTests().
      It's public in case you need to initialize ImGui earlier.
     */
    void initImGui();

    /**
      Captures a screenshot.

      Image is saved to disk.
      This signature is identical to ImGuiTestContext::CaptureScreenshotWindowm but we have our
      own implementation, so we can control where the images are actually stored.

      @param ctx test context
      @param ref An ImGuiID or a char* path that identifies the widget to be captured. Captures the window by default
      @param captureFlags See ImGui's ImGuiCaptureFlags_ enum
     */
    static void captureScreenshot(ImGuiTestContext& ctx, ImGuiTestRef ref = "/", int captureFlags = ImGuiCaptureFlags_HideMouseCursor | ImGuiCaptureFlags_IncludePopups | ImGuiCaptureFlags_IncludeOtherWindows);

    // Prints the existing window ids, for debugging purposes
    static void printWindows();

    // Creates a plugin loader and registers block definitions
    // Call this if your test uses gr blocks
    static std::shared_ptr<gr::PluginLoader> createPluginLoader();

protected:
    virtual void registerTests() = 0;

    ImGuiTestEngine* engine() const;

private:
    ImGuiTestApp(const ImGuiTestApp&)            = delete;
    ImGuiTestApp& operator=(const ImGuiTestApp&) = delete;
};

} // namespace DigitizerUi::test

#endif
