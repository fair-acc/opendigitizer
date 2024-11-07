#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <App.hpp>
#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <Flowgraph.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "gnuradio-4.0/basic/FunctionGenerator.hpp"
#include "gnuradio-4.0/basic/clock_source.hpp"
#include "imgui_test_engine/imgui_te_internal.h"
#include <gnuradio-4.0/fourier/fft.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

std::shared_ptr<DigitizerUi::Dashboard> g_dashboard;

struct TestState {};

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(500, 500));

            ut::expect(!g_dashboard->plots().empty());
            auto plot = g_dashboard->plots()[0];

            if (ImPlot::BeginPlot("Line Plot")) {
                DigitizerUi::DashboardPage::drawPlot(*g_dashboard, plot);
                ImPlot::EndPlot();
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                const int numFramesToWait = 200;
                while (ImGuiTestEngine_GetFrameCount(ctx->Engine) < numFramesToWait) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart";

    ImPlot::CreateContext();

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    // needs to be initialized early due to BlockDefinitions registry being accessed
    DigitizerUi::App::instance();

    auto loader = std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), std::span<const std::filesystem::path>());

    OpenDashboardPage page;
    page.addSource("example://builtin-samples");
    auto dashBoardDescription = page.get(0);

    g_dashboard = DigitizerUi::Dashboard::create(dashBoardDescription);
    g_dashboard->setPluginLoader(loader);
    g_dashboard->load();

    auto execution = g_dashboard->localFlowGraph.createExecutionContext();
    g_dashboard->localFlowGraph.setPlotSinkGrBlocks(std::move(execution.plotSinkGrBlocks));

    App::instance().assignScheduler(std::move(execution.graph));

    auto result = app.runTests() ? 0 : 1;

    g_dashboard = {};

    return result;
}
