#include "FlowgraphPage.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"
#include "TestingBlockInspectionUtils.hpp"

#include <ToolbarView.hpp>

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
#include "blocks/ImControlNumber.hpp"
#include "blocks/ImControlText.hpp"
#include "blocks/ImControlToggle.hpp"
#include "blocks/ImControlTrigger.hpp"
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
    DigitizerUi::ToolbarView   toolbar; // need to draw the toolbar since ui control blocks only do things when drawn

    void onDashboardLoaded() override { flowgraphPage.setDashboard(dashboard.get()); }
    void onDashboardAboutToBeUnloaded() override { flowgraphPage.setDashboard(nullptr); }

    ~TestState() override { TestState::onDashboardAboutToBeUnloaded(); }

    void waitForScheduler(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) override {
        opendigitizer::test::TestDashboardRunner::waitForScheduler(ctx, location);

        // the default waitForScheduler waits for the scheduler to become active. we also want to wait for inspection to complete
        if (flowgraphPage.editorCount() == 0) {
            std::println("\tScheduler started, sending kSchedulerInspect message");
            gr::Message message;
            message.cmd      = gr::message::Command::Get;
            message.endpoint = gr::scheduler::property::kSchedulerInspect;
            message.data     = {};
            dashboard->session.graphModel.sendMessage(std::move(message));
        } else {
            std::println("\tGraph does not need inspection / it seems populated already");
        }

        waitUntil(ctx, "the scheduler inspection yields a root block", [this] { return !dashboard->session.graphModel.rootBlock.blockUniqueName.empty(); }, location);
        std::println("\tInspection succeeded, we got a root editor");
        flowgraphPage.pushEditor("rootBlock node editor", dashboard->session.graphModel, std::addressof(dashboard->session.graphModel.rootBlock));
    }

    // for testing topology changing messages
    void waitForGraphModelUpdate(ImGuiTestContext* ctx, std::size_t expectedBlockCount) {
        waitUntil(ctx, std::format("the graph has {} blocks", expectedBlockCount), [this, expectedBlockCount] { return blocks().size() == expectedBlockCount; });
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
            flowgraphPage.pushEditor(block->blockUniqueName, dashboard->session.graphModel, block.get());
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

using namespace opendigitizer::test;
TESTING_BLOCK_INSPECTION_UTILS_MAKE_ALL_GLOBAL_STATE_AWARE_OVERLOADS(g_state.dashboard->session)

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    [[nodiscard]] static bool waitForRepliesOnEndpoint(ImGuiTestContext* ctx, std::string_view endpoint, std::size_t count = 1UZ) {
        const bool replied = waitFor(ctx, [endpoint, count] { //
            return static_cast<std::size_t>(std::ranges::count_if(g_state.collectedMessages, [endpoint](const gr::Message& message) { return message.endpoint == endpoint; })) >= count;
        });
        g_state.clearMessages();
        return replied;
    }

    // no deadline, see TestDashboardRunner::waitUntil
    [[nodiscard]] static bool waitFor(ImGuiTestContext* ctx, const std::function<bool()>& predicate) {
        while (!predicate()) {
            ctx->Yield();
        }
        return true;
    }

    static void dragPinToPin(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphPort* fromPort, const DigitizerUi::UiGraphPort* toPort) {
        ctx->Yield(2); // ax::NodeEditor pin positions are not resolved until the frame after the first draw
        waitForSettledView(ctx, editor);

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
        g_state.dashboard->session.graphModel.sendMessage(std::move(message));
    }

    static constexpr std::array  kEditingButtons{"Add block...", "Add sub graph...", "Add remote signal...", "Rearrange blocks"};
    static constexpr const char* kLocalTabRef = "//Fit Window/maintabbar/Local";

    static constexpr auto fitGuiFunc = [](ImGuiTestContext*) {
        IMW::Window window("Fit Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowPos({0, 0});
        ImGui::SetWindowSize(ImVec2(640, 420));
        if (g_state.dashboard) {
            g_state.flowgraphPage.draw();
            g_state.dashboard->handleMessages();
        }
    };

    // scattered pairs carry deliberately tangled stored positions (ui_constraints): loading neither arranges nor fits
    static std::string sourceSinkPairsGraph(std::size_t nPairs, bool scattered = false) {
        const auto  position = [scattered](std::size_t seed) { return scattered ? std::format("      ui_constraints:\n        x: {}\n        y: {}\n", static_cast<int>((seed * 389UZ) % 1300UZ), static_cast<int>((seed * 233UZ) % 900UZ)) : std::string{}; };
        std::string blocks;
        std::string connections;
        for (std::size_t i = 0UZ; i < nPairs; ++i) {
            blocks += std::format("  - id: \"opendigitizer::SineSource<float32>\"\n    parameters:\n      name: \"source{0}\"\n{1}"
                                  "  - id: \"gr::basic::DataSink<float32>\"\n    parameters:\n      name: \"sink{0}\"\n{2}",
                i, position(2UZ * i + 1UZ), position(2UZ * i + 2UZ));
            connections += std::format("  - [source{0}, 0, sink{0}, 0]\n", i);
        }
        return std::format("blocks:\n{}connections:\n{}", blocks, connections);
    }

    static bool allNodesAboveButtonBar(DigitizerUi::FlowgraphEditor& editor, float barPixels) {
        const auto* context      = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        const float barTopCanvas = context->GetViewRect().Max.y - barPixels * context->GetViewRect().GetHeight() / context->GetRect().GetHeight();
        return contentBounds(editor).Max.y <= barTopCanvas;
    }

    static ImRect contentBounds(DigitizerUi::FlowgraphEditor& editor) {
        editor.makeCurrent();
        ImRect bounds(ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), ImVec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
        for (const auto& block : g_state.blocks()) {
            const auto id       = ax::NodeEditor::NodeId(block.get());
            const auto position = ax::NodeEditor::GetNodePosition(id);
            bounds.Add(ImRect(position, position + ax::NodeEditor::GetNodeSize(id)));
        }
        return bounds;
    }

    static std::vector<ImVec2> nodePositions(DigitizerUi::FlowgraphEditor& editor) {
        editor.makeCurrent();
        std::vector<ImVec2> positions;
        for (const auto& block : g_state.blocks()) {
            positions.push_back(ax::NodeEditor::GetNodePosition(ax::NodeEditor::NodeId(block.get())));
        }
        return positions;
    }

    static float viewZoom(DigitizerUi::FlowgraphEditor& editor) {
        const auto* context = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        return context->GetRect().GetWidth() / context->GetViewRect().GetWidth();
    }

    static bool allNodesInView(DigitizerUi::FlowgraphEditor& editor) {
        const auto* context = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        return context->GetViewRect().Contains(contentBounds(editor));
    }

    static void waitForSettledView(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor) {
        const auto* context      = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        ImRect      previous     = context->GetViewRect();
        std::size_t stableFrames = 0UZ;
        for (std::size_t frame = 0UZ; frame < 600UZ && stableFrames < 5UZ; ++frame) {
            ctx->Yield();
            const ImRect current = context->GetViewRect();
            const bool   pending = editor._firstDraw || editor._rearrangeRequested || editor._fitRequested || editor._fitJustApplied;
            stableFrames         = (!pending && current.Min == previous.Min && current.Max == previous.Max) ? stableFrames + 1UZ : 0UZ;
            previous             = current;
        }
        expect(stableFrames >= 5UZ) << fatal << "the editor view settles";
    }

    // the page's editor would find blocks already arranged by the root editor's: keep only the page's
    static DigitizerUi::FlowgraphEditor& loadGraph(ImGuiTestContext* ctx, std::size_t nPairs, bool scattered = false) {
        g_state.reloadFromYamlString(sourceSinkPairsGraph(nPairs, scattered));
        g_state.waitForScheduler(ctx);
        g_state.waitUntil(ctx, "the graph has blocks", [] { return g_state.hasBlocks(); });
        while (g_state.flowgraphPage.editorCount() > 1UZ) {
            g_state.flowgraphPage.popEditor();
        }
        return g_state.flowgraphPage.currentEditor();
    }

    static DigitizerUi::FlowgraphEditor& loadUiControlGraph(ImGuiTestContext* ctx, const char* grc) {
        g_state.flowgraphPage.showEditorControls = false; // make sure mouse can click everything within the view rect
        g_state.reload(cmrc::ui_test_assets::get_filesystem(), grc, "uicontrol");
        g_state.waitForScheduler(ctx);
        g_state.waitUntil(ctx, "the graph has blocks", [] { return g_state.hasBlocks(); });
        ctx->SetRef("Test Window");
        auto& editor = g_state.flowgraphPage.currentEditor();
        ctx->Yield(2);
        waitForSettledView(ctx, editor);
        editor.makeCurrent();
        ax::NodeEditor::NavigateToContent(0.0f);
        ctx->Yield(2);
        return editor;
    }

    static ImVec2 centerUiControlDragHandleInView(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* control) {
        const ImGuiTestItemInfo info = ctx->ItemInfo(std::format("**/{}.uiDragHandle", control->blockName).c_str());
        expect(info.ID != 0u) << fatal << "no drag handle item found for " << control->blockName;
        editor.makeCurrent();
        return ax::NodeEditor::CanvasToScreen(info.RectFull.GetCenter());
    }

    static void dragUiControlDragHandleToPosition(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const DigitizerUi::UiGraphBlock* control, ImVec2 dropScreenPos) {
        ctx->MouseTeleportToPos(centerUiControlDragHandleInView(ctx, editor, control));
        ctx->Yield();
        ctx->MouseDown(ImGuiMouseButton_Left);
        ctx->Yield();
        expect(editor._blockDragConnect.has_value()) << fatal << "pressing the handle starts a drag";
        ctx->MouseLiftDragThreshold(ImGuiMouseButton_Left);
        ctx->Yield();
        ctx->MouseMoveToPos(dropScreenPos);
        ctx->Yield();
        ctx->MouseUp(ImGuiMouseButton_Left);
        ctx->Yield(2); // one frame to handle the drop, one to open the popup
    }

    static ImVec2 findEmptySpotInFlowgraph(DigitizerUi::FlowgraphEditor& editor) {
        editor.makeCurrent();
        const auto*  context = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        const ImRect view    = context->GetRect();
        for (const float fractionX : {0.9f, 0.1f, 0.5f}) {
            for (const float fractionY : {0.5f, 0.1f, 0.9f}) {
                const ImVec2 screenPos{view.Min.x + view.GetWidth() * fractionX, view.Min.y + view.GetHeight() * fractionY};
                const ImVec2 canvasPos = ax::NodeEditor::ScreenToCanvas(screenPos);
                const bool   onNode    = std::ranges::any_of(g_state.blocks(), [&](const auto& block) {
                    const auto   nodeId   = ax::NodeEditor::NodeId(block.get());
                    const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
                    return ImRect(position, position + ax::NodeEditor::GetNodeSize(nodeId)).Contains(canvasPos);
                });
                if (!onNode) {
                    return screenPos;
                }
            }
        }
        expect(false) << fatal << "flowgraph view is completely full of blocks?";
        return {};
    }

    static ImGuiID getFrontmostPopupID(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
        ctx->Yield();
        const ImGuiID popupId = frontmostPopupId();
        expect(popupId != 0u) << fatal << std::format("a popup should be open (requested at line {})", location.line());
        return popupId;
    }

    static std::string getTargetMapForBlock(const char* controlName) { return setting(controlName, "target_map").value_or(std::string{}); }

    static void finishUiControlTest() {
        g_state.flowgraphPage.showEditorControls = true;
        g_state.stopScheduler();
    }

    static void sendSetSettingMessage(const std::string& blockName, gr::property_map data) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.serviceName = blockName;
        message.endpoint    = gr::block::property::kSetting;
        message.data        = std::move(data);
        g_state.dashboard->session.sendMessage(std::move(message));
    }

    /// expect()s a condition for about a second, hopefully to catch any late-arriving messages or similar
    static void expectConditionToStayTrue(ImGuiTestContext* ctx, const std::function<bool()>& condition, const char* what) {
        for (int frame = 0; frame < 60; ++frame) {
            expect(condition()) << what << fatal;
            ctx->Yield();
        }
    }

    static void setBlockPanelAlwaysOpen(DigitizerUi::FlowgraphEditor& editor) { editor._editPaneContext.closeTime = std::chrono::system_clock::now() + std::chrono::hours(1); }

    static void openBlockPanel(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, DigitizerUi::UiGraphBlock* block) {
        const auto owners = editor.ownersForRoot();
        expect(owners.has_value()) << fatal;
        editor._editPaneContext.targetGraph = owners->graph;
        editor._editPaneContext.setSelectedBlock(block, std::addressof(g_state.dashboard->session.graphModel));
        setBlockPanelAlwaysOpen(editor);
        ctx->Yield(2);
    }

    /// opens the "Unlink | Jump to control" context menu and returns the it's imgui ID
    static ImGuiID openContextMenuForProperty(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const char* parameterItemRef) {
        setBlockPanelAlwaysOpen(editor);
        ctx->SetRef("//BlockControlsPanel");
        const ImGuiTestItemInfo info = ctx->ItemInfo(parameterItemRef);
        expect(info.ID != 0u) << fatal << parameterItemRef;
        ctx->MouseMoveToPos(info.RectFull.GetCenter());
        ctx->Yield();
        ctx->MouseClick(ImGuiMouseButton_Right);
        ctx->Yield();
        ctx->SetRef("Test Window");
        return frontmostPopupId();
    }

    static void clickButtonInContextMenuForProperty(ImGuiTestContext* ctx, DigitizerUi::FlowgraphEditor& editor, const char* parameterItemRef, const char* menuItem) {
        const ImGuiID menuId = openContextMenuForProperty(ctx, editor, parameterItemRef);
        expect(menuId != 0u) << fatal << "right-clicking a locked property should open its context menu";
        ctx->SetRef(menuId);
        ctx->ItemClick(menuItem);
        ctx->Yield();
        ctx->SetRef("Test Window");
    }

    static void expectBlockCenteredAtDefaultZoom(DigitizerUi::FlowgraphEditor& editor, const char* blockName, std::source_location location = std::source_location::current()) {
        const DigitizerUi::UiGraphBlock* block = findRootChildByName(blockName);
        expect(block != nullptr) << fatal;
        expect(approx(viewZoom(editor), 1.f, 1e-3f)) << std::format("jumping shows {} at 1:1 zoom (line {})", blockName, location.line());
        editor.makeCurrent();
        const auto*  context                       = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor._editorPtr);
        const auto   nodeId                        = ax::NodeEditor::NodeId(block);
        const ImVec2 blockCenter                   = ax::NodeEditor::GetNodePosition(nodeId) + ax::NodeEditor::GetNodeSize(nodeId) * 0.5f;
        const ImVec2 offsetOfCameraFromBlockCenter = blockCenter - context->GetViewRect().GetCenter();
        expect(std::abs(offsetOfCameraFromBlockCenter.x) < 2.f && std::abs(offsetOfCameraFromBlockCenter.y) < 2.f) //
            << std::format("{} centered in the view, offset ({}, {}) (line {})", blockName, offsetOfCameraFromBlockCenter.x, offsetOfCameraFromBlockCenter.y, location.line());
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
                    opendigitizer::test::waitUntilAllSamplesDrawn(ctx, *g_state.dashboard);

                    std::string firstBlockName = g_state.nameOfFirstBlock();
                    expect(that % !firstBlockName.empty()) << "There should be at least one block";

                    // Delete the first block
                    const auto numBlocksBefore = g_state.blocks().size();

                    g_state.deleteBlock(firstBlockName);
                    ctx->Yield(); // Give time for UI to update

                    // deletion is async, let's wait for kBlockRemoved
                    const auto expectedBlockCount = numBlocksBefore - 1;
                    g_state.waitForGraphModelUpdate(ctx, expectedBlockCount);

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

                waitForSettledView(ctx, g_state.flowgraphPage.currentEditor());
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
                            .exportedName    = {},
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

                auto& graphModel = g_state.dashboard->session.graphModel;
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
                g_state.waitUntil(ctx, "the grouped blocks moved into the subgraph", groupedBlocksLeftRootGraph);

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
            // an embedding host draws the page away from the screen origin
            static constexpr ImVec2 kHostPos{200.f, 150.f};
            ImGuiTest*              t = IM_REGISTER_TEST(engine(), "flowgraph", "Button overlay stays inside an offset host window");
            t->SetVarsDataType<TestState>();

            t->GuiFunc = [](ImGuiTestContext*) {
                IMW::Window window("Offset Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetWindowPos(kHostPos);
                ImGui::SetWindowSize(ImVec2(800, 600));
                if (g_state.dashboard) {
                    g_state.flowgraphPage.draw();
                    g_state.dashboard->handleMessages();
                }
            };

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reloadFromYamlString(simpleGraph);
                g_state.waitForScheduler(ctx);
                g_state.waitUntil(ctx, "the graph has blocks", [] { return g_state.hasBlocks(); });
                ctx->Yield(2);

                const ImGuiWindow* host    = ImGui::FindWindowByName("Offset Host");
                const ImGuiWindow* overlay = ImGui::FindWindowByName("Button Overlay");
                expect(host != nullptr && overlay != nullptr) << fatal;
                const ImRect hostRect(host->Pos, host->Pos + host->Size);
                expect(hostRect.Contains(ImRect(overlay->Pos, overlay->Pos + overlay->Size))) << std::format("overlay at ({}, {}) outside host at ({}, {}) size ({}, {})", overlay->Pos.x, overlay->Pos.y, host->Pos.x, host->Pos.y, host->Size.x, host->Size.y);

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

                auto& graphModel = g_state.dashboard->session.graphModel;
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
                waitForSettledView(ctx, editor);
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
                waitForSettledView(ctx, editor); // a zoom-to-fill would push blocks added by grouping under the button bar

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

                editor.requestRelayout();
                waitForSettledView(ctx, editor);

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

                auto& graphModel = g_state.dashboard->session.graphModel;
                graphModel.requestAvailableBlocksTypesUpdate();
                expect(waitFor(ctx, [&graphModel] { return graphModel.knownSchedulerTypes.contains("gr::scheduler::Simple"); })) << fatal << "the scheduler registry should have reached the UI\n";

                ctx->SetRef("Test Window");
                auto& editor = g_state.flowgraphPage.currentEditor();
                ctx->Yield(2);
                waitForSettledView(ctx, editor);
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

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Editor controls are shown by default and relayout equals the button");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = fitGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                auto& editor = loadGraph(ctx, 2UZ);
                waitForSettledView(ctx, editor);

                for (const char* label : kEditingButtons) {
                    expect(ctx->ItemExists(std::format("//Button Overlay/{}", label).c_str())) << std::format("'{}' shown by default", label);
                }
                expect(ctx->ItemExists(kLocalTabRef)) << "tab bar shown by default";

                ctx->ItemClick("//Button Overlay/Rearrange blocks");
                waitForSettledView(ctx, editor);
                const auto arrangedByButton = nodePositions(editor);

                auto& movedBlock = *g_state.blocks().front();
                expect(movedBlock.storedXY.has_value()) << fatal << "an arranged block has a stored position";
                movedBlock.storedXY = DigitizerUi::UiGraphBlock::StoredXY{movedBlock.storedXY->x + 150.f, movedBlock.storedXY->y + 90.f};
                ctx->Yield(2);
                expect(nodePositions(editor) != arrangedByButton) << fatal << "moving a block changes the layout";

                g_state.flowgraphPage.requestRelayout();
                waitForSettledView(ctx, editor);
                expect(nodePositions(editor) == arrangedByButton) << "requestRelayout() arranges as the button does";
                expect(allNodesAboveButtonBar(editor, 53.f)) << "with the controls shown the fit leaves the button bar free";
                captureScreenshot(*ctx, "Fit Window");

                g_state.stopScheduler();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Hidden editor controls and a relayout that fits the graph into the view");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = fitGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.flowgraphPage.showEditorControls = false;

                auto& largeEditor = loadGraph(ctx, 8UZ);
                waitForSettledView(ctx, largeEditor);

                for (const char* label : kEditingButtons) {
                    expect(!ctx->ItemExists(std::format("//Button Overlay/{}", label).c_str())) << std::format("'{}' hidden", label);
                }
                expect(!ctx->ItemExists(kLocalTabRef)) << "no tab bar";

                expect(viewZoom(largeEditor) < 1.f) << fatal << "the large graph needs shrinking to fit the view";
                expect(allNodesInView(largeEditor)) << "the first-draw arrange fits the whole graph into the view";
                captureScreenshot(*ctx, "Fit Window");

                auto& smallEditor = loadGraph(ctx, 1UZ);
                waitForSettledView(ctx, smallEditor);

                auto* context = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(smallEditor._editorPtr);
                smallEditor.makeCurrent();
                context->NavigateTo(ImRect(ImVec2(-3000.f, -3000.f), ImVec2(3000.f, 3000.f)), true, 0.f);
                waitForSettledView(ctx, smallEditor);
                expect(viewZoom(smallEditor) < 1.f) << fatal << "zoomed out before the relayout";

                g_state.flowgraphPage.requestRelayout();
                waitForSettledView(ctx, smallEditor);
                expect(approx(viewZoom(smallEditor), 1.f, 1e-3f)) << "a graph that fits is shown at 1:1, never enlarged";
                expect(allNodesInView(smallEditor));
                const ImVec2 offCentre = contentBounds(smallEditor).GetCenter() - context->GetViewRect().GetCenter();
                expect(std::abs(offCentre.x) < 1.f && std::abs(offCentre.y) < 1.f) << std::format("graph centred, offset ({}, {})", offCentre.x, offCentre.y);
                captureScreenshot(*ctx, "Fit Window");

                g_state.flowgraphPage.showEditorControls = true;
                g_state.stopScheduler();
            };
        }

        // ui controls have a unique gui func because we also need to draw the toolbar
        constexpr auto uiControlGuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(1024, 800));
            if (g_state.dashboard) {
                if (g_state.dashboard->isInitialised) {
                    g_state.toolbar.draw(g_state.dashboard->session, false);
                }
                g_state.flowgraphPage.draw();
                g_state.dashboard->handleMessages();
            }
        };

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "UI control drag and drop lists only properties with a matching type");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_dragdrop.grc");

                struct FilterCase {
                    const char*              control;
                    std::vector<const char*> offered;
                    std::vector<const char*> notOffered;
                };
                const std::vector<FilterCase> cases{
                    {"controlToggle", {"visible", "plot_tags"}, {"signal_name", "sample_rate"}},
                    {"controlTrigger", {"visible", "plot_tags"}, {"signal_name", "sample_rate"}},
                    {"controlText", {"signal_name", "signal_unit"}, {"visible", "sample_rate"}},
                    {"controlNumber", {"sample_rate", "signal_min"}, {"visible", "signal_name"}},
                };
                for (const FilterCase& filterCase : cases) {
                    DigitizerUi::UiGraphBlock* control = findRootChildByName(filterCase.control);
                    DigitizerUi::UiGraphBlock* sink    = findRootChildByName("sink1");
                    expect(control != nullptr && sink != nullptr) << fatal;

                    dragUiControlDragHandleToPosition(ctx, editor, control, nodeCentreOnScreen(editor, sink));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    for (const char* property : filterCase.offered) {
                        expect(ctx->ItemExists(std::format("**/{}", property).c_str())) << filterCase.control << " offers " << property;
                    }
                    for (const char* property : filterCase.notOffered) {
                        expect(!ctx->ItemExists(std::format("**/{}", property).c_str())) << filterCase.control << " must not offer " << property;
                    }
                    ctx->SetRef("Test Window");
                    ctx->KeyPress(ImGuiKey_Escape);
                    ctx->Yield(2);
                    expect(frontmostPopupId() == 0u) << "escape closes the property selector";
                }

                "if there are no compatible properties then the popup is empty"_test = [&] {
                    DigitizerUi::UiGraphBlock* toggle = findRootChildByName("controlToggle");
                    DigitizerUi::UiGraphBlock* text2  = findRootChildByName("controlText2");
                    expect(toggle != nullptr && text2 != nullptr) << fatal;
                    dragUiControlDragHandleToPosition(ctx, editor, toggle, nodeCentreOnScreen(editor, text2));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    expect(!ctx->ItemExists("**/value")) << "the properties shown are not the ones expected (none of them)";
                    captureScreenshot(*ctx);
                    ctx->ItemClick("**/Done");
                    ctx->Yield();
                    ctx->SetRef("Test Window");
                    expect(frontmostPopupId() == 0u) << "Done closes the property selector";
                };

                finishUiControlTest();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "UI control drag and drop connects when the user closes the popup");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_dragdrop.grc");

                "the connection is made when Done is pressed and not during the interaction"_test = [&] {
                    g_state.waitUntil(ctx, "sink1 starts invisible", [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", false); });

                    dragUiControlDragHandleToPosition(ctx, editor, findRootChildByName("controlToggle"), nodeCentreOnScreen(editor, findRootChildByName("sink1")));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    ctx->ItemClick("**/visible");
                    expectConditionToStayTrue(ctx, [] { return getTargetMapForBlock("controlToggle").empty() && blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", false); }, "unchecking something in the popup doesn't do anything");

                    ctx->ItemClick("**/Done");
                    ctx->SetRef("Test Window");
                    g_state.waitUntil(ctx, "Done connects the property", [] { return getTargetMapForBlock("controlToggle") == "sink1:visible"; });
                    expectConditionToStayTrue(ctx, [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", false); }, "closing the popup makes a connection but connecting does not send an initial value, the user must interact with the control first");
                };

                "a property can be set by something other than its ui controls, and the ui controls will not notice or try to change that"_test = [&] {
                    sendSetSettingMessage("sink1", gr::property_map{{"visible", true}});
                    g_state.waitUntil(ctx, "settings were applied", [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", true); });
                    expectConditionToStayTrue(ctx, [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", true); }, "connection doesn't try to adjust mismatched properties");
                };

                "escape cancels the user's changes in the popup"_test = [&] {
                    expect(blockHasStringKeyValuePair("sink1", "signal_name", "first")) << fatal;
                    dragUiControlDragHandleToPosition(ctx, editor, findRootChildByName("controlText"), nodeCentreOnScreen(editor, findRootChildByName("sink1")));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    ctx->ItemClick("**/signal_name");
                    ctx->SetRef("Test Window");
                    ctx->KeyPress(ImGuiKey_Escape);
                    expectConditionToStayTrue(ctx, [] { return getTargetMapForBlock("controlText").empty() && blockHasStringKeyValuePair("sink1", "signal_name", "first"); }, "operation is cancelled after the user presses escape");
                };

                "you can try to use a UI control to control another UI control"_test = [&] {
                    dragUiControlDragHandleToPosition(ctx, editor, findRootChildByName("controlText2"), nodeCentreOnScreen(editor, findRootChildByName("controlText")));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    ctx->ItemClick("**/value");
                    ctx->ItemClick("**/Done");
                    ctx->SetRef("Test Window");
                    g_state.waitUntil(ctx, "connection has completed", [] { return getTargetMapForBlock("controlText2") == "controlText:value"; });
                };

                finishUiControlTest();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "add glob entries with the Multi-Select dialog");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_dragdrop.grc");

                "when opening multi-select with a number control, the popup only shows properties which are numbers (are compatible)"_test = [&] {
                    dragUiControlDragHandleToPosition(ctx, editor, findRootChildByName("controlNumber"), findEmptySpotInFlowgraph(editor));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    expect(ctx->ItemExists("**/sample_rate"));
                    expect(!ctx->ItemExists("**/visible"));
                    expect(!ctx->ItemExists("**/signal_name"));
                    ctx->SetRef("Test Window");
                    ctx->KeyPress(ImGuiKey_Escape);
                    ctx->Yield(2);
                };

                "when opening multi-select for a boolean control, only compatible properties appear, and the connections about to be made are previewed"_test = [&] {
                    g_state.waitUntil(ctx, "sink1 starts invisible", [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", false); });

                    dragUiControlDragHandleToPosition(ctx, editor, findRootChildByName("controlToggle"), findEmptySpotInFlowgraph(editor));
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    expect(ctx->ItemExists("**/visible"));
                    expect(ctx->ItemExists("**/plot_tags"));
                    expect(!ctx->ItemExists("**/signal_name"));
                    expect(!ctx->ItemExists("**/sample_rate"));

                    // hovering an option should show a preview of what would be selected
                    ctx->MouseMove("**/visible");
                    ctx->Yield(2);
                    captureScreenshot(*ctx, "//Test Window");

                    ctx->ItemClick("**/visible");
                    ctx->ItemClick("**/Done");
                    ctx->SetRef("Test Window");
                    g_state.waitUntil(ctx, "the '*' selector lands in the control's target_map", [] { return getTargetMapForBlock("controlToggle") == "*:visible"; });
                    expectConditionToStayTrue(ctx, [] { return blockHasBasicTypeKeyValuePair<bool>("sink1", "visible", false); }, "connecting does not set the value on the matching blocks");
                };

                finishUiControlTest();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "UI-controlled properties in the block properties panel are marked as locked");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                openBlockPanel(ctx, editor, findRootChildByName("sinkA"));
                ctx->SetRef("//BlockControlsPanel");

                "a locked property cannot be edited"_test = [&] {
                    expect(blockHasStringKeyValuePair("sinkA", "signal_name", "alpha")) << fatal;
                    const ImGuiTestItemInfo info = ctx->ItemInfo("**/##parameter_signal_name");
                    expect(info.ID != 0u) << fatal;
                    setBlockPanelAlwaysOpen(editor);
                    ctx->MouseMoveToPos(info.RectFull.GetCenter());
                    ctx->Yield();
                    ctx->MouseClick(ImGuiMouseButton_Left);
                    ctx->KeyChars("edited");
                    ctx->Yield(2);
                    expectConditionToStayTrue(ctx, [] { return blockHasStringKeyValuePair("sinkA", "signal_name", "alpha"); }, "property does not get changed");
                    captureScreenshot(*ctx);
                };

                "right-clicking a locked property opens the Unlink / Jump to control popup menu"_test = [&] {
                    const ImGuiID menuId = openContextMenuForProperty(ctx, editor, "**/##parameter_signal_name");
                    expect(menuId != 0u) << fatal << "the context menu should open";
                    ctx->SetRef(menuId);
                    expect(ctx->ItemExists("Unlink"));
                    expect(ctx->ItemExists("Jump to control"));
                    ctx->SetRef("Test Window");
                    ctx->PopupCloseAll();
                    ctx->Yield(2);
                };

                "right-clicking an unlocked property does not open the Unlink / Jump to control popup menu"_test = [&] {
                    const ImGuiID menuId = openContextMenuForProperty(ctx, editor, "**/##parameter_signal_unit");
                    expect(menuId == 0u) << "popup menu should only open for locked properties";
                };

                finishUiControlTest();
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "Unlink opens a dialog to remove or edit UI control connections");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                "a single direct connection is removed without a dialog"_test = [ctx] {
                    auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                    openBlockPanel(ctx, editor, findRootChildByName("sinkA"));
                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_quantity", "Unlink");
                    ctx->Yield(2);
                    expect(frontmostPopupId() == 0u) << "no dialog should appear for a single direct connection";
                    g_state.waitUntil(ctx, "the entry has been removed from the control's target_map", [] { return getTargetMapForBlock("controlA").empty(); });
                    finishUiControlTest();
                };

                "if there is a glob type connection, then the unlink dialog should open"_test = [ctx] {
                    auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                    openBlockPanel(ctx, editor, findRootChildByName("sinkA"));
                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_name", "Unlink");
                    ctx->SetRef("Modify Connections");
                    expect(ctx->ItemExists("**/##connected0")) << fatal << "the glob connection is shown in the dialog";

                    ctx->ItemClick("**/##connected0");
                    ctx->Yield(2);
                    captureScreenshot(*ctx);
                    expectConditionToStayTrue(ctx, [] { return getTargetMapForBlock("controlB") == "*:signal_name"; }, "unchecking doesn't change the target_map, you have to press Done");
                    ctx->ItemClick("**/Cancel");
                    ctx->Yield(2);
                    ctx->SetRef("Test Window");
                    expectConditionToStayTrue(ctx, [] { return getTargetMapForBlock("controlB") == "*:signal_name"; }, "if you press cancel, nothing is changed");

                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_name", "Unlink");
                    ctx->SetRef("Modify Connections");
                    ctx->ItemClick("**/##connected0");
                    ctx->ItemClick("**/Done");
                    ctx->SetRef("Test Window");
                    g_state.waitUntil(ctx, "pressing Done applies the changes", [] { return getTargetMapForBlock("controlB").empty(); });
                    finishUiControlTest();
                };

                "multiple direct connections open the dialog as well"_test = [ctx] {
                    auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                    openBlockPanel(ctx, editor, findRootChildByName("sinkB"));
                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_quantity", "Unlink");
                    ctx->SetRef("Modify Connections");
                    expect(ctx->ItemExists("**/##connected0") && ctx->ItemExists("**/##connected1")) << fatal << "both connections are listed";

                    ctx->ItemClick("**/##connected0");
                    ctx->ItemClick("**/##connected1");
                    ctx->ItemClick("**/##connected1");
                    expectConditionToStayTrue(ctx, [] { return getTargetMapForBlock("controlC") == "sinkB:signal_quantity" && getTargetMapForBlock("controlD") == "sinkB:signal_quantity"; }, "checking/unchecking should not apply changes");
                    ctx->ItemClick("**/Done");
                    ctx->SetRef("Test Window");
                    g_state.waitUntil(ctx, "the one deselected connection was removed", [] { return getTargetMapForBlock("controlC").empty() != getTargetMapForBlock("controlD").empty(); });
                    expect((getTargetMapForBlock("controlC") == "sinkB:signal_quantity") != (getTargetMapForBlock("controlD") == "sinkB:signal_quantity")) << "the connection that was still selected is still present";
                    finishUiControlTest();
                };
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "jump to control functionality");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = uiControlGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                "if there is only one ui control, it gets focused without a dialog"_test = [ctx] {
                    auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                    openBlockPanel(ctx, editor, findRootChildByName("sinkA"));
                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_name", "Jump to control");
                    ctx->Yield(2);
                    expect(frontmostPopupId() == 0u) << "a single controller needs no chooser";
                    waitForSettledView(ctx, editor);
                    expectBlockCenteredAtDefaultZoom(editor, "controlB");
                    finishUiControlTest();
                };

                "if there are multiple UI control blocks controlling a property, a dialog opens asking which one to jump to"_test = [ctx] {
                    auto& editor = loadUiControlGraph(ctx, "examples/qa_uicontrol_connections.grc");
                    openBlockPanel(ctx, editor, findRootChildByName("sinkB"));
                    clickButtonInContextMenuForProperty(ctx, editor, "**/##parameter_signal_quantity", "Jump to control");
                    const ImGuiID popupId = getFrontmostPopupID(ctx);
                    ctx->SetRef(popupId);
                    expect(ctx->ItemExists("**/controlC") && ctx->ItemExists("**/controlD")) << fatal << "both controllers are in the popup";

                    ctx->ItemClick("**/controlC");
                    waitForSettledView(ctx, editor);
                    expectBlockCenteredAtDefaultZoom(editor, "controlC");
                    expect(frontmostPopupId() != 0u) << "if you select a controller to jump to, it does not close the popup";

                    ctx->ItemClick("**/controlD");
                    waitForSettledView(ctx, editor);
                    expectBlockCenteredAtDefaultZoom(editor, "controlD");

                    ctx->ItemClick("**/Done");
                    ctx->Yield(2);
                    ctx->SetRef("Test Window");
                    expect(frontmostPopupId() == 0u) << "Done closes the popup";
                    finishUiControlTest();
                };
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "One graph with and without editor controls, as loaded and after a relayout");
            t->SetVarsDataType<TestState>();
            t->GuiFunc = fitGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                for (const bool showControls : {true, false}) {
                    g_state.flowgraphPage.showEditorControls = showControls;
                    auto& editor                             = loadGraph(ctx, 8UZ, /*scattered*/ true);
                    waitForSettledView(ctx, editor);

                    const auto stored = [] {
                        std::vector<ImVec2> positions;
                        for (const auto& block : g_state.blocks()) {
                            positions.emplace_back(block->storedXY ? ImVec2(block->storedXY->x, block->storedXY->y) : ImVec2(-1.f, -1.f));
                        }
                        return positions;
                    }();
                    expect(nodePositions(editor) == stored) << "as loaded: blocks keep their stored positions, not arranged";
                    expect(approx(viewZoom(editor), 1.f, 1e-3f)) << "as loaded: not fitted";
                    captureScreenshot(*ctx, "Fit Window");

                    g_state.flowgraphPage.requestRelayout();
                    waitForSettledView(ctx, editor);
                    expect(nodePositions(editor) != stored) << "after the relayout the blocks are arranged";
                    expect(allNodesInView(editor)) << "after the relayout the whole graph is visible";
                    if (showControls) {
                        expect(allNodesAboveButtonBar(editor, 53.f)) << "the button bar covers no block";
                    }
                    captureScreenshot(*ctx, "Fit Window");

                    g_state.stopScheduler();
                }
                g_state.flowgraphPage.showEditorControls = true;
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
    gr::registerBlock<DigitizerUi::ImControlNumber>(registry);
    gr::registerBlock<DigitizerUi::ImControlToggle>(registry);
    gr::registerBlock<DigitizerUi::ImControlTrigger>(registry);
    gr::registerBlock<DigitizerUi::ImControlText>(registry);
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

    // let the flowgraph editors draw the block properties panel, as the application does
    g_state.flowgraphPage.requestBlockControlsPanel = [](DigitizerUi::components::BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& size, bool verticalLayout) { DigitizerUi::components::BlockControlsPanel(panelContext, pos, size, verticalLayout); };

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_chart.grc");

    auto result = app.runTests();
    g_state.unloadDashboard(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
