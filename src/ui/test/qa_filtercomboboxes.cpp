#include "ImGuiTestApp.hpp"

#include "components/FilterComboBoxes.hpp"
#include <boost/ut.hpp>

using namespace boost;

struct ComboItem {
    std::string title;
    bool        isActive = true;
    bool        isValid() const { return !title.empty(); }
};

struct CategoryData {
    std::string            id;
    std::string            label;
    std::array<ImColor, 2> color;
    std::vector<ComboItem> items;
};

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            ComboItem lastItem = {};
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "filtercomboboxes", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            // IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(300, 350));

            const std::vector<ComboItem> items = {
                {"one", true},
                {"two", false},
                {"three", true},
                {"four", true},
            };

            DigitizerUi::FilterComboBoxes<CategoryData> combobox;

            combobox.setData({
                {"##combo1", "Combo1", {ImColor(255, 0, 0), ImColor(0, 255, 0)}, items},     //
                {"##combo2", "Combo2", {ImColor(255, 0, 255), ImColor(255, 255, 0)}, items}, //
            });

            auto& vars   = ctx->GetVars<TestState>();
            auto  result = combobox.draw();
            if (result.has_value()) {
                vars.lastItem = *result.value();
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("my test") = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");

                // two closed combo-boxes
                captureScreenshot(*ctx);

                // an open combo-box
                ctx->ItemClick("##combo1");
                captureScreenshot(*ctx);
                ut::expect(!vars.lastItem.isValid());

                // close 1 and open 2
                ctx->ItemClick("##combo2");
                captureScreenshot(*ctx);
                ut::expect(!vars.lastItem.isValid());

                // BUG: capturing a screenshot will close the combo-box
                // reopen it again
                // if it gets fixed in ImGui, just remove this line
                ctx->ItemClick("##combo2");

                // click an item
                ctx->ItemClick("//##Combo_00/one");
                ut::expect(ut::eq(vars.lastItem.title, std::string("one")));
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "filtercomboboxes";

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
