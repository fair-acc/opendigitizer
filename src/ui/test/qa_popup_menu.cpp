#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/PopupMenu.hpp"
#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

using namespace boost;

/**
  Tests opening a PopupMenu and clicking on its button

  Very simple as it's a proof of concept for ImGui Test Engine.

  boost::ut vs ImGuiTest
      We're using boost::ut as the unit-test framework. ImGuiTest is
      an implementation detail, required by the test engine.

      Use boost::ut for asserts and reporting results instead of
      ImGuiTest's equivalents.

      Feel free to add several boost::ut tests under the same ImGuiTest.
      While for pure unit-tests the relation is 1:1, for end-to-end tests
      well have 1:N, that is, 1 ImGuiTest doing several UI interactions,
      so N boost:ut::test grouping testable functionallity.
 */

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool pressed = false;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "popup_menu", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(500, 602));

            DigitizerUi::VerticalPopupMenu<1> menu;

            if (!menu.isOpen()) {
                menu.addButton("button", [ctx] {
                    auto& vars   = ctx->GetVars<TestState>();
                    vars.pressed = true;
                });
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("my test") = [ctx] {
                ctx->SetRef("Test Window");
                const ImGuiID popupId = ctx->PopupGetWindowID("MenuPopup_1");
                captureScreenshot(*ctx);

                ctx->SetRef(popupId);
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
