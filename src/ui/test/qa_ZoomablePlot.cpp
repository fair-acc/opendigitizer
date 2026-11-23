#include "ImGuiTestApp.hpp"

#include "../common/TouchHandler.hpp"

#include <boost/ut.hpp>

#include <implot.h>

using namespace gr::od;

using namespace boost::ut;

namespace {
struct TestState {
    ImGuiID keyInWindowA = 0;
    ImGuiID keyInWindowB = 0;
};

ImGuiID drawZoomablePlotIn(const char* windowName, ImVec2 position) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize({300.f, 200.f});
    imw::Window window(windowName, nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGuiID     key = 0;
    if (TouchHandler<>::beginZoomablePlot("chart", ImVec2(-1, -1), ImPlotFlags_None)) {
        key = TouchHandler<>::zoomablePlotInit;
        TouchHandler<>::endZoomablePlot();
    }
    return key;
}
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

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
                const auto& vars = ctx->GetVars<TestState>();
                test::waitUntil(ctx, "both plots are drawn", [&vars] { return vars.keyInWindowA != 0u && vars.keyInWindowB != 0u; });
                expect(vars.keyInWindowA != 0u && vars.keyInWindowB != 0u) << fatal << "both plots are drawn";
                expect(vars.keyInWindowA != vars.keyInWindowB);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "zoomable_plot";
    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
