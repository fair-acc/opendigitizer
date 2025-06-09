#include "ImGuiTestApp.hpp"

#include "components/Dialog.hpp"
#include <boost/ut.hpp>

using namespace boost;
using namespace DigitizerUi::components;

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool         okEnabled = true;
            DialogButton pressedButton;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dialog", "testButtonStates");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 300));

            auto&        vars   = ctx->GetVars<TestState>();
            DialogButton button = DialogButtons(vars.okEnabled);

            if (button != DialogButton::None) {
                vars.pressedButton = button;
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("test button states") = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");

                // OK button should be visually enabled
                captureScreenshot(*ctx);

                ut::expect(vars.pressedButton == DialogButton::None);

                ctx->ItemClick("Cancel");
                ut::expect(vars.pressedButton == DialogButton::Cancel);

                ctx->ItemClick("Ok");
                ut::expect(vars.pressedButton == DialogButton::Ok);
                vars.okEnabled = false;

                // OK button should be visually disabled
                captureScreenshot(*ctx);

                // Clicking the disabled item shouldn't have any effect
                vars.pressedButton = DialogButton::None;
                ctx->ItemClick("Ok");
                ut::expect(vars.pressedButton == DialogButton::None);

                // Still disabled
                captureScreenshot(*ctx);

                // Test keyboard Enter/Esc
                vars.okEnabled = true;
                ctx->KeyPress(ImGuiKey_Enter);
                ut::expect(vars.pressedButton == DialogButton::Ok);
                ctx->KeyPress(ImGuiKey_Escape);
                ut::expect(vars.pressedButton == DialogButton::Cancel);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dialog";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
