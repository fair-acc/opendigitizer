#include "FlowgraphPage.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <imgui_node_editor_internal.h>

#include <boost/ut.hpp>

#include <algorithm>
#include <format>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TestSpectrumGenerator.hpp" // although the symbol is unused by this file, we need this for static block registration

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
    void onDashboardAboutToBeUnloaded() override { flowgraphPage.setDashboard(nullptr); }

    ~TestState() override { TestState::onDashboardAboutToBeUnloaded(); }

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
            editor.drawGraph(ImGui::GetContentRegionAvail());
        }
    }

    void reloadSubgraph() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }

    void reloadGrouping() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_grouping.grc", "grouping_test"); }

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

    UiGraphBlock& currentRootBlock() {
        if (auto* ptr = flowgraphPage.currentEditor().rootBlock()) {
            return *ptr;
        }
        expect(false) << "flowgraph page should have an editor and some contents";
        std::unreachable();
    }
};

constexpr const char* simpleGraph = "connections: []\n"
                                    "blocks:\n"
                                    "  - parameters:\n"
                                    "      name: \"connectSineSource\"\n"
                                    "    id: \"opendigitizer::SineSource<float32>\"\n"
                                    "  - parameters:\n"
                                    "      name: \"connectDataSink\"\n"
                                    "    id: \"gr::basic::DataSink<float32>\"";

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    [[nodiscard]] static bool waitForRepliesOnEndpoint(ImGuiTestContext* ctx, std::string_view endpoint, std::size_t count = 1UZ) {
        const bool replied = waitFor(ctx, [endpoint, count] { //
            return static_cast<std::size_t>(std::ranges::count_if(g_state.collectedMessages, [endpoint](const gr::Message& message) { return message.endpoint == endpoint; })) >= count;
        });
        g_state.clearMessages();
        return replied;
    }

    [[nodiscard]] static bool waitFor(ImGuiTestContext* ctx, const std::function<bool()>& predicate, std::chrono::seconds timeout = std::chrono::seconds(10)) {
        auto start = std::chrono::high_resolution_clock::now();
        while (!predicate() && (std::chrono::high_resolution_clock::now() - start < timeout)) {
            ctx->Yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return predicate();
    }

    static void dragPinToPin(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphPort* fromPort, const DigitizerUi::UiGraphPort* toPort) {
        ctx->Yield(2); // for some reason ax::NodeEditor pin positions are not resolved until after the frame after first draw

        editor.makeCurrent();
        ctx->Yield();
        ax::NodeEditor::NavigateToContent(0.0f);
        ctx->Yield();

        auto* editorContext = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        auto* fromPin       = editorContext->FindPin(ax::NodeEditor::PinId(fromPort));
        auto* toPin         = editorContext->FindPin(ax::NodeEditor::PinId(toPort));
        expect(fromPin != nullptr) << fatal;
        expect(toPin != nullptr) << fatal;

        ctx->MouseTeleportToPos(ax::NodeEditor::CanvasToScreen(fromPin->m_Bounds.GetCenter()));
        ctx->Yield();
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield();
        ctx->MouseLiftDragThreshold(ImGuiMouseButton_Left);
        ctx->Yield();
        ctx->MouseMoveToPos(ax::NodeEditor::CanvasToScreen(toPin->m_Bounds.GetCenter()));
        ctx->Yield();
        ctx->MouseUp(ImGuiMouseButton_Left);
    }

    [[nodiscard]] static bool edgeExistsIn(const DigitizerUi::UiGraphBlock& graph, std::string_view sourceBlockName, std::string_view destinationPortName) {
        return std::ranges::any_of(graph.childEdges, [&](const DigitizerUi::UiGraphEdge& edge) {
            return edge.edgeSourcePort && edge.edgeSourcePort->ownerBlock && edge.edgeSourcePort->ownerBlock->blockName == sourceBlockName && //
                   edge.edgeDestinationPort && edge.edgeDestinationPort->portName == destinationPortName;
        });
    }

    [[nodiscard]] static DigitizerUi::UiGraphPort* findFirstPortOfBlock(DigitizerUi::UiGraphBlock& graph, std::string_view blockName, gr::PortDirection direction) {
        for (auto& block : graph.childBlocks) {
            if (block->blockName != blockName) {
                continue;
            }
            auto& ports = direction == gr::PortDirection::INPUT ? block->_inputPorts : block->_outputPorts;
            return ports.empty() ? nullptr : std::addressof(ports.front());
        }
        return nullptr;
    }

    // do Yield(2) before calling this if the node was just created
    static ImVec2 nodeCentreOnScreen(DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* block) {
        editor.makeCurrent();
        const auto   nodeId   = ax::NodeEditor::NodeId(block);
        const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
        const ImVec2 size     = ax::NodeEditor::GetNodeSize(nodeId);
        expect(position.x < FLT_MAX && position.y < FLT_MAX) << fatal << "the node editor does not know about this block yet";
        expect(size.x > 0.f && size.y > 0.f) << fatal << "the node has not been laid out yet, probably do Yield(2) in the test";
        return ax::NodeEditor::CanvasToScreen(position + size * 0.5f);
    }

    static void clickNode(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* block, ImGuiMouseButton button) {
        ctx->MouseMoveToPos(nodeCentreOnScreen(editor, block));
        ctx->Yield();
        ctx->MouseClick(button);
        ctx->Yield();
    }

    /// Moves the mouse onto the node for real, but makes the selection through the node editor's API.
    /// TODO: figure out why imgui MouseClick and other testing mouse actions don't seem to work for
    /// selecting nodes
    static void selectNode(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* block, bool addToSelection = false) {
        ctx->MouseMoveToPos(nodeCentreOnScreen(editor, block));
        ctx->Yield();
        editor.makeCurrent();
        ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(block), addToSelection);
        ctx->Yield();
    }

    [[nodiscard]] static ImGuiID frontmostPopupId() {
        const ImGuiContext& g = *GImGui;
        return g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window ? g.OpenPopupStack.back().Window->ID : 0;
    }

    static void clickInBlockContextMenu(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* block, const char* menuItem) {
        clickNode(ctx, editor, block, ImGuiMouseButton_Right);
        ctx->Yield();
        const ImGuiID contextMenuId = frontmostPopupId();
        expect(contextMenuId != 0u) << fatal << "right-clicking a block should open its context menu";
        ctx->SetRef(contextMenuId);
        ctx->ItemClick(menuItem);
        ctx->Yield();
        ctx->SetRef("Test Window");
    }

    static void chooseInBlockSelector(ImGuiTestContext* ctx, const char* typeName, const char* parametrization) {
        ctx->SetRef("//New Block");
        ctx->ItemClick("**/##filterTypenameType");
        ctx->KeyCharsReplace(typeName);
        ctx->Yield();
        ctx->ItemClick(std::format("**/##{}", typeName).c_str());
        ctx->Yield();
        ctx->ItemClick(std::format("**/{}", parametrization).c_str());
        ctx->Yield();
        ctx->ItemClick("Ok");
        ctx->Yield();
        ctx->SetRef("Test Window");
    }

    static DigitizerUi::UiGraphBlock* findRootChildByName(std::string_view blockName) {
        auto& children = g_state.currentRootBlock().childBlocks;
        auto  it       = std::ranges::find(children, blockName, [](const auto& child) { return std::string_view(child->blockName); });
        return it == children.end() ? nullptr : it->get();
    }

    static DigitizerUi::UiGraphBlock* findRootChildByUniqueName(std::string_view uniqueName) {
        auto& children = g_state.currentRootBlock().childBlocks;
        auto  it       = std::ranges::find(children, uniqueName, [](const auto& child) { return std::string_view(child->blockUniqueName); });
        return it == children.end() ? nullptr : it->get();
    }

    static DigitizerUi::UiGraphBlock* findSubgraphInCurrentRoot() {
        auto& children = g_state.currentRootBlock().childBlocks;
        auto  it       = std::ranges::find_if(children, [](const auto& child) { return child->isGraph() || child->isScheduler(); });
        return it == children.end() ? nullptr : it->get();
    }

    static void requestEmplaceBlock(DigitizerUi::FlowgraphEditor& editor, std::string blockType) {
        auto owner = editor.ownersForRoot();
        expect(owner.has_value()) << fatal;

        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kEmplaceBlock;
        message.serviceName = owner->scheduler;
        message.data        = gr::property_map{{"type", std::move(blockType)}, {"_targetGraph", owner->graph}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    void registerTests() override { // NOSONAR (cognitive complexity)
        constexpr auto basicGuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));
            g_state.drawGraph();
            g_state.dashboard->handleMessages();
        };

        constexpr auto pageGuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(1024, 800));
            if (g_state.dashboard) {
                g_state.flowgraphPage.draw();
                g_state.dashboard->handleMessages();
            }
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

                const auto findTargetPort = []() -> DigitizerUi::UiGraphPort* {
                    for (auto& block : g_state.currentRootBlock().childBlocks) {
                        if (!block->_outputPorts.empty()) {
                            return &block->_outputPorts.front();
                        }
                    }
                    return nullptr;
                };
                DigitizerUi::UiGraphPort* targetPort = findTargetPort();
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

                const bool recievedReplyAboutExport = waitForRepliesOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort);
                expect(recievedReplyAboutExport) << "Scheduler never responded about the request to export a port\n";

                // there are two messages, one to confirm the export happened
                // (already done, as per recievedReplyAboutExport), and then one
                // to send a full block update, which we have to wait for
                expect(waitFor(ctx, [&] {
                    auto* port = findTargetPort();
                    return port && port->isExportedTo(editor.exportPortTargetBlock());
                })) << "ui action should have caused port to become exported\n";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Connect root graph block to exported input port");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                g_state.reloadFromYamlString(simpleGraph);
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                // create the scheduler subgraph, as if through the "Add sub graph..." dialog (bug does not happen if the scheduler is loaded at the same time as its contents)
                requestEmplaceBlock(g_state.flowgraphPage.currentEditor(), "gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>");
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlockEmplaced)) << "Scheduler never responded about the request to create the subgraph\n" << fatal;

                const auto findSubgraphBlock = []() -> DigitizerUi::UiGraphBlock* {
                    for (auto& block : g_state.currentRootBlock().childBlocks) {
                        if (block->isScheduler()) {
                            return block.get();
                        }
                    }
                    return nullptr;
                };
                expect(waitFor(ctx,
                    [&] {
                        auto* subgraphBlock = findSubgraphBlock();
                        return subgraphBlock && !subgraphBlock->childBlocks.empty();
                    }))
                    << "subgraph and its child graph should appear in the model\n"
                    << fatal;

                g_state.enterSubgraphEditor();
                expect(g_state.flowgraphPage.editorCount() > 1) << fatal;

                // place a DataSink inside the subgraph, as if through the "Add block..." dialog
                requestEmplaceBlock(g_state.flowgraphPage.currentEditor(), "gr::basic::DataSink<float32>");
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlockEmplaced)) << "Scheduler never responded about the request to create the DataSink\n" << fatal;

                const auto findSinkPort = []() -> DigitizerUi::UiGraphPort* {
                    for (auto& block : g_state.currentRootBlock().childBlocks) {
                        if (block->blockTypeName.starts_with("gr::basic::DataSink") && !block->_inputPorts.empty()) {
                            return std::addressof(block->_inputPorts.front());
                        }
                    }
                    return nullptr;
                };
                expect(waitFor(ctx, [&] { return findSinkPort() != nullptr; })) << "the DataSink should appear in the subgraph\n" << fatal;

                {
                    auto& subgraphEditor = g_state.flowgraphPage.currentEditor();
                    auto* sinkPort       = findSinkPort();
                    expect(sinkPort) << fatal;

                    subgraphEditor.requestExportPort({
                        .uniqueBlockName = sinkPort ? sinkPort->ownerBlock->blockUniqueName : "", // -Werror=null-dereference
                        .portDirection   = "input",
                        .portName        = sinkPort ? sinkPort->portName : "", // -Werror=null-dereference
                        .exportedName    = "exported_in",
                        .exportFlag      = true,
                    });
                    expect(waitForRepliesOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "Scheduler never responded about the request to export a port\n" << fatal;
                    // re-find the port each time, the model may have been rebuilt in the meantime
                    expect(waitFor(ctx,
                        [&] {
                            auto* port = findSinkPort();
                            return port && port->isExportedTo(g_state.flowgraphPage.currentEditor().exportPortTargetBlock());
                        }))
                        << "port should be exported\n"
                        << fatal;
                }

                g_state.flowgraphPage.popEditor();
                expect(g_state.flowgraphPage.editorCount() == 1_ul) << fatal;

                // wait for the full model update triggered by popEditor() so the
                // subgraph block in the root graph gains the exported input port
                const auto findExportedPort = [&]() -> DigitizerUi::UiGraphPort* {
                    auto* subgraphBlock = findSubgraphBlock();
                    if (!subgraphBlock) {
                        return nullptr;
                    }
                    auto portIterator = std::ranges::find_if(subgraphBlock->_inputPorts, [](const UiGraphPort& port) { return port.portName == "exported_in"; });
                    return portIterator != subgraphBlock->_inputPorts.end() ? std::addressof(*portIterator) : nullptr;
                };
                expect(waitFor(ctx, [&] { return findExportedPort() != nullptr; })) << "exported input port should appear on the subgraph block in the root graph\n" << fatal;

                auto* sourcePort = findFirstPortOfBlock(g_state.currentRootBlock(), "connectSineSource", gr::PortDirection::OUTPUT);
                expect(sourcePort != nullptr) << fatal;

                // a normal forwards drag, from the source's output pin to the exported input pin
                dragPinToPin(ctx, g_state.flowgraphPage.currentEditor(), sourcePort, findExportedPort());

                expect(waitFor(ctx, [] { return edgeExistsIn(g_state.currentRootBlock(), "connectSineSource", "exported_in"); })) << "edge from root graph block to exported input port should appear in the UI\n";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Connect two pins dragging backwards from input to output");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reloadFromYamlString(simpleGraph);
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                auto* sourcePort      = findFirstPortOfBlock(g_state.currentRootBlock(), "connectSineSource", gr::PortDirection::OUTPUT);
                auto* destinationPort = findFirstPortOfBlock(g_state.currentRootBlock(), "connectDataSink", gr::PortDirection::INPUT);
                expect(sourcePort != nullptr) << fatal;
                expect(destinationPort != nullptr) << fatal;

                // drag starting from the input pin towards the output pin
                dragPinToPin(ctx, g_state.flowgraphPage.currentEditor(), destinationPort, sourcePort);

                expect(waitFor(ctx, [] { return edgeExistsIn(g_state.currentRootBlock(), "connectSineSource", "in"); })) << "edge should appear in the UI when connecting backwards\n";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Export all unused ports");
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

                const auto isConnected = [](const UiGraphPort& port) {
                    return std::ranges::any_of(g_state.currentRootBlock().childEdges, [&port](const auto& edge) { //
                        return edge.edgeSourcePort == &port || edge.edgeDestinationPort == &port;
                    });
                };

                std::size_t totalUnconnectedPorts = 0;
                for (auto& block : g_state.currentRootBlock().childBlocks) {
                    for (const auto& port : block->_inputPorts) {
                        totalUnconnectedPorts += isConnected(port) ? 0 : 1;
                    }
                    for (const auto& port : block->_outputPorts) {
                        totalUnconnectedPorts += isConnected(port) ? 0 : 1;
                    }
                }
                expect(totalUnconnectedPorts > 0_ul) << "subgraph should have unconnected ports";

                const auto expectAndUnexportAllWithFilter = [ctx, &editor, &isConnected](std::span<UiGraphPort> ports, UiGraphPort* filter = nullptr, std::source_location location = std::source_location::current()) {
                    for (const auto& port : ports) {
                        if (std::addressof(port) == filter || isConnected(port)) {
                            continue;
                        }
                        expect(waitFor(ctx, [&] { return port.isExportedTo(editor.exportPortTargetBlock()); })) << "all ports should be exported, this was not: " << port.portName << " of " << port.ownerBlock->blockName << std::format(" - line {}\n", location.line());
                        editor.requestExportPort({
                            .uniqueBlockName = port.ownerBlock->blockUniqueName,
                            .portDirection   = port.portDirection == gr::PortDirection::INPUT ? "input" : "output",
                            .portName        = port.portName,
                            .exportFlag      = false,
                        });
                        expect(waitForRepliesOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "Scheduler never responded about the request to un-export a port\n" << fatal;
                        expect(waitFor(ctx, [&] { return !port.isExportedTo(editor.exportPortTargetBlock()); })) << "failed to un-export" << port.portName << "of" << port.ownerBlock->blockName << std::format("- line {}\n", location.line()) << fatal;
                    }
                };

                "export all unconnected ports"_test = [ctx, &editor, &expectAndUnexportAllWithFilter, totalUnconnectedPorts] {
                    editor.exportAllUnusedPorts();
                    std::println("waitForRepliesOnEndpoint() about to be called for all port messages, {} in total...", totalUnconnectedPorts);
                    expect(waitForRepliesOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort, totalUnconnectedPorts)) << "Scheduler never responded about the request to export all ports\n" << fatal;

                    for (const auto& block : g_state.currentRootBlock().childBlocks) {
                        expectAndUnexportAllWithFilter(block->_outputPorts);
                        expectAndUnexportAllWithFilter(block->_inputPorts);
                    }
                };

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Selection does not report blocks deleted by grouping");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = basicGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
                g_state.reloadGrouping();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                auto& graphModel = g_state.dashboard->graphModel;
                auto& editor     = g_state.flowgraphPage.currentEditor();

                DigitizerUi::UiGraphBlock* loner1 = graphModel.recursiveFindBlockByName("loner1").block;
                DigitizerUi::UiGraphBlock* loner2 = graphModel.recursiveFindBlockByName("loner2").block;
                expect(loner1 != nullptr && loner2 != nullptr) << fatal;
                const std::vector<std::string> groupedNames{loner1->blockUniqueName, loner2->blockUniqueName};

                ctx->Yield(2); // the node editor needs a frame before nodes become selectable

                editor.makeCurrent();
                ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(loner1), true);
                ax::NodeEditor::SelectNode(ax::NodeEditor::NodeId(loner2), true);
                expect(eq(editor.selectedBlockUniqueNames().size(), 2UZ)) << fatal << "both blocks should report as selected before grouping";

                editor.requestBlocksGrouping("gr::Graph", groupedNames);
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlocksGrouped)) << "scheduler should confirm grouping";

                const auto groupedBlocksLeftRootGraph = [&] {
                    return std::ranges::none_of(g_state.currentRootBlock().childBlocks, [&](const auto& child) { //
                        return std::ranges::contains(groupedNames, child->blockUniqueName);
                    });
                };
                const auto start = std::chrono::high_resolution_clock::now();
                while (!groupedBlocksLeftRootGraph() && std::chrono::high_resolution_clock::now() - start < std::chrono::seconds(10)) {
                    ctx->Yield();
                }
                expect(groupedBlocksLeftRootGraph()) << fatal << "grouped blocks should have moved into the subgraph";

                ctx->Yield(); // draw a frame so the editor can observe the model change

                const std::vector<std::string> selectedAfter = editor.selectedBlockUniqueNames();
                for (const std::string& name : selectedAfter) {
                    expect(graphModel.recursiveFindBlockByUniqueName(name).block != nullptr) << "selection reported a block that does not exist: " << name;
                }
                expect(selectedAfter.empty()) << "blocks deleted by grouping should not be reported as selected";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Place a block using the new block selector");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = pageGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reloadFromYamlString(simpleGraph);
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                auto& graphModel = g_state.dashboard->graphModel;
                graphModel.requestAvailableBlocksTypesUpdate();
                expect(waitFor(ctx, [&graphModel] { return graphModel.knownBlockTypes.contains("opendigitizer::Arithmetic"); })) << fatal << "the block registry should have reached the UI before the selector is opened\n";

                const auto arithmeticBlockCount = [] { return std::ranges::count_if(g_state.currentRootBlock().childBlocks, [](const auto& child) { return child->blockTypeName == "opendigitizer::Arithmetic<float64>"; }); };
                expect(arithmeticBlockCount() == 0) << fatal << "initial number of blocks should be 0, verifying that it increases to 1";

                ctx->SetRef("Test Window");
                ctx->ItemClick("//Button Overlay/Add block...");
                ctx->Yield();

                chooseInBlockSelector(ctx, "opendigitizer::Arithmetic", "<float64>");

                expect(waitFor(ctx, [&] { return arithmeticBlockCount() == 1; })) << "the type chosen in the dialog should be emplaced into the graph\n";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Deleting a block clears the node selection");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = pageGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reloadGrouping();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                ctx->SetRef("Test Window");
                auto& editor = g_state.flowgraphPage.currentEditor();
                ctx->Yield(2);
                editor.makeCurrent();
                ax::NodeEditor::NavigateToContent(0.0f);
                ctx->Yield(2);

                DigitizerUi::UiGraphBlock* middleA = findRootChildByName("middleA");
                DigitizerUi::UiGraphBlock* middleB = findRootChildByName("middleB");
                DigitizerUi::UiGraphBlock* loner1  = findRootChildByName("loner1");
                expect(middleA != nullptr && middleB != nullptr && loner1 != nullptr) << fatal;
                const std::string loner1UniqueName = loner1->blockUniqueName;

                selectNode(ctx, editor, middleA);
                selectNode(ctx, editor, middleB, /*addToSelection*/ true);
                expect(eq(editor.selectedBlockUniqueNames().size(), 2UZ)) << fatal << "both blocks should be selected before the deletion\n";

                // test that deleting a block clears the selection
                editor.requestBlockDeletion(loner1UniqueName);
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlockRemoved)) << fatal << "scheduler did not confirm the removal\n";
                expect(waitFor(ctx, [&] { return findRootChildByUniqueName(loner1UniqueName) == nullptr; })) << fatal << "the deleted block should disappear from the graph model\n";
                ctx->Yield(); // let the editor draw once, which is where stale node ids are dropped

                expect(findRootChildByName("middleA") != nullptr && findRootChildByName("middleB") != nullptr) << "the selected blocks themselves should still be there\n";
                expect(editor.selectedBlockUniqueNames().empty()) << "deleting a block should clear the node editor selection\n";

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Group and ungroup blocks from the context menu");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = pageGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                g_state.reloadGrouping();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                ctx->SetRef("Test Window");
                auto& editor = g_state.flowgraphPage.currentEditor();
                ctx->Yield(2);
                editor.makeCurrent();
                ax::NodeEditor::NavigateToContent(0.0f);
                ctx->Yield(2);

                DigitizerUi::UiGraphBlock* middleA = findRootChildByName("middleA");
                DigitizerUi::UiGraphBlock* middleB = findRootChildByName("middleB");
                expect(middleA != nullptr && middleB != nullptr) << fatal;
                const std::vector<std::string> groupedNames{middleA->blockUniqueName, middleB->blockUniqueName};

                selectNode(ctx, editor, middleA);
                selectNode(ctx, editor, middleB, /*addToSelection*/ true);
                expect(eq(editor.selectedBlockUniqueNames().size(), 2UZ)) << fatal << "clicking and ctrl-clicking should select both blocks\n";

                clickInBlockContextMenu(ctx, editor, middleB, "Group blocks");
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlocksGrouped)) << fatal << "the menu item should have asked the scheduler to group the selection\n";

                expect(waitFor(ctx,
                    [&] {
                        DigitizerUi::UiGraphBlock* subgraph = findSubgraphInCurrentRoot();
                        DigitizerUi::UiGraphBlock* interior = subgraph && subgraph->isScheduler() ? (subgraph->childBlocks.empty() ? nullptr : subgraph->childBlocks.front().get()) : subgraph;
                        return interior && std::ranges::all_of(groupedNames, [interior](const std::string& name) { //
                            return interior->findBlockByUniqueName(name) != nullptr && findRootChildByUniqueName(name) == nullptr;
                        });
                    }))
                    << fatal << "both selected blocks should have moved into a new subgraph\n";

                ctx->Yield(3); // let the editor lay out the node for the new subgraph

                DigitizerUi::UiGraphBlock* subgraph = findSubgraphInCurrentRoot();
                expect(subgraph != nullptr) << fatal;
                clickInBlockContextMenu(ctx, editor, subgraph, "Ungroup blocks");
                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlocksUngrouped)) << fatal << "the menu item should have asked the scheduler to ungroup\n";

                expect(waitFor(ctx, [&] {
                    return findSubgraphInCurrentRoot() == nullptr && //
                           std::ranges::all_of(groupedNames, [](const std::string& name) { return findRootChildByUniqueName(name) != nullptr; });
                })) << "ungrouping through the context menu should bring both blocks back to the root graph\n";

                g_state.stopScheduler();
            };
        }

        for (bool pickGraphTypeInDialog : {false, true}) {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Group blocks inside a subgraph editor");
            t->SetOwnedName(pickGraphTypeInDialog ? "Group blocks inside a subgraph editor, managed subgraph picked in the dialog" : "Group blocks inside a subgraph editor, unmanaged subgraph");
            t->ArgVariant = pickGraphTypeInDialog ? 1 : 0;
            t->SetVarsDataType<TestState>();

            t->GuiFunc = pageGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                const bool pickInDialog = ctx->Test->ArgVariant == 1;

                g_state.reloadSubgraph();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                g_state.enterSubgraphEditor();
                expect(g_state.flowgraphPage.editorCount() > 1) << fatal;

                auto& graphModel = g_state.dashboard->graphModel;
                graphModel.requestAvailableBlocksTypesUpdate();
                expect(waitFor(ctx, [&graphModel] { return graphModel.knownSchedulerTypes.contains("gr::scheduler::Simple"); })) << fatal << "the scheduler registry should have reached the UI\n";

                ctx->SetRef("Test Window");
                auto& editor = g_state.flowgraphPage.currentEditor();
                ctx->Yield(2);
                editor.makeCurrent();
                ax::NodeEditor::NavigateToContent(0.0f);
                ctx->Yield(2);

                auto& innerBlocks = g_state.currentRootBlock().childBlocks;
                expect(innerBlocks.size() >= 2UZ) << fatal << "qa_subgraph.grc should have at least two blocks inside its subgraph\n";
                DigitizerUi::UiGraphBlock*     first  = innerBlocks[0].get();
                DigitizerUi::UiGraphBlock*     second = innerBlocks[1].get();
                const std::vector<std::string> groupedNames{first->blockUniqueName, second->blockUniqueName};

                selectNode(ctx, editor, first);
                selectNode(ctx, editor, second, /*addToSelection*/ true);
                expect(eq(editor.selectedBlockUniqueNames().size(), 2UZ)) << fatal << "both blocks inside the subgraph should be selected\n";

                if (pickInDialog) {
                    clickInBlockContextMenu(ctx, editor, second, "Group blocks and pick graph type...");
                    chooseInBlockSelector(ctx, "gr::scheduler::Simple", "<gr::scheduler::ExecutionPolicy::singleThreaded>");
                } else {
                    clickInBlockContextMenu(ctx, editor, second, "Group blocks");
                }

                expect(waitForRepliesOnEndpoint(ctx, gr::scheduler::property::kBlocksGrouped)) << fatal << "grouping inside a subgraph editor should reach the owning scheduler\n";

                expect(waitFor(ctx,
                    [&] {
                        DigitizerUi::UiGraphBlock* nested = findSubgraphInCurrentRoot();
                        if (!nested) {
                            return false;
                        }
                        DigitizerUi::UiGraphBlock* interior = nested->isScheduler() ? (nested->childBlocks.empty() ? nullptr : nested->childBlocks.front().get()) : nested;
                        return interior && std::ranges::all_of(groupedNames, [interior](const std::string& name) { return interior->findBlockByUniqueName(name) != nullptr; });
                    }))
                    << fatal << "a nested subgraph holding both blocks should appear inside the subgraph editor\n";

                DigitizerUi::UiGraphBlock* nested = findSubgraphInCurrentRoot();
                expect(nested != nullptr) << fatal;
                expect(pickInDialog ? nested->isScheduler() : nested->isGraph()) << "the type of subgraph should match what the user asked for\n";

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
    gr::registerBlock<opendigitizer::Arithmetic, float, double>(registry);
    gr::registerBlock<opendigitizer::SineSource, float>(registry);
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
    // TODO: fix gnuradio so the explicit alias is not needed for this block to be reachable by its own name
    gr::registerBlock<"gr::testing::AtomicCountingSink", gr::testing::AtomicCountingSink, float>(registry);

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
    g_state.unloadDashboard(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
