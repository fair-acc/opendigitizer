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
    UiGraphModel*          _graphModel = nullptr;

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

    FlowgraphEditor(std::string name, UiGraphModel& graphModel) : _editorConfig(defaultEditorConfig()), _editorName(std::move(name)), _graphModel(&graphModel), _editorPtr(ax::NodeEditor::CreateEditor(std::addressof(_editorConfig))) { makeCurrent(); }

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
        bool openRemoteSignalSelector : 1 = false;
        bool rearrangeBlocks : 1          = false;
        bool closeWindow : 1              = false;
    };

    Buttons drawButtons(const ImVec2& contentTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio);

    void sortNodes(bool all);

    void requestBlockDeletion(const std::string& blockName);

    void setFilterBlock(const UiGraphBlock* block) { _filterBlock = block; }

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;
};

class FlowgraphPage {
private:
    friend struct ::TestState;
    enum class Alignment {
        Left,
        Right,
    };

    std::shared_ptr<opencmw::client::RestClient> m_restClient;
    Dashboard*                                   m_dashboard             = nullptr;
    bool                                         m_currentTabIsFlowGraph = false;

    std::deque<FlowgraphEditor> m_editors;

    std::unique_ptr<SignalSelector> m_remoteSignalSelector;
    NewBlockSelector                m_newBlockSelector;

    void drawNodeEditorTab();
    void drawLocalYamlTab();
    void drawRemoteYamlTab(Dashboard::Service& service);

    void pushEditor(std::string name);
    void popEditor();

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
        m_dashboard = dashboard;
        m_newBlockSelector.setGraphModel(dashboard ? std::addressof(dashboard->graphModel()) : nullptr);
        if (dashboard) {
            m_remoteSignalSelector = std::make_unique<SignalSelector>(dashboard->graphModel());
        } else {
            m_remoteSignalSelector.reset();
        }
        reset();
    }
    void reset();

    void setStyle(LookAndFeel::Style style);

    static const DataTypeStyle& styleForDataType(std::string_view type);

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    FlowgraphEditor& currentEditor() { return m_editors.back(); }
};

} // namespace DigitizerUi
