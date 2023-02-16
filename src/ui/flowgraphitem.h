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
    FlowGraph *m_flowGraph;
    Block                        *m_selectedBlock = nullptr;
    std::vector<Block::Parameter> m_parameters;
    BlockType                    *m_selectedBlockType = nullptr;
    ImVec2                        m_mouseDrag         = { 0, 0 };
    bool m_createNewBlock = false;
    ImVec2 m_contextMenuPosition;
};

}
