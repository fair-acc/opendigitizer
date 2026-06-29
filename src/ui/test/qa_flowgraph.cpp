#include "FlowgraphPage.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <imgui_node_editor_internal.h>

#include <boost/ut.hpp>

#include <format>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TestSpectrumGenerator.hpp"

#include "scope_exit.hpp"

#include <cmrc/cmrc.hpp>

#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState : public opendigitizer::test::TestDashboardRunner {
    DigitizerUi::FlowgraphPage flowgraphPage;

    TestState() : flowgraphPage(restClient) {
        flowgraphPage.requestBlockControlsPanel = [](DigitizerUi::components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool) { /* this is called unconditionally, so we have to define it to not crash */ };
    }

    void onDashboardLoaded() override { flowgraphPage.setDashboard(dashboard.get()); }

    ~TestState() override = default;

    void waitForScheduler(                                                   //
        ImGuiTestContext*         ctx,                                       //
        std::chrono::milliseconds timeout  = std::chrono::seconds(3),        //
        std::source_location      location = std::source_location::current() //
        ) override {
        opendigitizer::test::TestDashboardRunner::waitForScheduler(ctx, timeout, location);

        // the default waitForScheduler waits for the scheduler to become active. we also want to wait for inspection to complete
        if (flowgraphPage.editorCount() == 0) {
            std::println("\tScheduler started, sending kSchedulerInspect message");
            gr::Message message;
            message.cmd      = gr::message::Command::Get;
            message.endpoint = gr::scheduler::property::kSchedulerInspect;
            message.data     = {};
            dashboard->graphModel.sendMessage(std::move(message));
        } else {
            std::println("\tGraph does not need inspection / it seems populated already");
        }

        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::high_resolution_clock::now() - start < timeout) {
            dashboard->handleMessages();

            if (!dashboard->graphModel.rootBlock.blockUniqueName.empty()) {
                std::println("\tInspection succeeded, we got a root editor");
                flowgraphPage.pushEditor("rootBlock node editor", dashboard->graphModel, std::addressof(dashboard->graphModel.rootBlock));
                break;
            }
        }
        auto timeTaken = std::chrono::high_resolution_clock::now() - start;
        if (timeTaken > timeout) {
            std::exit(1);
            throw gr::exception(std::format("waitForScheduler({}): timeout exceeded while waiting for inspection", timeTaken), location);
        }
    }

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

    void deleteBlock(const std::string& blockName) { flowgraphPage.currentEditor().requestBlockDeletion(blockName); }

    std::string nameOfFirstBlock() const {
        if (blocks().empty()) {
            return {};
        }

        return blocks()[0]->blockUniqueName;
    }

    void setFilterBlock(const DigitizerUi::UiGraphBlock* block) { flowgraphPage.currentEditor().setFilterBlock(block); }

    void drawGraph() {
        // draw it here since we can't make FlowgraphPage a friend of the GuiFunc lambda
        if (hasBlocks() && flowgraphPage.editorCount() > 0) {
            auto& editor = flowgraphPage.currentEditor();
            editor.sortNodes(false);
            editor.drawGraph(ImGui::GetContentRegionAvail());
        }
    }

    void reloadSubgraph() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }

    void enterSubgraphEditor() {
        auto& graphChildren = blocks();
        for (auto& block : graphChildren) {
            if (!block->isScheduler()) {
                continue;
            }
            expect(!block->childBlocks.empty());
            std::println("Entering subgraph editor for {} (children: {})", block->blockUniqueName, block->childBlocks.size());
            flowgraphPage.pushEditor(block->blockUniqueName, dashboard->graphModel, block.get());
            return;
        }
        assert(false && "No subgraph block found in graph children");
    }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    [[nodiscard]] static bool waitForReplyOnEndpoint(ImGuiTestContext* ctx, std::string_view endpoint) {
        std::optional<gr::Message>   outReply;
        auto                         subscription = g_state.dashboard->graphModel.subscribeToResponses([&outReply, endpoint](const gr::Message& reply) {
            if (reply.endpoint == endpoint) {
                outReply = reply;
            }
        });
        Digitizer::utils::scope_exit unsubscribe  = [subscription] { g_state.dashboard->graphModel.unsubscribeFromResponses(subscription); };

        auto start   = std::chrono::high_resolution_clock::now();
        auto timeout = std::chrono::seconds(10);
        while (!outReply && (std::chrono::high_resolution_clock::now() - start < timeout)) {
            ctx->Yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return outReply.has_value();
    }

    void registerTests() override {
        constexpr auto basicGuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));
            g_state.drawGraph();
            g_state.dashboard->handleMessages();
        };

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Drawing, deleting and filtering test");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                "FlowgraphPage::drawNodeEditor"_test = [ctx] {
                    ctx->SetRef("Test Window");

                    g_state.waitForScheduler(ctx);
                    while (!g_state.hasBlocks()) {
                        ctx->Yield();
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

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Export port by dragging outside subgraph bounds");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                g_state.reloadSubgraph();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                g_state.enterSubgraphEditor();
                expect(g_state.flowgraphPage.editorCount() > 1) << fatal;

                auto& editor = g_state.flowgraphPage.currentEditor();

                DigitizerUi::UiGraphPort* targetPort = nullptr;
                for (auto& block : editor.rootBlock()->childBlocks) {
                    if (!block->_outputPorts.empty()) {
                        targetPort = &block->_outputPorts.front();
                        break;
                    }
                }
                expect(targetPort != nullptr) << fatal;

                ctx->Yield(2); // for some reason ax::NodeEditor pin positions are not resolved until after the frame after first draw

                g_state.flowgraphPage.currentEditor().makeCurrent();
                ctx->Yield();
                ax::NodeEditor::NavigateToContent(0.0f);
                ctx->Yield();

                auto  pinId         = ax::NodeEditor::PinId(targetPort);
                auto* editorContext = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
                auto* pin           = editorContext->FindPin(pinId);
                expect(pin) << fatal;
                ImVec2 pinCanvasPosition = pin->m_Bounds.GetCenter();
                ImVec2 pinScreenPosition = ax::NodeEditor::CanvasToScreen(pinCanvasPosition);

                ctx->MouseTeleportToPos(pinScreenPosition);
                ctx->Yield();
                ctx->MouseDown(ImGuiMouseButton_Left);
                ctx->Yield();
                ctx->MouseLiftDragThreshold(ImGuiMouseButton_Left);
                ctx->Yield();
                // canvas-space bounding box for qa_subgraph.grc is min: (-402 19), max: (796 294)
                ctx->MouseMoveToPos(ax::NodeEditor::CanvasToScreen({400, 400}));
                ctx->Yield();
                ctx->MouseUp(ImGuiMouseButton_Left);

                const bool recievedReplyAboutExport = waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort);
                expect(recievedReplyAboutExport) << "Scheduler never responded about the request to export a port\n";

                expect(targetPort->isExportedTo(editor._exportPortTargetBlock)) << "ui action should have caused port to become exported\n";

                g_state.stopScheduler();
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
    using BoundingBox = DigitizerUi::FlowgraphEditor::BoundingBox;

    "addRectangle()"_test = [] {
        BoundingBox bb{.minX = 50, .minY = 50, .maxX = 50, .maxY = 50};
        bb.addRectangle({10, 20}, {30, 30});
        bb.addRectangle({80, 90}, {40, 40});
        expect(bb.minX == 10.0f);
        expect(bb.minY == 20.0f);
        expect(bb.maxX == 120.0f);
        expect(bb.maxY == 130.0f);
    };

    "contains()"_test = [] {
        BoundingBox bb{.minX = 0, .minY = 0, .maxX = 100, .maxY = 100};
        expect(bb.contains({50, 50}));
        expect(bb.contains({0, 0}));
        expect(bb.contains({100, 100}));
        expect(bb.contains({0, 100}));
        expect(bb.contains({100, 0}));
        expect(!bb.contains({-1, 50}));
        expect(!bb.contains({50, -1}));
        expect(!bb.contains({101, 50}));
        expect(!bb.contains({50, 101}));
    };

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "flowgraph";

    // This is not a globalBlockRegistry, but a copy of it
    gr::BlockRegistry&     registry          = gr::globalBlockRegistry();
    gr::SchedulerRegistry& schedulerRegistry = gr::globalSchedulerRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    // qa_subgraph.grc uses the singlethreaded simple scheduler
    schedulerRegistry.insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>();

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    g_state.reload(cmrc::sample_dashboards::get_filesystem(), "assets/sampleDashboards/DemoDashboard.grc");

    auto result = app.runTests();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
