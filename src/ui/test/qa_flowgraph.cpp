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

    /// In case the tests create more than 1 scheduler, we'll need to wait for them at the end
    std::vector<std::jthread> schedulerThreads;

    void startScheduler() { dashboard->scheduler()->start(); }
    void stopScheduler() { dashboard->scheduler()->stop(); }

    bool hasBlocks() const { return dashboard && !dashboard->graphModel().blocks().empty(); }

    void deleteBlock(const std::string& blockName) { flowgraphPage.deleteBlock(blockName); }

    std::string nameOfFirstBlock() const {
        const auto& blocks = dashboard->graphModel().blocks();
        if (blocks.empty()) {
            return {};
        }
        return blocks[0]->blockUniqueName;
    }

    void drawGraph() {
        // draw it here since we can't make FlowgraphPage a friend of the GuiFunc lambda
        if (hasBlocks()) {
            flowgraphPage.sortNodes(false);
            DigitizerUi::FlowgraphPage::drawGraph(dashboard->graphModel(), ImGui::GetContentRegionAvail(), flowgraphPage.m_filterBlock);
        }
    }

    void waitForScheduler(std::size_t maxCount = 100UZ, std::source_location location = std::source_location::current()) {
        if (dashboard->scheduler()->state() == gr::lifecycle::State::STOPPED || dashboard->scheduler()->state() == gr::lifecycle::State::IDLE) {
            reload();
        }

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

    // Waits for the graph to have exactly expectedBlockCount blocks
    // for testing topology changing messages
    void waitForGraphModelUpdate(size_t expectedBlockCount, std::size_t maxCount = 20UZ) {
        std::size_t count = 0;
        while (dashboard->graphModel().blocks().size() != expectedBlockCount && count < maxCount) {
            dashboard->handleMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            count++;
        }
    }

    /// Creates a fresh Scheduler and Graph so that tests are more individual and deterministics (i.e. not influenced by previous test runs)
    void reload() {
        auto fs            = cmrc::sample_dashboards::get_filesystem();
        auto grcFile       = fs.open("assets/sampleDashboards/DemoDashboard.grc");
        auto dashboardFile = fs.open("assets/sampleDashboards/DemoDashboard.yml");

        auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
        dashboard                 = DigitizerUi::Dashboard::create(dashBoardDescription);

        dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()), [this](gr::Graph&& grGraph) { //
            using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>;
            dashboard->emplaceScheduler<TScheduler, gr::Graph>(std::move(grGraph));
        });

        flowgraphPage.setDashboard(dashboard.get());

        schedulerThreads.emplace_back([this] { startScheduler(); });
    }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        {
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

                    std::string firstBlockName = g_state.nameOfFirstBlock();
                    expect(that % !firstBlockName.empty()) << "There should be at least one block";

                    // Delete the first block
                    const auto numBlocksBefore = g_state.dashboard->graphModel().blocks().size();

                    g_state.deleteBlock(firstBlockName);
                    ctx->Yield(); // Give time for UI to update

                    // deletion is async, let's wait for kBlockRemoved
                    const auto expectedBlockCount = numBlocksBefore - 1;
                    g_state.waitForGraphModelUpdate(expectedBlockCount);

                    const auto numBlocksAfter = g_state.dashboard->graphModel().blocks().size();

                    expect(that % (numBlocksAfter == numBlocksBefore - 1)) << "Exactly one block should be removed";

                    ctx->Yield(); // Give time for UI to update

                    g_state.stopScheduler();
                    captureScreenshot(*ctx);

                    // Test filtering
                    if (!g_state.dashboard->graphModel().blocks().empty()) {
                        g_state.setFilterBlock(g_state.dashboard->graphModel().blocks()[0].get());
                        captureScreenshot(*ctx);
                    }
                };
            };
        }

        { // Test for the "Remove Block"
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "FlowgraphPage remove block");
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
                "FlowgraphPage remove block"_test = [ctx] {
                    ctx->SetRef("Test Window");

                    g_state.waitForScheduler();
                    while (!g_state.hasBlocks()) {
                        ImGuiTestEngine_Yield(ctx->Engine);
                    }

                    g_state.stopScheduler();
                    captureScreenshot(*ctx);

                    // Test filtering
                    if (!g_state.dashboard->graphModel().blocks().empty()) {
                        g_state.setFilterBlock(g_state.dashboard->graphModel().blocks()[0].get());
                        captureScreenshot(*ctx);
                    }
                };
            };
        }
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

    // set the callback so we don't crash
    g_state.flowgraphPage.requestBlockControlsPanel = [](DigitizerUi::components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool) {};

    g_state.reload();

    return app.runTests() ? 0 : 1;
}
