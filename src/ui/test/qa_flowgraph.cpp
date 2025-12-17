#include "FlowgraphPage.hpp"
#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <format>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <GrBasicBlocks.hpp>
#include <GrFourierBlocks.hpp>
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
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::shared_ptr<DigitizerUi::Dashboard>      dashboard;
    DigitizerUi::FlowgraphPage                   flowgraphPage;

    TestState() : flowgraphPage(restClient) {}

    /// In case the tests create more than 1 scheduler, we'll need to wait for them at the end
    std::vector<std::jthread> schedulerThreads;

    void startScheduler() { dashboard->scheduler()->start(); }
    void stopScheduler() { dashboard->scheduler()->stop(); }

    const auto& blocks() const {
        assert(dashboard);
        auto& rootChildren = dashboard->graphModel().rootBlock.childBlocks;
        assert(rootChildren.size() == 1);
        return rootChildren[0]->childBlocks;
    }

    bool hasBlocks() const { return dashboard && !dashboard->graphModel().rootBlock.childBlocks.empty() && !blocks().empty(); }

    void deleteBlock(const std::string& blockName) { flowgraphPage.currentEditor().requestBlockDeletion(blockName); }

    std::string nameOfFirstBlock() const {
        if (blocks().empty()) {
            return {};
        }

        return blocks()[0]->blockUniqueName;
    }

    void drawGraph() {
        // draw it here since we can't make FlowgraphPage a friend of the GuiFunc lambda
        if (hasBlocks() && flowgraphPage.editorCount() > 0) {
            auto& editor = flowgraphPage.currentEditor();
            editor.sortNodes(false);
            editor.drawGraph(ImGui::GetContentRegionAvail());
        }
    }

    void waitForScheduler(std::size_t maxCount = 10UZ, std::source_location location = std::source_location::current()) {
        if (dashboard->scheduler()->state() == gr::lifecycle::State::STOPPED || dashboard->scheduler()->state() == gr::lifecycle::State::IDLE) {
            reload();
        }

        std::size_t count = 0;
        enum StartingState { SchedulerNotRunning, RequestedSchedulerInspection, SchedulerInspected };
        StartingState state = SchedulerNotRunning;

        std::println("Waiting for scheduler to start...");
        while (count < maxCount && state != SchedulerInspected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;

            switch (state) {
            case SchedulerNotRunning:
                if (gr::lifecycle::isActive(dashboard->scheduler()->state())) {
                    if (flowgraphPage.editorCount() == 0) {
                        std::println("Scheduler started, sending kSchedulerInspect message");
                        state = RequestedSchedulerInspection;
                        gr::Message message;
                        message.cmd      = gr::message::Command::Get;
                        message.endpoint = gr::scheduler::property::kSchedulerInspect;
                        message.data     = gr::property_map{};
                        dashboard->graphModel().sendMessage(std::move(message));
                    } else {
                        std::println("We got a root editor from earlier");
                        state = SchedulerInspected;
                    }
                }
                break;

            case RequestedSchedulerInspection:
                if (!dashboard->graphModel().rootBlock.blockUniqueName.empty()) {
                    std::println("We got a root editor");
                    flowgraphPage.pushEditor("rootBlock node editor", dashboard->graphModel(), std::addressof(dashboard->graphModel().rootBlock));
                    state = SchedulerInspected;
                }
                break;

            default: break;
            }
        }
        if (count >= maxCount) {
            std::exit(1);
            throw gr::exception(std::format("waitForScheduler({}): maxCount exceeded", count), location);
        }
    }

    void setFilterBlock(const DigitizerUi::UiGraphBlock* block) { flowgraphPage.currentEditor().setFilterBlock(block); }

    // Waits for the graph to have exactly expectedBlockCount blocks
    // for testing topology changing messages
    void waitForGraphModelUpdate(size_t expectedBlockCount, std::size_t maxCount = 20UZ) {
        std::size_t count = 0;
        while (blocks().size() != expectedBlockCount && count < maxCount) {
            dashboard->handleMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            count++;
        }
    }

    /// Creates a fresh Scheduler and Graph so that tests are more individual and deterministics (i.e. not influenced by previous test runs)
    void reload() {
        auto fs      = cmrc::sample_dashboards::get_filesystem();
        auto grcFile = fs.open("assets/sampleDashboards/DemoDashboard.grc");

        auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
        dashboard                 = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);

        dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [this](gr::Graph&& grGraph) { //
            using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>;
            dashboard->emplaceScheduler<TScheduler>();
            dashboard->scheduler()->setGraph(std::move(grGraph));
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
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Drawing, deleting and filtering test");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = [](ImGuiTestContext*) {
                IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

                ImGui::SetWindowPos({0, 0});
                ImGui::SetWindowSize(ImVec2(800, 800));

                g_state.drawGraph();

                g_state.dashboard->handleMessages();
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
                    const auto numBlocksBefore = g_state.blocks().size();

                    g_state.deleteBlock(firstBlockName);
                    ctx->Yield(); // Give time for UI to update

                    // deletion is async, let's wait for kBlockRemoved
                    const auto expectedBlockCount = numBlocksBefore - 1;
                    g_state.waitForGraphModelUpdate(expectedBlockCount);

                    const auto numBlocksAfter = g_state.blocks().size();

                    expect(that % (numBlocksAfter == numBlocksBefore - 1)) << "Exactly one block should be removed";

                    ctx->Yield(); // Give time for UI to update

                    g_state.stopScheduler();
                    captureScreenshot(*ctx);

                    // Test filtering
                    if (!g_state.blocks().empty()) {
                        g_state.setFilterBlock(g_state.blocks()[0].get());
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

    gr::PluginLoader pluginLoader(registry, gr::globalSchedulerRegistry(), {});

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
