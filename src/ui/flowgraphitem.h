#pragma once

#include <vector>
#include <span>

#include <imgui_node_editor.h>

#include "flowgraph.h"

namespace DigitizerUi
{

class FlowGraph;

class FlowGraphItem
{
public:
    FlowGraphItem(FlowGraph *fg);

    void draw(const ImVec2 &size);

private:
    enum class Alignment {
        Left,
        Right,
    };

    void                          addBlock(const Block &b, std::optional<ImVec2> nodePos = {}, Alignment alignment = Alignment::Left);
    void                          drawNewBlockDialog();

    FlowGraph *m_flowGraph;
    Block                        *m_selectedBlock = nullptr;
    std::vector<Block::Parameter> m_parameters;
    BlockType                    *m_selectedBlockType = nullptr;
    ImVec2                        m_mouseDrag         = { 0, 0 };
    bool m_createNewBlock = false;
    ImVec2 m_contextMenuPosition;
    ax::NodeEditor::Config        m_config;

    // new block dialog data
    const Block                  *m_filterBlock = nullptr;
    std::string                   m_typesListFilter;
    std::vector<BlockType *>      m_filteredBlockTypes;
    bool                          m_filterInputReclaimFocus = false;
};

}
