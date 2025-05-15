#include "FlowgraphPage.hpp"
#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include <format>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <GrBasicBlocks.hpp>
#include <GrFourierBlocks.hpp>
#include <GrTestingBlocks.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include "imgui_test_engine/imgui_te_internal.h"

#include <cmrc/cmrc.hpp>

#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::FlowgraphPage              flowgraphPage;

    void startScheduler() { dashboard->scheduler()->start(); }
    void stopScheduler() { dashboard->scheduler()->stop(); }

    bool hasBlocks() const { return dashboard && !dashboard->graphModel().blocks().empty(); }

    void drawGraph() {
        // draw it here since we can't make FlowgraphPage a friend of the GuiFunc lambda
        if (hasBlocks()) {
            flowgraphPage.sortNodes(false);
            DigitizerUi::FlowgraphPage::drawGraph(dashboard->graphModel(), ImGui::GetContentRegionAvail(), flowgraphPage.m_filterBlock);
        }
    }

    void waitForScheduler(std::size_t maxCount = 100UZ, std::source_location location = std::source_location::current()) {
        std::size_t count = 0;
        while (!gr::lifecycle::isActive(dashboard->scheduler()->state()) && count < maxCount) {
            // wait until scheduler is started
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;
        }
        if (count >= maxCount) {
            throw gr::exception(std::format("waitForScheduler({}): maxCount exceeded", count), location);
        }
    }

    void setFilterBlock(const DigitizerUi::UiGraphBlock* block) { flowgraphPage.m_filterBlock = block; }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "FlowgraphPage::drawNodeEditor");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            g_state.drawGraph();

            g_state.dashboard->handleMessages();
            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "FlowgraphPage::drawNodeEditor"_test = [ctx] {
                ctx->SetRef("Test Window");

                g_state.waitForScheduler();
                while (!g_state.hasBlocks()) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                g_state.stopScheduler();
                captureScreenshot(*ctx);

                // Test filtering
                g_state.setFilterBlock(&g_state.dashboard->graphModel().blocks()[0]);
                captureScreenshot(*ctx);
            };
        };
    }
};

namespace {
template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<opendigitizer::Arithmetic, float>(registry);
    gr::registerBlock<opendigitizer::SineSource, float>(registry);
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);

    std::print("Available blocks:\n");
    for (auto& blockName : registry.keys()) {
        std::print("  - {}\n", blockName);
    }
#pragma GCC diagnostic pop
}
} // namespace

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "flowgraph";

    // This is not a globalBlockRegistry, but a copy of it
    gr::BlockRegistry& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    gr::PluginLoader pluginLoader(registry, {});

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto fs            = cmrc::sample_dashboards::get_filesystem();
    auto grcFile       = fs.open("assets/sampleDashboards/DemoDashboard.grc");
    auto dashboardFile = fs.open("assets/sampleDashboards/DemoDashboard.yml");

    auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(dashBoardDescription);
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()), [](gr::Graph&& grGraph) { //
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>;
        g_state.dashboard->emplaceScheduler<TScheduler, gr::Graph>(std::move(grGraph));
    });

    // set the callback so we don't crash
    g_state.flowgraphPage.requestBlockControlsPanel = [](DigitizerUi::components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool) {};

    g_state.flowgraphPage.setDashboard(g_state.dashboard.get());

    std::jthread schedulerThread([] { g_state.startScheduler(); });

    return app.runTests() ? 0 : 1;
}
