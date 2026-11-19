#include "../ui/common/ImguiWrap.hpp"
#include "ImGuiTestApp.hpp"

#include "components/Docking.hpp"
#include <boost/ut.hpp>
#include <format>

using namespace boost;
using namespace DigitizerUi;

struct TestState {
    DockSpace          dockspace;
    DockSpace::Windows windows;
};

TestState* gState = nullptr;

std::shared_ptr<DockSpace::Window> createWindow(const std::string& name) {
    auto window        = std::make_shared<DockSpace::Window>(name);
    window->renderFunc = [] { ImGui::Button("click me"); };
    return window;
}

void createButtonGroup(DigitizerUi::DockSpace& dockspace) {

    IMW::Group group;

    if (ImGui::Button("row")) {
        dockspace.setLayoutType(DockingLayoutType::Row);
    }

    ImGui::SameLine();
    if (ImGui::Button("col")) {
        dockspace.setLayoutType(DockingLayoutType::Column);
    }

    ImGui::SameLine();
    if (ImGui::Button("grid")) {
        dockspace.setLayoutType(DockingLayoutType::Grid);
    }

    ImGui::SameLine();
    if (ImGui::Button("free")) {
        dockspace.setLayoutType(DockingLayoutType::Free);
    }

    ImGui::SameLine();
    if (ImGui::Button("add window")) {
        static int extraWinId = 1;
        extraWinId++;
        gState->windows.push_back(createWindow(std::format("window-{}", extraWinId)));
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(std::string(gr::meta::enumName(dockspace.layoutType()).value_or("")).c_str());
}

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "docking", "docking layouts");

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(1600, 1000));
            IMW::Window window("Test Window", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | //
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |   //
                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

            createButtonGroup(gState->dockspace);
            gState->dockspace.render(gState->windows, ImGui::GetContentRegionAvail(), false);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("test button states") = [ctx] {
                ctx->SetRef("Test Window");

                ctx->ItemClick("row");
                captureScreenshot(*ctx);

                ctx->ItemClick("col");
                captureScreenshot(*ctx);

                ctx->ItemClick("grid");
                captureScreenshot(*ctx);

                ctx->ItemClick("add window");
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "docking";

    TestApp   app(options);
    TestState state;
    gState = std::addressof(state);
    for (int i = 0; i < 4; ++i) {
        state.windows.push_back(createWindow(std::format("dock{}", i)));
    }
    return app.runTests() ? 0 : 1;
}
