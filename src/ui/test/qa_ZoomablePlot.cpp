#include "ImGuiTestApp.hpp"

#include "../common/TouchHandler.hpp"

#include <boost/ut.hpp>

#include <implot.h>

using namespace boost::ut;

namespace {
struct TestState {
    ImGuiID keyInWindowA = 0;
    ImGuiID keyInWindowB = 0;
};

ImGuiID drawZoomablePlotIn(const char* windowName, ImVec2 position) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize({300.f, 200.f});
    IMW::Window window(windowName, nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGuiID     key = 0;
    if (DigitizerUi::TouchHandler<>::BeginZoomablePlot("chart", ImVec2(-1, -1), ImPlotFlags_None)) {
        key = DigitizerUi::TouchHandler<>::zoomablePlotInit;
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
    }
    return key;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "zoomableplot", "zoom limits per window");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            auto& vars        = ctx->GetVars<TestState>();
            vars.keyInWindowA = drawZoomablePlotIn("Window A", {0.f, 0.f});
            vars.keyInWindowB = drawZoomablePlotIn("Window B", {320.f, 0.f});
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "charts of the same name in different windows keep separate zoom limits"_test = [ctx] {
                ctx->Yield(2);
                const auto& vars = ctx->GetVars<TestState>();
                expect(vars.keyInWindowA != 0u && vars.keyInWindowB != 0u) << fatal << "both plots are drawn";
                expect(vars.keyInWindowA != vars.keyInWindowB);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "zoomable_plot";
    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
