#pragma once

#include <vector>

#include <imgui_node_editor.h>

#include "flowgraph.h"

namespace DigitizerUi
{

class FlowGraph;

class FlowGraphItem
{
public:
    FlowGraphItem(FlowGraph *fg);

    void draw();

private:
    struct LinkInfo {
        ax::NodeEditor::LinkId Id;
        ax::NodeEditor::PinId  InputId;
        ax::NodeEditor::PinId  OutputId;
    };

    FlowGraph *m_flowGraph;
    std::vector<LinkInfo> m_links;
    int m_linkId = 10000;
    Block *m_editingBlock = nullptr;
    std::vector<Block::Parameter> m_parameters;
};

}
