#pragma once

#ifndef IMPLOT_POINT_CLASS_EXTRA
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include <span>
#include <vector>

#include <imgui_node_editor.h>

#include "common.hpp"
#include "flowgraph.hpp"
#include "imguiutils.hpp"
#include "remotesignalsources.hpp"

namespace DigitizerUi {

class FlowGraph;

class FlowGraphItem {
public:
    FlowGraphItem();
    ~FlowGraphItem();

    void                                draw(FlowGraph *fg, const ImVec2 &size);

    std::function<Block *(FlowGraph *)> newSinkCallback;

    std::string                         settings(FlowGraph *fg) const;
    void                                setSettings(FlowGraph *fg, const std::string &settings);
    void                                clear();
    void                                setStyle(Style style);

private:
    enum class Alignment {
        Left,
        Right,
    };

    void                              addBlock(const Block &b, std::optional<ImVec2> nodePos = {}, Alignment alignment = Alignment::Left);
    void                              drawNewBlockDialog(FlowGraph *fg);
    void                              drawAddSourceDialog(FlowGraph *fg);
    void                              sortNodes(FlowGraph *fg, const std::vector<const Block *> &blocks);
    void                              arrangeUnconnectedNodes(FlowGraph *fg, const std::vector<const Block *> &blocks);

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
    std::vector<const Block *>        m_nodesToArrange;

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
    bool                                     m_layoutGraph{ true };
};

} // namespace DigitizerUi
