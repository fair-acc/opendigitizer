#include "ImGuiTestApp.hpp"

#include "components/FilterComboBoxes.hpp"
#include <boost/ut.hpp>

using namespace gr::od;

using namespace boost;

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            FilterTag lastItem = {};
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "filtercomboboxes", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 350));

            const std::vector<FilterTag> items = {
                {.title = "one", .isActive = true},
                {.title = "two", .isActive = false},
                {.title = "three", .isActive = true},
                {.title = "four", .isActive = true},
            };

            FilterComboBoxes combobox{.combos = {
                                          {"##combo1", "Combo1", {ImColor(255, 0, 0), ImColor(0, 255, 0)}, items},     //
                                          {"##combo2", "Combo2", {ImColor(255, 0, 255), ImColor(255, 255, 0)}, items}, //
                                      }};

            auto& vars = ctx->GetVars<TestState>();
            if (const FilterTag* result = combobox.draw()) {
                vars.lastItem = *result;
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("my test") = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");

                captureScreenshot(*ctx);

                ctx->ItemClick("##combo1");
                captureScreenshot(*ctx);
                ut::expect(vars.lastItem.title.empty());

                ctx->ItemClick("##combo2");
                captureScreenshot(*ctx);
                ut::expect(vars.lastItem.title.empty());

                // a capture closes the combo box (an ImGui bug): reopened
                ctx->ItemClick("##combo2");

                ctx->ItemClick("//##Combo_00/one");
                ut::expect(ut::eq(vars.lastItem.title, std::string("one")));
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "filtercomboboxes";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
