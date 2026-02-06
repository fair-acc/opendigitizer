#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <GrBasicBlocks.hpp>
#include <GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::DockingLayoutType          layoutType = DigitizerUi::DockingLayoutType::Row;
};

TestState* g_state = nullptr;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboardpage", "layouting");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            auto&       vars = ctx->GetVars<TestState>();
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state->dashboard) {
                DigitizerUi::DashboardPage page;
                page.setDashboard(*g_state->dashboard);
                page.setLayoutType(vars.layoutType);
                page.draw();
                ut::expect(!g_state->dashboard->uiWindows.empty()) << ut::fatal;
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::layout"_test = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");

                vars.layoutType = DigitizerUi::DockingLayoutType::Row;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Column;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Grid;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Free;
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboardpage_layout";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);
    auto    restClient = std::make_shared<opencmw::client::RestClient>();

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);

    auto fs      = cmrc::ui_test_assets::get_filesystem();
    auto grcFile = fs.open("examples/qa_layout.grc");

    auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state->dashboard        = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);
    g_state->dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { //
        g_state->dashboard->emplaceGraph(std::move(graph));
    });

    auto result = app.runTests();
    g_state     = nullptr;
    return result ? 0 : 1;
}
