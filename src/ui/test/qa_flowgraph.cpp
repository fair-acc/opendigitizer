#include "FlowgraphPage.hpp"
#include "ImGuiTestApp.hpp"

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

struct TestState {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::shared_ptr<DigitizerUi::Dashboard>      dashboard;
    DigitizerUi::FlowgraphPage                   flowgraphPage;

    TestState() : flowgraphPage(restClient) {}

    void stopScheduler() { dashboard->scheduler->stop(); }

    const auto& blocks() const {
        assert(dashboard);
        auto& rootChildren = dashboard->graphModel.rootBlock.childBlocks;
        assert(rootChildren.size() == 1);
        return rootChildren[0]->childBlocks;
    }

    bool hasBlocks() const { return dashboard && !dashboard->graphModel.rootBlock.childBlocks.empty() && !blocks().empty(); }

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

    void waitForScheduler(ImGuiTestContext* ctx, std::chrono::milliseconds timeout = std::chrono::seconds(3), std::source_location location = std::source_location::current()) {
        if (dashboard->scheduler->state() == gr::lifecycle::State::STOPPED || dashboard->scheduler->state() == gr::lifecycle::State::IDLE) {
            reload();
        }

        enum StartingState { SchedulerNotRunning, RequestedSchedulerInspection, SchedulerInspected };
        StartingState state = SchedulerNotRunning;

        std::println("Waiting for scheduler to start...");
        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::high_resolution_clock::now() - start < timeout && state != SchedulerInspected) {
            ctx->Yield();
            dashboard->handleMessages();

            switch (state) {
            case SchedulerNotRunning:
                if (gr::lifecycle::isActive(dashboard->scheduler->state())) {
                    if (flowgraphPage.editorCount() == 0) {
                        std::println("Scheduler started, sending kSchedulerInspect message");
                        state = RequestedSchedulerInspection;
                        gr::Message message;
                        message.cmd      = gr::message::Command::Get;
                        message.endpoint = gr::scheduler::property::kSchedulerInspect;
                        message.data     = gr::property_map{};
                        dashboard->graphModel.sendMessage(std::move(message));
                    } else {
                        std::println("We got a root editor from earlier");
                        state = SchedulerInspected;
                    }
                }
                break;

            case RequestedSchedulerInspection:
                if (!dashboard->graphModel.rootBlock.blockUniqueName.empty()) {
                    std::println("We got a root editor");
                    flowgraphPage.pushEditor("rootBlock node editor", dashboard->graphModel, std::addressof(dashboard->graphModel.rootBlock));
                    state = SchedulerInspected;
                }
                break;

            default: break;
            }
        }
        auto timeTaken = std::chrono::high_resolution_clock::now() - start;
        if (timeTaken > timeout) {
            std::exit(1);
            throw gr::exception(std::format("waitForScheduler({}): timeout exceeded", timeTaken), location);
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
    void reload(const cmrc::embedded_filesystem& fs = cmrc::sample_dashboards::get_filesystem(), const char* grc = "assets/sampleDashboards/DemoDashboard.grc", const char* dashboardName = "empty") {
        auto grcFile = fs.open(grc);

        auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty(dashboardName);
        dashboard                 = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);

        dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [this](gr::Graph&& grGraph) { //
            dashboard->emplaceGraph(std::move(grGraph));
        });

        flowgraphPage.setDashboard(dashboard.get());
    }

    void reloadSubgraph() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }

    void enterSubgraphEditor(ImGuiTestContext* ctx, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        auto& graphChildren = blocks();
        for (auto& block : graphChildren) {
            if (!block->isScheduler()) {
                continue;
            }
            // sub-scheduler children are loaded asynchronously after sending a kSchedulerInspect Message
            auto start = std::chrono::high_resolution_clock::now();
            while (block->childBlocks.empty() && std::chrono::high_resolution_clock::now() - start < timeout) {
                dashboard->handleMessages();
                ctx->Yield();
            }
            if (block->childBlocks.empty()) {
                std::println("enterSubgraphEditor: timed out waiting for sub-scheduler children to load");
                return;
            }
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
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "getPortNameAndExportInfo");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                auto& editor      = g_state.flowgraphPage.currentEditor();
                auto* exportBlock = editor._exportPortTargetBlock;

                // tests for just handlePortExported in isolation, because it is
                // relied on for later tests. could be moved to another test for
                // the graphmodel if one existed
                "export adds to exportedOutputPorts"_test = [exportBlock] {
                    gr::property_map data{
                        {"portDirection", "output"},
                        {"uniqueBlockName", "testBlock"},
                        {"portName", "out"},
                        {"exportedName", "myOut"},
                        {"exportFlag", true},
                    };
                    exportBlock->handlePortExported(data);

                    expect(exportBlock->exportedOutputPorts.contains("testBlock"));
                    auto& portSet = exportBlock->exportedOutputPorts["testBlock"];
                    auto  it      = std::ranges::find_if(portSet, [](const auto& pm) { return pm.internalName == "out"; });
                    expect(it != portSet.end());
                    expect(it->exportedName == std::string("myOut"));

                    data["exportFlag"] = false;
                    exportBlock->handlePortExported(data);
                    expect(exportBlock->exportedOutputPorts["testBlock"].empty()) << "exportFlag: false should un-export";
                };
                "export adds to exportedInputPorts"_test = [exportBlock] {
                    gr::property_map data{
                        {"portDirection", "input"},
                        {"uniqueBlockName", "blockA"},
                        {"portName", "in0"},
                        {"exportedName", "exposedIn"},
                        {"exportFlag", true},
                    };
                    exportBlock->handlePortExported(data);

                    expect(exportBlock->exportedInputPorts.contains("blockA"));
                    expect(!exportBlock->exportedInputPorts["blockA"].empty());

                    data["exportFlag"] = false;
                    exportBlock->handlePortExported(data);
                    expect(exportBlock->exportedInputPorts["blockA"].empty());
                };

                "returns not-exported for port with no exports"_test = [&editor] {
                    DigitizerUi::UiGraphPort* testPort = nullptr;
                    for (auto& block : editor.rootBlock()->childBlocks) {
                        if (!block->_outputPorts.empty()) {
                            testPort = &block->_outputPorts.front();
                            break;
                        }
                    }
                    expect(testPort != nullptr) << fatal;
                    auto info = editor.getPortNameAndExportInfo(testPort);
                    expect(!info.isExported);
                    expect(info.internalName.empty()) << "shouldn't be exported";
                    expect(info.exportedName.empty());
                };

                "returns exported info after handlePortExported"_test = [&editor, exportBlock] {
                    DigitizerUi::UiGraphPort* testPort = nullptr;
                    std::string               targetBlockName;
                    for (auto& block : editor.rootBlock()->childBlocks) {
                        if (!block->_outputPorts.empty()) {
                            testPort        = &block->_outputPorts.front();
                            targetBlockName = block->blockUniqueName;
                            break;
                        }
                    }
                    expect(testPort != nullptr) << fatal;

                    gr::property_map exportData{
                        {"portDirection", "output"},
                        {"uniqueBlockName", targetBlockName},
                        {"portName", testPort->portName},
                        {"exportedName", "myExportedPort"},
                        {"exportFlag", true},
                    };
                    exportBlock->handlePortExported(exportData);

                    auto info = editor.getPortNameAndExportInfo(testPort);
                    expect(info.isExported) << "port should be exported";
                    expect(info.internalName == testPort->portName);
                    expect(info.exportedName == std::string("myExportedPort"));

                    exportData["exportFlag"] = false;
                    exportBlock->handlePortExported(exportData);

                    auto info2 = editor.getPortNameAndExportInfo(testPort);
                    expect(!info2.isExported) << "port should no longer be exported";
                };

                "returns not-exported for null ownerBlock"_test = [&editor] {
                    DigitizerUi::UiGraphPort orphanPort(nullptr);
                    orphanPort.portName      = "orphan";
                    orphanPort.portDirection = gr::PortDirection::OUTPUT;

                    auto info = editor.getPortNameAndExportInfo(&orphanPort);
                    expect(!info.isExported);
                };

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Export port by dragging outside subgraph bounds");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reloadSubgraph();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                g_state.enterSubgraphEditor(ctx);
                expect(g_state.flowgraphPage.editorCount() > 1) << fatal;

                auto& editor = g_state.flowgraphPage.currentEditor();

                DigitizerUi::UiGraphPort*  targetPort  = nullptr;
                DigitizerUi::UiGraphBlock* targetBlock = nullptr;
                for (auto& block : editor.rootBlock()->childBlocks) {
                    if (!block->_outputPorts.empty()) {
                        targetPort  = &block->_outputPorts.front();
                        targetBlock = block.get();
                        break;
                    }
                }
                expect(targetPort != nullptr) << fatal;

                std::optional<gr::Message> capturedMessage;
                auto                       originalSendMessage = editor.graphModel()->sendMessage_;
                editor.graphModel()->sendMessage_              = [&capturedMessage, &originalSendMessage](gr::Message msg, std::source_location loc) {
                    if (msg.endpoint == gr::graph::property::kSubgraphExportPort) {
                        capturedMessage = msg;
                    }
                    originalSendMessage(std::move(msg), loc);
                };
                Digitizer::utils::scope_exit restoreSendMessage([&] { editor.graphModel()->sendMessage_ = originalSendMessage; });

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
                ctx->Yield();

                expect(capturedMessage.has_value()) << "export message should have been sent on release outside bounding box";
                if (capturedMessage.has_value()) {
                    expect(capturedMessage->endpoint == std::string(gr::graph::property::kSubgraphExportPort));
                    auto& data = *capturedMessage->data;
                    expect(data.at("uniqueBlockName").value_or(std::string{}) == targetBlock->blockUniqueName);
                    expect(data.at("portDirection").value_or(std::string{}) == std::string("output"));
                    expect(data.at("portName").value_or(std::string{}) == targetPort->portName);
                    expect(data.at("exportFlag").value_or(false));
                }

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
    gr::BlockRegistry& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    gr::PluginLoader pluginLoader(registry, gr::globalSchedulerRegistry(), {});

    // qa_subgraph.grc uses the simple scheduler
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>();
    gr::globalSchedulerRegistry().insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>>();

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    // set the callback so we don't crash
    g_state.flowgraphPage.requestBlockControlsPanel = [](DigitizerUi::components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool) {};

    g_state.reload();

    auto result = app.runTests();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
