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

namespace opendigitizer {
class ImPlotSinkModel;
}

namespace DigitizerUi {

class FlowGraphItem {
private:
    enum class Alignment {
        Left,
        Right,
    };

    UiGraphModel  m_graphModel;
    UiGraphBlock* m_selectedBlock = nullptr;

    ax::NodeEditor::Config         m_editorConfig;
    ax::NodeEditor::EditorContext* m_editor = nullptr;

    bool   m_layoutGraph = true;
    ImVec2 m_contextMenuPosition;

    SignalSelector   m_remoteSignalSelector;
    NewBlockSelector m_newBlockSelector;

    components::BlockControlsPanelContext m_editPaneContext;

    void sortNodes();

    void drawNodeEditor(const ImVec2& size);

public:
    FlowGraphItem();
    ~FlowGraphItem();

    void draw(Dashboard& dashboard) noexcept;

    void setStyle(LookAndFeel::Style style);

    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

    UiGraphModel& graphModel() { return m_graphModel; }
};

} // namespace DigitizerUi
