#pragma once

#include <functional>
#include <span>
#include <vector>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "Flowgraph.hpp"
#include "RemoteSignalSources.hpp"

#include "components/Block.hpp"

namespace DigitizerUi {

class FlowGraph;

class FlowGraphItem {
public:
    FlowGraphItem();
    ~FlowGraphItem();

    void        draw(FlowGraph *fg, const ImVec2 &size);

    std::string settings(FlowGraph *fg) const;
    void        setSettings(FlowGraph *fg, const std::string &settings);
    void        clear();
    void        setStyle(LookAndFeel::Style style);

    //
    std::function<Block *(FlowGraph *)>                                                                newSinkCallback;
    std::function<void(components::BlockControlsPanelContext &, const ImVec2 &, const ImVec2 &, bool)> requestBlockControlsPanel;

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
    components::BlockControlsPanelContext    m_editPaneContext;
    bool                                     m_layoutGraph{ true };
};

} // namespace DigitizerUi
