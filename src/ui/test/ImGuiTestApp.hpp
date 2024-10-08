#ifndef OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_
#define OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_

#include "imgui_test_engine/imgui_te_engine.h"

class ImGuiApp;
class ImGuiTestEngine;
class ImGuiTestEngineIO;

namespace DigitizerUi::test {

struct TestOptions {
    // If true, tests are started manually by the user, via a dialog
    // mostly used for debugging a test
    bool useInteractiveMode = false;

    // If cursor should teleport or move at human speed
    ImGuiTestRunSpeed speedMode = ImGuiTestRunSpeed::ImGuiTestRunSpeed_Fast;
};

/**
   Helper to share ImGui, ImGuiTestEngine and SDL setup code through all tests

   It's responsible for setup, cleanup and main loop. All you need to do is write your
   tests in registerTests() and then call runTests().
 */
class ImGuiTestApp {
    ImGuiApp*          _app      = nullptr;
    ImGuiTestEngine*   _engine   = nullptr;
    ImGuiTestEngineIO* _engineIO = nullptr;
    const TestOptions  _options  = {};

public:
    explicit ImGuiTestApp(const TestOptions& = {});

    // Frees ImGui and SDL resources
    virtual ~ImGuiTestApp();

    // Runs the gui tests and returns true on success
    bool runTests();

protected:
    virtual void registerTests() = 0;

    ImGuiTestEngine* engine() const;

private:
    void initImGui();

    ImGuiTestApp(const ImGuiTestApp&)            = delete;
    ImGuiTestApp& operator=(const ImGuiTestApp&) = delete;
};

} // namespace DigitizerUi::test

#endif
