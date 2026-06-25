#pragma once

#include <functional>
#include <stack>
#include <vector>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "Dashboard.hpp"

#include "components/Block.hpp"
#include "components/NewBlockSelector.hpp"
#include "components/SignalSelector.hpp"

#include "GraphModel.hpp"

struct TestState;
struct TestApp;

namespace DigitizerUi {

// Returns the pin positionY relative to the block
float pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight);
void  drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& name, const std::string& type, bool mainFlowGraph = true);

class FlowgraphEditor {
    friend struct ::TestState;
    friend struct ::TestApp;

private:
    ax::NodeEditor::Config _editorConfig;
    std::string            _editorName;
    std::size_t            _editorLevel = 0UZ;

    UiGraphModel* _graphModel            = nullptr;
    UiGraphBlock* _rootBlock             = nullptr;
    UiGraphBlock* _exportPortTargetBlock = nullptr;

    ax::NodeEditor::EditorContext* _editorPtr = nullptr;

    const UiGraphBlock* _filterBlock   = nullptr;
    UiGraphBlock*       _selectedBlock = nullptr;

    struct ExportPortMessageData {
        std::string uniqueBlockName;
        std::string portDirection;
        std::string portName;
        std::string exportedName;
        bool        exportFlag;
    };

    components::BlockControlsPanelContext _editPaneContext;
    ImVec2                                _contextMenuPosition;

    float                                _timeSpentHoldingPin = 0.0;
    std::optional<ExportPortMessageData> _draggingPinExportRequest;

    static constexpr float borderExteriorHoverFadeTransitionDurationSeconds = 0.4f;
    float                  _timeSpentHoveringBoundingBoxExterior            = 0.0;
    bool                   _wasHoveringBoundingBoxExteriorThisFrame         = false;

    void makeCurrent() { ax::NodeEditor::SetCurrentEditor(_editorPtr); }

    auto defaultEditorConfig() {
        ax::NodeEditor::Config config;
        config.SettingsFile = nullptr;
        config.UserPointer  = this;

        config.SaveSettings = [](const char* data, size_t size, ax::NodeEditor::SaveReasonFlags /*reason*/, void* userPointer) -> bool {
            auto* editor = static_cast<FlowgraphEditor*>(userPointer);
            editor->_rootBlock->storedEditorSettings.assign(data, size);
            return true;
        };

        config.LoadSettings = [](char* data, void* userPointer) -> size_t {
            auto*       editor   = static_cast<FlowgraphEditor*>(userPointer);
            const auto& settings = editor->_rootBlock->storedEditorSettings;
            if (data) {
                settings.copy(data, settings.size());
            }
            return settings.size();
        };

        return config;
    }

public:
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
    constexpr static BoundingBox defaultBoundingBox{.minX = 0, .minY = 0, .maxX = 100, .maxY = 100};

    std::function<void()> closeRequestedCallback;

    std::function<void(UiGraphModel*)> openNewBlockSelectorCallback;
    std::function<void(UiGraphModel*)> openNewSubGraphSelectorCallback;
    std::function<void(UiGraphModel*)> openAddRemoteSignalCallback;

    std::string                          exportPortTextField;
    std::optional<ExportPortMessageData> exportPortRequest;
    void                                 requestExportPort(const ExportPortMessageData& request);

    FlowgraphEditor(std::string name, UiGraphModel& graphModel, UiGraphBlock* rootBlock, std::size_t level) : _editorConfig(defaultEditorConfig()), _editorName(std::move(name)), _editorLevel(level), _graphModel(&graphModel), _rootBlock(rootBlock), _editorPtr(ax::NodeEditor::CreateEditor(std::addressof(_editorConfig))) {
        makeCurrent();

        if (_rootBlock->blockCategory == "ScheduledBlockGroup") {
            // the editor should show this scheduler's graph's children,
            // not its own (as it only has one child -- the graph)
            assert(_rootBlock->childBlocks.size() == 1);
            _exportPortTargetBlock = _rootBlock;
            _rootBlock             = _rootBlock->childBlocks.front().get();
        } else {
            _exportPortTargetBlock = _rootBlock;
        }
    }

    ~FlowgraphEditor() {
        for (auto& child : _rootBlock->childBlocks) {
            child->view.reset();
        }
        makeCurrent();
        ax::NodeEditor::DestroyEditor(_editorPtr);
    }

    void updateStyle() {
        makeCurrent();

        auto& style        = ax::NodeEditor::GetStyle();
        style.NodeRounding = 0;
        style.PinRounding  = 0;

        style.Colors[ax::NodeEditor::StyleColor_Bg]         = LookAndFeel::instance().palette().flowgraphBg;
        style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = LookAndFeel::instance().palette().flowgraphNodeBg;
        style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = LookAndFeel::instance().palette().flowgraphNodeBorder;
    }

    void draw(const ImVec2& contentTopLeft, const ImVec2& contentSize, bool isCurrentEditor);

    void drawGraph(const ImVec2& size);

    struct NodeDrawResult {
        ImVec2 topLeft;
        float  bottomY;
    };
    NodeDrawResult drawNode(UiGraphBlock& block, std::span<const UiGraphPort*> inputPorts, std::span<const UiGraphPort*> outputPorts, float pinHorizontalPadding);

    void applyNodePosition(UiGraphBlock& block, std::optional<BoundingBox>& boundingBox, float pinHorizontalPadding);

    void handlePinDrag(BoundingBox boundingBox, ImVec4 linkColor);

    void sendPinsConnectedGraphMessage(ax::NodeEditor::PinId inputPinId, ax::NodeEditor::PinId outputPinId);

    void drawComputeDomainTag(UiGraphBlock& block);

    /// Must be called while the editor is active, so we are in canvas/local space.
    void drawBoundingBoxExterior(const BoundingBox& canvasSpacingBoundingBox);

    struct Buttons {
        bool openNewBlockDialog : 1       = false;
        bool openNewSubGraphDialog : 1    = false;
        bool openRemoteSignalSelector : 1 = false;
        bool rearrangeBlocks : 1          = false;
        bool closeWindow : 1              = false;
    };

    Buttons drawButtons(const ImVec2& contentTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio);

    void sortNodes(bool all);

    void requestBlockDeletion(const std::string& blockName);

    void setFilterBlock(const UiGraphBlock* block) { _filterBlock = block; }

    UiGraphModel* graphModel() const { return _graphModel; }

    auto* rootBlock() const { return _rootBlock; }

    struct SchedulerGraphPair {
        std::string scheduler;
        std::string graph;
    };
    SchedulerGraphPair ownersForRoot() const {
        if (std::get_if<UiGraphBlock::SchedulerBlockInfo>(&_rootBlock->blockCategoryInfo)) {
            assert(_rootBlock->childBlocks.size() == 1);
            return {_rootBlock->blockUniqueName, _rootBlock->childBlocks.front()->blockUniqueName};
        } else if (auto* graphInfo = std::get_if<UiGraphBlock::GraphBlockInfo>(&_rootBlock->blockCategoryInfo)) {
            return {graphInfo->ownerSchedulerUniqueName, _rootBlock->blockUniqueName};
        } else {
            assert(false && "A normal block can not be editor root, it can not have children and edges");
            return {};
        }
    }

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    std::function<void(UiGraphBlock*)> requestGraphEdit;
};

class FlowgraphPage {
private:
    friend struct ::TestState;
    enum class Alignment {
        Left,
        Right,
    };

    std::shared_ptr<opencmw::client::RestClient> _restClient;
    Dashboard*                                   _dashboard             = nullptr;
    bool                                         _currentTabIsFlowGraph = false;

    std::deque<FlowgraphEditor> _editors;

    std::unique_ptr<SignalSelector> _remoteSignalSelector;
    NewBlockSelector                _newBlockSelector;

    void drawNodeEditorTab();
    void drawLocalYamlTab();
    void drawRemoteYamlTab(Dashboard::Service& service);

public:
    struct DataTypeStyle {
        std::uint32_t color;
        bool          unsignedMarker = false;
        bool          datasetMarker  = false;
    };

    explicit FlowgraphPage(std::shared_ptr<opencmw::client::RestClient> restClient);
    ~FlowgraphPage();

    void draw() noexcept;

    void setDashboard(Dashboard* dashboard) {
        _dashboard = dashboard;
        _newBlockSelector.setGraphModel(dashboard ? std::addressof(dashboard->graphModel) : nullptr);
        if (dashboard) {
            _remoteSignalSelector = std::make_unique<SignalSelector>(dashboard->graphModel);
        } else {
            _remoteSignalSelector.reset();
        }
        reset();
    }
    void reset();

    auto editorCount() const { return _editors.size(); }
    void pushEditor(std::string name, UiGraphModel& model, UiGraphBlock* rootBlock);
    void popEditor();

    void updateStyle(); // reads from LookAndFeel::instance().style

    static const DataTypeStyle& styleForDataType(std::string_view type);

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    FlowgraphEditor& currentEditor() { return _editors.back(); }
};

} // namespace DigitizerUi
