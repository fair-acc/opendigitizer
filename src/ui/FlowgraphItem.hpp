#pragma once

#include <functional>
#include <span>
#include <vector>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "Flowgraph.hpp"

#include "components/Block.hpp"
#include "components/SignalSelector.hpp"

namespace DigitizerUi {

class FlowGraph;

class FlowGraphItem {
public:
    FlowGraphItem();
    ~FlowGraphItem();

    void draw(FlowGraph* fg, const ImVec2& size);

    std::string settings(FlowGraph* fg) const;
    void        setSettings(FlowGraph* fg, const std::string& settings);
    void        clear();
    void        setStyle(LookAndFeel::Style style);

    // TODO: Block pointer
    std::function<Block*(FlowGraph*)>                                                               newSinkCallback;
    std::function<void(components::BlockControlsPanelContext&, const ImVec2&, const ImVec2&, bool)> requestBlockControlsPanel;

private:
    enum class Alignment {
        Left,
        Right,
    };

    void addBlock(const Block& b, std::optional<ImVec2> nodePos = {}, Alignment alignment = Alignment::Left);
    void drawNewBlockDialog(FlowGraph* fg);
    void sortNodes(FlowGraph* fg);
    void arrangeUnconnectedNodes(FlowGraph* fg, const std::vector<const Block*>& blocks);

    Block*                        m_selectedBlock = nullptr;
    std::vector<Block::Parameter> m_parameters;
    const BlockDefinition*        m_selectedBlockDefinition = nullptr;
    bool                          m_createNewBlock          = false;
    ImVec2                        m_contextMenuPosition;
    std::vector<const Block*>     m_nodesToArrange;

    SignalSelector m_signalSelector;

    // new block dialog data
    // TODO: Block pointer
    const Block* m_filterBlock = nullptr;
    struct Context {
        Context();

        ax::NodeEditor::EditorContext* editor = nullptr;
        ax::NodeEditor::Config         config;
        std::string                    settings;
    };
    std::unordered_map<FlowGraph*, Context> m_editors;
    components::BlockControlsPanelContext   m_editPaneContext;
    bool                                    m_layoutGraph{true};
};

} // namespace DigitizerUi
