#include "ImGuiTestApp.hpp"
#include "components/Keypad.hpp"

#include <boost/ut.hpp>

using namespace gr::od;

using namespace boost;

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool edited = false;
            int  value  = 0;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "keypad", "keypad visual test");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            imw::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 300));

            auto& vars = ctx->GetVars<TestState>();
            if (components::InputKeypad::edit("item_source", "label", &vars.value)) {
                // do not override with false every frame
                vars.edited = true;
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("keypad visual test") = [ctx] {
                ImGuiContext& g = *GImGui;

                ut::expect(g.OpenPopupStack.empty());

                ctx->SetRef("Test Window");
                ctx->ItemClick("label");

                ut::expect(g.OpenPopupStack.size() == 1);
                auto keypadWindow = ImGui::FindWindowByID(g.OpenPopupStack[0].Window->ID);

                ctx->SleepNoSkip(0.2f, 0.02f); // the modal background fades in over 1/6 s of frame time
                captureScreenshot(*ctx, keypadWindow->ID);

                auto& vars = ctx->GetVars<TestState>();
                ut::expect(vars.value == 0);
                ut::expect(!vars.edited);

                // the Keypad's buttons live in a child window
                auto subWindowInfo = ctx->WindowInfo("//KeypadX/drawKeypad Input");
                ctx->SetRef(subWindowInfo.Window->ID);

                ctx->ItemClick("9");
                ctx->ItemClick("Enter");
                ut::expect(vars.value == 9);
                ut::expect(vars.edited);

                ut::expect(g.OpenPopupStack.empty());
            };

            ut::test("operator button replaces a typed operator") = [ctx] {
                ImGuiContext& g    = *GImGui;
                auto&         vars = ctx->GetVars<TestState>();

                ctx->SetRef("Test Window");
                ctx->ItemClick("label");
                ut::expect(g.OpenPopupStack.size() == 1);

                ctx->SetRef(ctx->WindowInfo("//KeypadX/drawKeypad Input").Window->ID);
                ctx->ItemInputValue("##hidden", "5*");
                ctx->ItemClick("+");
                ctx->ItemClick("3");
                ctx->ItemClick("Enter"); // evaluates
                ctx->ItemClick("Enter"); // accepts
                ut::expect(vars.value == 8) << vars.value;
                ut::expect(g.OpenPopupStack.empty());
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "keypad";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
