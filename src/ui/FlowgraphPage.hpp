#pragma once

#include <functional>
#include <span>
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

namespace opendigitizer {
struct ImPlotSinkModel;
}

namespace DigitizerUi {

// Returns the pin positionY relative to the block
float pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight);
void  drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& name, const std::string& type, bool mainFlowGraph = true);

class FlowgraphEditor {
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

    components::BlockControlsPanelContext _editPaneContext;
    ImVec2                                _contextMenuPosition;

    void makeCurrent() { ax::NodeEditor::SetCurrentEditor(_editorPtr); }

    auto defaultEditorConfig() {
        ax::NodeEditor::Config config;
        config.SettingsFile = nullptr;
        config.UserPointer  = this;
        return config;
    }

public:
    std::function<void()> closeRequestedCallback;

    std::function<void(UiGraphModel*)> openNewBlockSelectorCallback;
    std::function<void(UiGraphModel*)> openNewSubGraphSelectorCallback;
    std::function<void(UiGraphModel*)> openAddRemoteSignalCallback;

    struct ExportPortMessageData {
        std::string uniqueBlockName;
        std::string portDirection;
        std::string portName;
        std::string exportedName;
        bool        exportFlag;
    };
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
        makeCurrent();
        ax::NodeEditor::DestroyEditor(_editorPtr);
    }

    void setStyle(LookAndFeel::Style s) {
        makeCurrent();

        auto& style        = ax::NodeEditor::GetStyle();
        style.NodeRounding = 0;
        style.PinRounding  = 0;

        switch (s) {
        case LookAndFeel::Style::Dark:
            style.Colors[ax::NodeEditor::StyleColor_Bg]         = {0.1f, 0.1f, 0.1f, 1.f};
            style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = {0.2f, 0.2f, 0.2f, 1.f};
            style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = {0.7f, 0.7f, 0.7f, 1.f};
            break;
        case LookAndFeel::Style::Light:
            style.Colors[ax::NodeEditor::StyleColor_Bg]         = {1.f, 1.f, 1.f, 1.f};
            style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = {0.94f, 0.92f, 1.f, 1.f};
            style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = {0.38f, 0.38f, 0.38f, 1.f};
            break;
        }
    }

    void draw(const ImVec2& contentTopLeft, const ImVec2& contentSize, bool isCurrentEditor);

    void drawGraph(const ImVec2& size);

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
        _newBlockSelector.setGraphModel(dashboard ? std::addressof(dashboard->graphModel()) : nullptr);
        if (dashboard) {
            _remoteSignalSelector = std::make_unique<SignalSelector>(dashboard->graphModel());
        } else {
            _remoteSignalSelector.reset();
        }
        reset();
    }
    void reset();

    auto editorCount() const { return _editors.size(); }
    void pushEditor(std::string name, UiGraphModel& model, UiGraphBlock* rootBlock);
    void popEditor();

    void setStyle(LookAndFeel::Style style);

    static const DataTypeStyle& styleForDataType(std::string_view type);

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    FlowgraphEditor& currentEditor() { return _editors.back(); }
};

} // namespace DigitizerUi
