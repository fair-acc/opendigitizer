#pragma once

#include <functional>
#include <span>
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
class ImPlotSinkModel;
}

namespace DigitizerUi {

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

    const UiGraphBlock* m_filterBlock   = nullptr;
    UiGraphBlock*       m_selectedBlock = nullptr;

    ax::NodeEditor::Config         m_editorConfig;
    ax::NodeEditor::EditorContext* m_editor = nullptr;

    ImVec2 m_contextMenuPosition;

    std::unique_ptr<SignalSelector> m_remoteSignalSelector;
    NewBlockSelector                m_newBlockSelector;

    components::BlockControlsPanelContext m_editPaneContext;

    void sortNodes(bool all);

    void drawNodeEditor(const ImVec2& size);

    static void drawGraph(UiGraphModel& graphModel, const ImVec2& size, const UiGraphBlock*& filterBlock);

    /// Sends a message to the scheduler so specified block is deleted
    void deleteBlock(const std::string& blockName);

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
        m_dashboard     = dashboard;
        m_selectedBlock = nullptr;
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

    static void drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& name, const std::string& type, bool mainFlowGraph = true);

    // Returns the pin positionY relative to the block
    static float pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight);

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;
};

} // namespace DigitizerUi
