#include "ImGuiTestApp.hpp"

#include "components/PopupMenu.hpp"
#include <boost/ut.hpp>

using namespace gr::od;

using namespace boost;

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool pressed = false;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "popup_menu", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            imw::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(500, 500));

            VerticalPopupMenu<1> menu;

            if (!menu.isOpen()) {
                menu.addButton("button", [ctx] {
                    auto& vars   = ctx->GetVars<TestState>();
                    vars.pressed = true;
                });
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("my test") = [ctx] {
                ctx->SetRef("Test Window");
                captureScreenshot(*ctx);

                ctx->SetRef("MenuPopup_1");
                ctx->ItemClick("button");

                auto& vars = ctx->GetVars<TestState>();
                ut::expect(vars.pressed);
            };
        };
    }
};

int main(int, char**) {
    TestApp app({.useInteractiveMode = false, .screenshotPrefix = "popup_menu"});
    return app.runTests() ? 0 : 1;
}
