#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

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
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

/// This file is a duplicate from qa_chart.cpp (but with fg_dipole_intensity_ramp.grc), as requested in #231, to be hacked on.
/// After the dust settles, we should compare the 2 and move duplicated code into shared helper classes

using namespace boost;
using namespace boost::ut;

// We derive from gr's scheduler just to make stop() public
template<gr::profiling::ProfilerLike TProfiler = gr::profiling::null::Profiler>
class TestScheduler : public gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler> {
public:
    explicit TestScheduler(gr::Graph&& graph) : gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler>(std::move(graph)) {}
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
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "more_plotting");
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
            "more_plotting"_test = [ctx] {
                ctx->SetRef("Test Window");

                // For our test we stop the graph after a certain amount samples.
                // TODO: Once Ivan finishes his new ImPlotSink registry class we can remove these reinterpret_cast.

                auto execution  = g_state.dashboard->localFlowGraph.createExecutionContext();
                auto blockModel = g_state.dashboard->localFlowGraph.findPlotSinkGrBlock("sink 3");
                ut::expect(blockModel);
                auto plotBlockModel = reinterpret_cast<gr::BlockWrapper<opendigitizer::ImPlotSink<float>>*>(blockModel);
                auto implotSink     = reinterpret_cast<opendigitizer::ImPlotSink<float>*>(plotBlockModel->raw());

                const int maxSamples = 1400;
                while (g_state.dashboard && implotSink->data.size() < maxSamples) {
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
    options.screenshotPrefix = "chart_fg_dipole";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto fs            = cmrc::ui_test_assets::get_filesystem();
    auto grcFile       = fs.open("examples/fg_dipole_intensity_ramp.grc");
    auto dashboardFile = fs.open("examples/fg_dipole_intensity_ramp.yml");

    auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(/**fgItem=*/nullptr, dashBoardDescription);
    g_state.dashboard->setPluginLoader(loader);
    g_state.dashboard->load(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()));

    auto execution = g_state.dashboard->localFlowGraph.createExecutionContext();
    g_state.dashboard->localFlowGraph.setPlotSinkGrBlocks(std::move(execution.plotSinkGrBlocks));

    g_state.startScheduler(std::move(execution.graph));

    return app.runTests() ? 0 : 1;
}
