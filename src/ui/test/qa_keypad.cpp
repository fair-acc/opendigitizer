#include "ImGuiTestApp.hpp"
#include "components/Keypad.hpp"
#include "imgui.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_internal.h"

#include <boost/ut.hpp>

using namespace boost;

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool edited = false;
            int  value  = 0;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "keypad", "keypad visual test");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 300));

            auto& vars = ctx->GetVars<TestState>();
            if (DigitizerUi::components::InputKeypad<>::edit("label", &vars.value)) {
                // do not override with false every frame
                vars.edited = true;
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("keypad visual test") = [ctx] {
                ImGuiContext& g = *GImGui;

                // keypad isn't visible yet
                ut::expect(g.OpenPopupStack.empty());

                ctx->SetRef("Test Window");
                ctx->ItemClick("label");

                ut::expect(g.OpenPopupStack.size() == 1);
                auto keypadWindow = ImGui::FindWindowByID(g.OpenPopupStack[0].Window->ID);

                captureScreenshot(*ctx, keypadWindow->ID);

                // nothing edited yet
                auto& vars = ctx->GetVars<TestState>();
                ut::expect(vars.value == 0);
                ut::expect(!vars.edited);

                // KeyPads buttons are actually in a child window
                auto subWindowInfo = ctx->WindowInfo("//KeypadX/drawKeypad Input");
                ctx->SetRef(subWindowInfo.Window->ID);

                // press some buttons
                ctx->ItemClick("9");
                ctx->ItemClick("Enter");
                ut::expect(vars.value == 9);
                ut::expect(vars.edited);

                // no keypad now
                ut::expect(g.OpenPopupStack.empty());
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "keypad";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
