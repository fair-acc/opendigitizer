#include "App.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"

#include <cmrc/cmrc.hpp>
#include <memory.h>

using namespace gr::od;

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState : test::TestDashboardRunner {
    DockingLayoutType              layoutType = DockingLayoutType::Row;
    std::shared_ptr<DashboardPage> dashboardPage;
};

TestState* gState = nullptr;

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboardpage", "layouting");

        t->GuiFunc = [](ImGuiTestContext*) {
            imw::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (gState->dashboard) {
                gState->dashboard->layoutType = gState->layoutType;
                if (!gState->dashboardPage) {
                    gState->dashboardPage = std::make_shared<DashboardPage>();
                    gState->dashboardPage->setDashboard(*gState->dashboard);
                }
                gState->dashboardPage->draw();
                ut::expect(!gState->dashboard->uiWindows.empty()) << ut::fatal;
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::layout"_test = [ctx] {
                ctx->SetRef("Test Window");

                gState->layoutType = DockingLayoutType::Row;
                test::waitUntilAllSamplesDrawn(ctx, *gState->dashboard);
                captureScreenshot(*ctx);

                gState->layoutType = DockingLayoutType::Column;
                captureScreenshot(*ctx);

                gState->layoutType = DockingLayoutType::Grid;
                captureScreenshot(*ctx);

                gState->layoutType = DockingLayoutType::Free;
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboardpage_layout";

    TestApp app(options);

    // before the dashboard, which sets ImGui styles
    app.initImGui();
    TestState state;
    gState = std::addressof(state);

    auto& registry = gr::globalBlockRegistry();

    registerAppBlocks(registry);

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_layout.grc");

    auto result = app.runTests();
    return result ? 0 : 1;
}
