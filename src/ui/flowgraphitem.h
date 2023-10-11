#pragma once

#include <algorithm>
#include <forward_list>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <refl.hpp>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <imgui_node_editor.h>

#include "common.h"
#include "flowgraph.h"
#include "imgui.h"
#include "imguiutils.h"
#include "remotesignalsources.h"

namespace DigitizerUi {

class FlowGraph;

class FlowGraphItem {
public:
    FlowGraphItem();
    ~FlowGraphItem();

    void                             draw(FlowGraph *fg, const ImVec2 &size);

    std::function<void(FlowGraph *)> newSinkCallback;

    std::string                      settings(FlowGraph *fg) const;
    void                             setSettings(FlowGraph *fg, const std::string &settings);
    void                             clear();
    void                             setStyle(Style style);

private:
    enum class Alignment {
        Left,
        Right,
    };

    void                              addBlock(const Block &b, std::optional<ImVec2> nodePos = {}, Alignment alignment = Alignment::Left);
    void                              drawNewBlockDialog(FlowGraph *fg);
    void                              drawAddSourceDialog(FlowGraph *fg);

    QueryFilterElementList            querySignalFilters;
    SignalList                        signalList{ querySignalFilters };
    Block                            *m_selectedBlock = nullptr;
    std::vector<Block::Parameter>     m_parameters;
    BlockType                        *m_selectedBlockType = nullptr;
    ImVec2                            m_mouseDrag         = { 0, 0 };
    bool                              m_createNewBlock    = false;
    ImVec2                            m_contextMenuPosition;
    bool                              m_addRemoteSignal             = false;
    bool                              m_addRemoteSignalDialogOpened = false;
    std::string                       m_addRemoteSignalUri;

    opencmw::service::dns::QueryEntry m_queryFilter;

    // new block dialog data
    const Block *m_filterBlock = nullptr;
    struct Context {
        Context();

        ax::NodeEditor::EditorContext *editor = nullptr;
        ax::NodeEditor::Config         config;
        std::string                    settings;
    };
    std::unordered_map<FlowGraph *, Context> m_editors;
    ImGuiUtils::BlockControlsPanel           m_editPane;
};

} // namespace DigitizerUi
