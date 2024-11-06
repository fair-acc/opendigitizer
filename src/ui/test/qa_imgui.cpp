#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/Dialog.hpp"
#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"
#include "imgui_test_engine/imgui_te_internal.h"
#include <fmt/format.h>

using namespace boost;
using namespace DigitizerUi::components;

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool         okEnabled = true;
            DialogButton pressedButton;
            int          counter = 0;
        };

#if 0
        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "dialog", "popup tests");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = [](ImGuiTestContext*) {
                ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetWindowSize(ImVec2(300, 300));

                ImGuiContext& g     = *GImGui;
                ImGuiID       popup = g.CurrentWindow->GetID("mypopup");

                if (false) {
                    std::cerr << "popup.open2? " << ImGui::IsPopupOpen("mypopup") << "/n";
                    ImGui::OpenPopup("mypopup");
                    std::cerr << "popup.open.3? " << ImGui::IsPopupOpen("mypopup") << "/n";

                    ImGui::BeginPopup("mypopup");
                    std::cerr << "popup.open.4? " << ImGui::IsPopupOpen("mypopup") << "/n";
                    ImGui::Button("mybutton");
                    ImGui::EndPopup();
                }

                ImGui::BeginChild("mychild", {200, 200});
                ImGui::Button("buttonchild\n");
                ImGui::EndChild();

                ImGui::Button("outsidebutton");

                auto w  = ImGui::FindWindowByName("Test Window");
                auto w2 = ImGui::FindWindowByName("##Popup_fcb8055c");

                char name[20];
                ImFormatString(name, IM_ARRAYSIZE(name), "##Popup_%08x", popup);

                // char name2[20];
                // ImFormatString(name2, IM_ARRAYSIZE(name), "##Popup_%08x", g.OpenPopupStack[0].PopupId);

                // std::cerr << "found popup window: " << (w2 != nullptr) << "\n";
                // std::cerr << "popup names: " << name << " - " << name2 << "\n";

                // for (auto w : g.Windows) {
                //     std::cerr << "window: " << w->Name << "\n";
                // }

                const char* child_window_name;
                auto        id = g.CurrentWindow->GetID("mychild");
                ImFormatStringToTempBuffer(&child_window_name, NULL, "%s/%s_%08X", "Test Window", "mychild", id);
                std::cerr << "Deduced child window name to be: " << child_window_name << "; exists=" << ImGui::FindWindowByName(child_window_name) << "\n";

                ImGuiTestApp::printWindows();
                ImGui::End();
            };

            t->TestFunc = [](ImGuiTestContext* ctx) {
                ut::test("popup tests") = [ctx] {
                    auto& vars = ctx->GetVars<TestState>();
                    ctx->SetRef("Test Window");

                    // OK button should be visually enabled
                    captureScreenshot(*ctx);

                    auto w2 = ImGui::FindWindowByName("##Popup_fcb8055c");
                    assert(w2);
                    captureScreenshot(*ctx, w2->ID);

                    // ut::expect(vars.pressedButton == DialogButton::None);

                    // ctx->ItemClick("Cancel");
                    // ut::expect(vars.pressedButton == DialogButton::Cancel);

                    // ctx->ItemClick("Ok");
                    // ut::expect(vars.pressedButton == DialogButton::Ok);
                    // vars.okEnabled = false;

                    // // OK button should be visually disabled
                    // captureScreenshot(*ctx);

                    // // Clicking the disabled item shouldn't have any effect
                    // vars.pressedButton = DialogButton::None;
                    // ctx->ItemClick("Ok");
                    // ut::expect(vars.pressedButton == DialogButton::None);

                    // // Still disabled
                    // captureScreenshot(*ctx);

                    // // Test keyboard Enter/Esc
                    // vars.okEnabled = true;
                    // ctx->KeyPress(ImGuiKey_Enter);
                    // ut::expect(vars.pressedButton == DialogButton::Ok);
                    // ctx->KeyPress(ImGuiKey_Escape);
                    // ut::expect(vars.pressedButton == DialogButton::Cancel);
                };
            };
        }
#endif

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "dialog", "event loop tests");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = [](ImGuiTestContext* ctx) {
                ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetWindowSize(ImVec2(300, 300));
                auto& vars = ctx->GetVars<TestState>();

                ImGuiContext& g = *GImGui;
                ImGui::Button("outsidebutton");

                static int count = 0;
                count++;
                vars.counter++;
                // fmt::println("GuiFunc count={}", count);

                ImGui::End();
            };

            t->TestFunc = [](ImGuiTestContext* ctx) {
                ut::test("my test") = [ctx] {
                    fmt::println("Start tests");
                    auto& vars = ctx->GetVars<TestState>();
                    ctx->SetRef("Test Window");

                    fmt::println("button start");
                    ctx->ItemClick("outsidebutton");
                    fmt::println("button end");

                    // ImGuiTestEngine_Yield(ctx->Engine);
                    // ImGuiTestEngine_Yield(ctx->Engine);
                    // fmt::println("yeld end");

                    // takes 10 frames
                    // captureScreenshot(*ctx);

                    fmt::println("Tests End");
                };
            };
        }
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dialog";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
