#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <Flowgraph.hpp>
#include <OpenDashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "gnuradio-4.0/basic/FunctionGenerator.hpp"
#include "gnuradio-4.0/basic/clock_source.hpp"
#include "imgui_test_engine/imgui_te_internal.h"
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

// We derive from gr's scheduler just to make stop() public
template<gr::profiling::ProfilerLike TProfiler = gr::profiling::null::Profiler>
class TestScheduler : public gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler> {
public:
    TestScheduler() = default;
    TestScheduler(gr::Graph&& graph) : gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler>(std::move(graph)) {}
    void stop() { gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler>::stop(); }
};

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard>                       dashboard;
    std::function<void()>                                         stopFunction;
    std::thread                                                   schedulerThread;
    std::unique_ptr<TestScheduler<gr::profiling::null::Profiler>> scheduler;

    void startScheduler(gr::Graph&& graph) {
        schedulerThread = std::thread([&] {
            scheduler = std::make_unique<TestScheduler<gr::profiling::null::Profiler>>(std::move(graph));
            scheduler->runAndWait();
        });
    }

    void stopScheduler() {
        dashboard = {};
        scheduler->stop();
        schedulerThread.join();
    }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(500, 500));

            if (g_state.dashboard) {
                ut::expect(!g_state.dashboard->plots().empty());
                auto plot = g_state.dashboard->plots()[0];

                if (ImPlot::BeginPlot("Line Plot")) {
                    DigitizerUi::DashboardPage::drawPlot(*g_state.dashboard, plot);
                    ImPlot::EndPlot();
                }
            }

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                // Maybe capturing after a certain amount of plot samples is better than frame counting
                // we'll see if this is flaky soon enough
                const int numFramesToWait = 200;
                while (ImGuiTestEngine_GetFrameCount(ctx->Engine) < numFramesToWait) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                captureScreenshot(*ctx);

                g_state.stopScheduler();
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    DigitizerUi::OpenDashboardPage page;
    page.addSource("example://builtin-samples");
    auto dashBoardDescription = page.get(0);

    g_state.dashboard = DigitizerUi::Dashboard::create(/**fgItem=*/nullptr, dashBoardDescription);
    g_state.dashboard->setPluginLoader(loader);
    g_state.dashboard->load();

    auto execution = g_state.dashboard->localFlowGraph.createExecutionContext();
    g_state.dashboard->localFlowGraph.setPlotSinkGrBlocks(std::move(execution.plotSinkGrBlocks));

    g_state.startScheduler(std::move(execution.graph));

    return app.runTests() ? 0 : 1;
}
