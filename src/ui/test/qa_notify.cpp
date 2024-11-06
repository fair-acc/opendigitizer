#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/ImGuiNotify.hpp"
#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

using namespace boost;

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {};

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "notify", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 300));

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("my test") = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "notify";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
