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

    void draw(const ImVec2 &size, std::span<const Block::Port> sources, std::span<const Block::Port> sinks);

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
    BlockType                    *m_selectedBlockType = nullptr;
    ImVec2                        m_mouseDrag         = { 0, 0 };
    bool m_createNewBlock = false;
    ImVec2 m_contextMenuPosition;
};

}
