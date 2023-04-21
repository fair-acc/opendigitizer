#pragma once

#include <functional>
#include <span>
#include <vector>

#include <imgui_node_editor.h>

#include "flowgraph.h"
#include "imguiutils.h"

namespace DigitizerUi {

class FlowGraph;

class FlowGraphItem {
public:
    FlowGraphItem(FlowGraph *fg);
    ~FlowGraphItem();

    void                  draw(const ImVec2 &size);
    void                  resetBlocksPosition();

    std::function<void()> newSinkCallback;

    std::string           settings() const;
    void                  setSettings(const std::string &settings);
    void                  clear();

private:
    enum class Alignment {
        Left,
        Right,
    };

    void                          addBlock(const Block &b, std::optional<ImVec2> nodePos = {}, Alignment alignment = Alignment::Left);
    void                          drawNewBlockDialog();
    void                          drawAddSourceDialog();

    FlowGraph                    *m_flowGraph;
    Block                        *m_selectedBlock = nullptr;
    std::vector<Block::Parameter> m_parameters;
    BlockType                    *m_selectedBlockType = nullptr;
    ImVec2                        m_mouseDrag         = { 0, 0 };
    bool                          m_createNewBlock    = false;
    ImVec2                        m_contextMenuPosition;
    ax::NodeEditor::Config        m_config;
    std::string                   m_settings;
    bool                          m_addRemoteSignal             = false;
    bool                          m_addRemoteSignalDialogOpened = false;
    std::string                   m_addRemoteSignalUri;

    // new block dialog data
    const Block *m_filterBlock = nullptr;
};

} // namespace DigitizerUi
