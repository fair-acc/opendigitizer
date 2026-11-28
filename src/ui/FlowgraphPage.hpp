#pragma once

#include "Dashboard.hpp"
#include "GraphModel.hpp"

#include "components/Block.hpp"
#include "components/ComputeDomainTag.hpp"
#include "components/NewBlockSelector.hpp"
#include "components/SignalSelector.hpp"
#include "components/Splitter.hpp"
#include "components/UiControlInteractionComponents.hpp"

#include <imgui_node_editor.h>

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gr::od {

struct FlowgraphEditor {
    struct ExportPortMessageData {
        std::string uniqueBlockName;
        std::string portDirection;
        std::string portName;
        std::string exportedName;
        bool        exportFlag;
    };

    struct BoundingBox {
        float minX = 0;
        float minY = 0;
        float maxX = 0;
        float maxY = 0;

        constexpr void addRectangle(ImVec2 position, ImVec2 size) {
            minX = std::min(minX, position[0]);
            minY = std::min(minY, position[1]);
            maxX = std::max(maxX, position[0] + size[0]);
            maxY = std::max(maxY, position[1] + size[1]);
        }

        [[nodiscard]] constexpr bool contains(ImVec2 position) const { //
            return position.x >= minX && position.y >= minY && position.x <= maxX && position.y <= maxY;
        }
    };
    constexpr static BoundingBox kDefaultBoundingBox{.minX = 0, .minY = 0, .maxX = 100, .maxY = 100};

    enum class EdgeConflict { InputHasInternalConnection, UnexportingPortHasExternalConnection };

    struct UnexportPortRequest {
        ExportPortMessageData      message; // exportFlag = false
        std::string                exportedName;
        std::optional<gr::Message> thenEmplaceEdge{}; // edge to create once the port is unexported
    };

    struct NodeDrawResult {
        ImVec2 topLeft;
        // to prevent pins from overlapping with other content, only place them on some top portion of the node
        float pinAreaHeight = 0.F;
    };

    struct Buttons {
        bool openNewBlockDialog : 1       = false;
        bool openNewSubGraphDialog : 1    = false;
        bool openRemoteSignalSelector : 1 = false;
        bool rearrangeBlocks : 1          = false;
        bool exportAllUnusedPorts : 1     = false;
        bool closeWindow : 1              = false;
    };

    struct SchedulerGraphPair {
        std::string scheduler;
        std::string graph;
    };

    std::function<void()> closeRequestedCallback;

    std::function<void(UiGraphModel*)>            openNewBlockSelectorCallback;
    std::function<void(UiGraphModel*)>            openNewSubGraphSelectorCallback;
    std::function<void(UiGraphModel*)>            openAddRemoteSignalCallback;
    std::function<void(std::vector<std::string>)> openGroupBlocksSelectorCallback;

    std::string                          exportPortTextField;
    std::optional<ExportPortMessageData> exportPortRequest;
    std::optional<UnexportPortRequest>   unexportPortRequest;

    // export deferred until the user confirms disconnecting the internal edge of the input port
    std::optional<ExportPortMessageData> exportConflictRequest;

    bool showEditorControls  = true;
    bool showUiControlBlocks = true;

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    std::function<void(UiGraphBlock*)> requestGraphEdit;

    std::function<void(const UiGraphBlock&, float)> drawUiControlWidget;

    ax::NodeEditor::Config _editorConfig;
    std::string            _editorName;
    std::size_t            _editorLevel = 0UZ;

    UiGraphModel* _graphModel = nullptr;
    std::string   _rootBlockUniqueName;
    std::string   _exportPortTargetBlockUniqueName;

    ax::NodeEditor::EditorContext* _editorPtr = nullptr;

    bool   _firstDraw          = true;
    bool   _rearrangeRequested = false;
    bool   _fitRequested       = false;
    bool   _fitJustApplied     = false;
    ImVec2 _lastCanvasSize;

    const UiGraphBlock* _filterBlock   = nullptr;
    UiGraphBlock*       _selectedBlock = nullptr;

    // a changed count refreshes NodeIds that may dangle
    std::uint64_t _seenBlockDestructionCount = 0;

    components::BlockControlsPanelContext _editPaneContext;
    components::SplitterState             _splitter;
    ImVec2                                _contextMenuPosition;

    float                                _timeSpentHoldingPin = 0.0;
    std::optional<ExportPortMessageData> _draggingPinExportRequest;

    std::optional<components::BlockDragConnectInteraction> _blockDragConnect;
    std::optional<components::UiControlPropertyLinkPopup>  _uiControlPropertyLinkPopup;
    std::optional<components::UiControlMultiSelectPopup>   _uiControlMultiSelectPopup;
    std::optional<components::ModifyUiConnectionsPopup>    _modifyUiConnectionsPopup;

    std::optional<std::string> _focusBlockRequest;

    UiGraphBlock* _blockContextMenuRequest = nullptr;

    std::unordered_map<const UiGraphPort*, ImVec2>  _pinPivots;
    std::unordered_map<const UiGraphBlock*, ImVec4> _deviceColours; // per frame: the blocks running on a device

    // set from within a context menu, handled once the menu popup is closed (a modal opened from inside a popup would be nested in it)
    std::optional<std::vector<std::string>> _pendingGroupBlocksRequest;

    float        _timeSpentHoveringBoundingBoxExterior    = 0.0;
    bool         _wasHoveringBoundingBoxExteriorThisFrame = false;
    EdgeConflict _popupEdgeConflict{};

    FlowgraphEditor(std::string name, UiGraphModel& graphModel, UiGraphBlock* rootBlock, std::size_t level) : _editorConfig(defaultEditorConfig()), _editorName(std::move(name)), _editorLevel(level), _graphModel(&graphModel), _rootBlockUniqueName(rootBlock->blockUniqueName), _exportPortTargetBlockUniqueName(rootBlock->blockUniqueName), _editorPtr(ax::NodeEditor::CreateEditor(std::addressof(_editorConfig))) {
        makeCurrent();
        _editPaneContext.showNeighboursPreview = true;
        _editPaneContext.focusBlockRequest     = [this](const std::string& uniqueName) { requestFocusOnBlock(uniqueName); };

        if (rootBlock->isScheduler()) {
            if (!rootBlock->childBlocks.empty()) {
                assert(std::get_if<UiGraphBlock::SchedulerBlockInfo>(&rootBlock->blockCategoryInfo) && std::get_if<UiGraphBlock::SchedulerBlockInfo>(&rootBlock->blockCategoryInfo)->childrenLoaded);
                _rootBlockUniqueName = rootBlock->childBlocks.front()->blockUniqueName;
            }
            // otherwise the children of the scheduler block are not loaded yet, we check every draw() to see if they are
        }
    }

    ~FlowgraphEditor() {
        makeCurrent();
        ax::NodeEditor::DestroyEditor(_editorPtr);
    }
    FlowgraphEditor(const FlowgraphEditor&)            = delete;
    FlowgraphEditor& operator=(const FlowgraphEditor&) = delete;

    void dropReferencesToDeletedBlocks();
    void focusOnBlockWithDefaultZoomLevel(const UiGraphBlock& block) const;
    void drawUiControlPopups();

    /// how the user drags a connection from a UI block and which blocks are valid targets: what to draw
    [[nodiscard]] std::optional<components::BlockDragConnectOverlay> updateUiControlDragConnect(std::span<UiGraphBlock* const> drawnBlocks);

    void makeCurrent() { ax::NodeEditor::SetCurrentEditor(_editorPtr); }

    ax::NodeEditor::Config defaultEditorConfig() {
        ax::NodeEditor::Config config;
        config.SettingsFile = nullptr;
        config.UserPointer  = this;

        config.SaveSettings = [](const char* data, size_t size, ax::NodeEditor::SaveReasonFlags /*reason*/, void* userPointer) -> bool {
            auto* editor = static_cast<FlowgraphEditor*>(userPointer);
            if (auto root = editor->rootBlock()) {
                root->storedEditorSettings.assign(data, size);
            }
            return true;
        };

        config.LoadSettings = [](char* data, void* userPointer) -> size_t {
            auto*       editor   = static_cast<FlowgraphEditor*>(userPointer);
            const auto  root     = editor->rootBlock();
            const auto& settings = root ? root->storedEditorSettings : std::string{};
            if (data) {
                settings.copy(data, settings.size());
            }
            return settings.size();
        };

        return config;
    }

    void requestExportPort(const ExportPortMessageData& request);
    void exportAllUnusedPorts();

    [[nodiscard]] std::vector<const UiGraphEdge*> externalEdgesForExportedPort(const std::string& exportedName) const;
    [[nodiscard]] bool                            hasExternalEdgesForExportedPort(const std::string& exportedName) const { return !externalEdgesForExportedPort(exportedName).empty(); }
    [[nodiscard]] const UiGraphEdge*              internalEdgeForInputPort(const ExportPortMessageData& request) const;
    void                                          requestEdgeRemoval(const UiGraphEdge& edge);

    [[nodiscard]] static std::optional<ImVec4> canvasBackgroundColor() {
        if (!LookAndFeel::instance().flowgraph.canvasBackground) {
            return std::nullopt; // transparent, let caller default to whatever is behind the canvas
        }
        return LookAndFeel::instance().palette().flowgraphBg;
    }

    void updateStyle() {
        makeCurrent();

        const auto& flowgraphLookAndFeel = LookAndFeel::instance().flowgraph;
        const auto& palette              = LookAndFeel::instance().palette();

        auto& style           = ax::NodeEditor::GetStyle();
        style.NodePadding     = ImVec4{flowgraphLookAndFeel.nodePadding, flowgraphLookAndFeel.nodePadding, flowgraphLookAndFeel.nodePadding, flowgraphLookAndFeel.nodePadding};
        style.NodeRounding    = flowgraphLookAndFeel.nodeRounding;
        style.NodeBorderWidth = flowgraphLookAndFeel.nodeBorderWidth;
        style.PinRounding     = flowgraphLookAndFeel.imguiNodeEditorPortRounding;
        style.GroupRounding   = flowgraphLookAndFeel.imguiNodeEditorGroupNodeRounding;
        style.LinkStrength    = flowgraphLookAndFeel.linkStrength;

        using enum ax::NodeEditor::StyleColor;
        style.Colors[StyleColor_Bg]                  = canvasBackgroundColor().value_or(ImVec4{});
        style.Colors[StyleColor_Grid]                = flowgraphLookAndFeel.canvasGrid ? palette.flowgraphGrid : ImVec4{};
        style.Colors[StyleColor_NodeBg]              = palette.flowgraphNodeBg;
        style.Colors[StyleColor_NodeBorder]          = palette.flowgraphNodeBorder;
        style.Colors[StyleColor_HovNodeBorder]       = palette.flowgraphNodeBorderHovered;
        style.Colors[StyleColor_SelNodeBorder]       = palette.flowgraphNodeBorderSelected;
        style.Colors[StyleColor_NodeSelRect]         = palette.flowgraphSelectionRect;
        style.Colors[StyleColor_NodeSelRectBorder]   = palette.flowgraphSelectionRectBorder;
        style.Colors[StyleColor_HovLinkBorder]       = palette.flowgraphLinkHovered;
        style.Colors[StyleColor_SelLinkBorder]       = palette.flowgraphLinkSelected;
        style.Colors[StyleColor_HighlightLinkBorder] = palette.flowgraphLinkHighlighted;
        style.Colors[StyleColor_LinkSelRect]         = palette.flowgraphSelectionRect;
        style.Colors[StyleColor_LinkSelRectBorder]   = palette.flowgraphSelectionRectBorder;
        style.Colors[StyleColor_PinRect]             = palette.flowgraphPinRect;
        style.Colors[StyleColor_PinRectBorder]       = palette.flowgraphPinRectBorder;
    }

    void draw(const ImVec2& contentTopLeft, const ImVec2& contentSize);

    void requestRelayout() { _rearrangeRequested = true; }

    void requestFocusOnBlock(std::string uniqueName) { _focusBlockRequest = std::move(uniqueName); }

    void drawPortsMenu(const char* text, const char* portDirection, const auto& blockPorts);

    /// what to draw after the graph, above it
    [[nodiscard]] std::optional<components::BlockDragConnectOverlay> drawGraph(const ImVec2& size);
    NodeDrawResult                                                   drawNode(                                                         //
        UiGraphBlock&                                          block,                //
        const UiGraphModel::ControlledPropertyMap&             controlledProperties, //
        std::span<const UiGraphPort* const>                    inputPorts,           //
        std::span<const UiGraphPort* const>                    outputPorts,          //
        std::optional<components::PropertyConnectionIndicator> connectionPreview);

    void updatePinDrag(BoundingBox boundingBox, ImVec4 linkColor);

    void sendPinsConnectedGraphMessage(ax::NodeEditor::PinId startPinId, ax::NodeEditor::PinId endPinId);

    void drawComputeDomainTag(UiGraphBlock& block, const std::optional<components::ComputeDomainTag>& computeDomain);

    /// only while the editor is active, in canvas (local) space
    void drawBoundingBoxExterior(const BoundingBox& canvasSpacingBoundingBox);

    Buttons drawButtons(const ImVec2& contentScreenTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio);

    void sortNodes(UiGraphBlock* rootBlock) const;

    void fitIntoView(const UiGraphBlock& rootBlock, float reservedBottomPixels) const;

    void requestBlockDeletion(const std::string& blockName);

    void requestBlocksGrouping(std::string graphType, const std::vector<std::string>& uniqueNames);

    void requestBlocksUngrouping(const std::string& uniqueName);

    [[nodiscard]] std::vector<std::string> selectedBlockUniqueNames();

    void drawGroupingMenuItems(std::vector<std::string> uniqueNames);

    void drawBlockContextMenu();

    void setFilterBlock(const UiGraphBlock* block) { _filterBlock = block; }

    UiGraphModel* graphModel() const { return _graphModel; }

    UiGraphBlock* rootBlock() const {
        if (!_rootBlockUniqueName.empty()) {
            return _graphModel->recursiveFindBlockByUniqueName(_rootBlockUniqueName).block;
        }
        return nullptr;
    }

    UiGraphBlock* exportPortTargetBlock() const {
        if (!_exportPortTargetBlockUniqueName.empty()) {
            return _graphModel->recursiveFindBlockByUniqueName(_exportPortTargetBlockUniqueName).block;
        }
        return nullptr;
    }
    std::optional<SchedulerGraphPair> ownersForRoot() const {
        auto* root = rootBlock();
        if (!root) {
            return {};
        }
        using RetType        = std::optional<SchedulerGraphPair>;
        const auto graphCase = [root](const UiGraphBlock::GraphBlockInfo& graphInfo) -> RetType { //
            return SchedulerGraphPair{graphInfo.ownerSchedulerUniqueName, root->blockUniqueName};
        };
        const auto schedulerCase = [root](const UiGraphBlock::SchedulerBlockInfo&) -> RetType {
            assert(root->childBlocks.size() == 1);
            return SchedulerGraphPair{root->blockUniqueName, root->childBlocks.front()->blockUniqueName};
        };
        const auto elseCase = [](const auto&) -> RetType {
            assert(false && "A normal block can not be editor root because it cannot have children nor edges");
            return {};
        };
        return std::visit(gr::meta::overloaded{graphCase, schedulerCase, elseCase}, root->blockCategoryInfo);
    }

    /// the scheduler and graph containing the block; not ownersForRoot() for UI control blocks, which the root graph always owns
    static std::optional<SchedulerGraphPair> ownersFor(const UiGraphBlock& block) {
        if (!block.parentBlock) {
            return {};
        }
        return SchedulerGraphPair{block.parentBlock->ownerSchedulerUniqueName(), block.parentBlock->blockUniqueName};
    }
};

struct FlowgraphPage {
    enum class Alignment {
        Left,
        Right,
    };

    bool showEditorControls  = true;
    bool showUiControlBlocks = true;

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    Dashboard*    _dashboard             = nullptr;
    UiGraphModel* _graphModel            = nullptr;
    bool          _currentTabIsFlowGraph = false;

    std::deque<FlowgraphEditor> _editors;

    std::unique_ptr<SignalSelector> _remoteSignalSelector;
    NewBlockSelector                _newBlockSelector;

    FlowgraphPage() = default;
    ~FlowgraphPage();

    void drawUiControlWidget(const UiGraphBlock& control, float maxWidth);
    void drawNodeEditorTab();
    void drawLocalNodeEditor();
    void drawLocalYamlTab();

    void draw() noexcept;

    void setDashboard(Dashboard* dashboard) {
        setGraphModel(dashboard ? std::addressof(dashboard->session.graphModel) : nullptr);
        _dashboard = dashboard;
    }
    void setGraphModel(UiGraphModel* graphModel) {
        _dashboard  = nullptr;
        _graphModel = graphModel;
        _remoteSignalSelector.reset();
        reset();
    }
    void reset();

    auto editorCount() const { return _editors.size(); }
    void pushEditor(std::string name, UiGraphModel& model, UiGraphBlock* rootBlock);
    void popEditor();

    void updateStyle(); // reads from LookAndFeel::instance().style

    void requestRelayout() {
        if (!_editors.empty()) {
            currentEditor().requestRelayout();
        }
    }

    FlowgraphEditor& currentEditor() { return _editors.back(); }
};

} // namespace gr::od
