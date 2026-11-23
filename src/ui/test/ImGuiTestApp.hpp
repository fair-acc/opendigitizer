#ifndef OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_
#define OPENDIGITIZER_UI_TEST_IMGUI_TEST_APP_HPP_

#include "../common/ImGuiHelperSDL.hpp"
#include "../common/Imw.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_internal.h"
#pragma GCC diagnostic pop

#include <memory>
#include <print>
#include <source_location>
#include <string_view>
#include <vector>

class ImGuiApp;
struct ImGuiTestEngine;
struct ImGuiTestEngineIO;

namespace gr::od::test {

struct TestOptions {
    // the tests start from a dialog, for debugging a test
    bool useInteractiveMode = false;

    // after the tests, for visual debugging
    bool keepGui = false;

    // the cursor teleports or moves at human speed
    ImGuiTestRunSpeed speedMode = ImGuiTestRunSpeed::ImGuiTestRunSpeed_Fast;

    // e.g. the test name
    const char* screenshotPrefix = "";

    // run only the tests whose name matches this imgui-test-engine filter pattern
    std::string testFilter{};

    ImVec2 windowSize{1600.f, 1000.f};

    static TestOptions fromArgs(int argc, char** argv);
};

// ImGui, ImGuiTestEngine and SDL set-up, clean-up and main loop shared by the tests: they register in registerTests(), then runTests() runs them
struct ImGuiTestApp {
    struct CapturedPixels {
        int                       width  = 0;
        int                       height = 0;
        std::vector<unsigned int> rgba; // RGBA8, row-major
    };

    std::unique_ptr<ImGuiApp> _app;
    ImGuiTestEngine*          _engine  = nullptr;
    const TestOptions         _options = {};

    explicit ImGuiTestApp(const TestOptions& = {});

    virtual ~ImGuiTestApp();

    // true on success
    bool runTests();

    ImGuiTestContext* testContext() const;

    // implicit in runTests(); public for an earlier ImGui context
    void initImGui();

    // like ImGuiTestContext::CaptureScreenshotWindow, but stored where this app decides; `ref` is an ImGuiID or a path of the widget, by default the window
    static void captureScreenshot(ImGuiTestContext& ctx, ImGuiTestRef ref = "/", int captureFlags = ImGuiCaptureFlags_HideMouseCursor | ImGuiCaptureFlags_IncludePopups | ImGuiCaptureFlags_IncludeOtherWindows);

    static void captureScreenshot(ImGuiTestContext& ctx, const ImRect& screenRect);

    [[nodiscard]] static CapturedPixels capturePixels(ImGuiTestContext& ctx, const ImRect& screenRect);
    // the first capture equal to the one of the frame before: the drawing has settled
    [[nodiscard]] static CapturedPixels captureSettledPixels(ImGuiTestContext& ctx, const ImRect& screenRect);

    // for debugging
    static void printWindows();

    virtual void registerTests() = 0;

    ImGuiTestEngine* engine() const;
};

// blocks until `condition` holds, processing UI frames.
// There is deliberately no deadline: the outcome must not depend on machine load, and a condition that never holds is caught by the ctest timeout.
template<typename Condition>
bool waitUntil(ImGuiTestContext* ctx, std::string_view what, Condition&& condition, std::source_location location = std::source_location::current()) {
    std::println("\twaiting until {} ({}:{})", what, location.file_name(), location.line());
    while (!condition()) {
        ctx->Yield();
    }
    return true;
}
} // namespace gr::od::test

#endif
