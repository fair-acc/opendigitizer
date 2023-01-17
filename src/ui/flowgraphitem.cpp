#include "flowgraphitem.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include "flowgraph.h"

namespace DigitizerUi
{

namespace {

void ImGuiEx_BeginColumn() {
    ImGui::BeginGroup();
}

void ImGuiEx_NextColumn() {
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
}

void ImGuiEx_EndColumn() {
    ImGui::EndGroup();
}

}

FlowGraphItem::FlowGraphItem(FlowGraph *fg)
             : m_flowGraph(fg)
{
    auto ed = ax::NodeEditor::CreateEditor();
    ax::NodeEditor::SetCurrentEditor(ed);

    for (const auto &b : m_flowGraph->blocks()) {
        for (const auto &port : b->outputs()) {
            for (auto &conn : port.connections) {
                auto inPort = conn.block->inputs()[conn.portNumber];
                m_links.push_back({ ax::NodeEditor::LinkId(m_linkId++), port.id, inPort.id });
            }
        }
    }
}

void FlowGraphItem::draw(const ImVec2 &size, std::span<const ImVec2> sources, std::span<const ImVec2> sinks)
{
    const float left = ImGui::GetCursorPosX() + 10;
    ax::NodeEditor::Begin("My Editor", size);

    int sourceId = 2000;
    int y        = 0;
    for (int i = 0; i < sources.size(); ++i) {
        const auto &s = sources[i];
        ax::NodeEditor::BeginNode(sourceId);
        auto p = ax::NodeEditor::ScreenToCanvas({ left, 0 });
        p.y    = y;

        ax::NodeEditor::SetNodePosition(sourceId++, p);
        ImGui::Dummy({ 100, 10 });

        int y2 = y + 40;
        ax::NodeEditor::BeginPin(sourceId++, ax::NodeEditor::PinKind::Output);
        auto p1 = p;
        p1.x += 100;
        auto p2 = p;
        p2.x += 120;
        p2.y = y2;
        ax::NodeEditor::PinRect(p1, p2);
        y = y2;

        ax::NodeEditor::EndPin();
        ax::NodeEditor::EndNode();

        p.y += 10;
        p.x += 10;
        ImGui::SetCursorPos(p);
        ImGui::TextColored({ 1, 1, 1, 1 }, "source %d        ->\n", i);
    }

    int sinkId = 4000;
    y          = 0;
    for (int i = 0; i < sinks.size(); ++i) {
        const auto &s = sinks[i];
        ax::NodeEditor::BeginNode(sinkId);
        auto p = ax::NodeEditor::ScreenToCanvas({ left + size.x, 0 });
        p.x -= 140;
        p.y = y;

        ax::NodeEditor::SetNodePosition(sinkId++, p);
        ImGui::Dummy({ 100, 10 });

        int y2 = y + 40;
        ax::NodeEditor::BeginPin(sinkId++, ax::NodeEditor::PinKind::Input);
        auto p1 = p;
        // p1.x += 100;
        auto p2 = p;
        p2.x += 20;
        p2.y = y2;
        ax::NodeEditor::PinRect(p1, p2);
        y = y2;

        ax::NodeEditor::EndPin();
        ax::NodeEditor::EndNode();

        p.y += 10;
        p.x -= 10;
        ImGui::SetCursorPos(p);
        ImGui::TextColored({ 1, 1, 1, 1 }, "<-       sink %d\n", i);
    }

    for (const auto &b : m_flowGraph->blocks()) {
        ax::NodeEditor::BeginNode(b->id);
        ImGui::TextUnformatted(b->name.c_str());

        if (!b->type) {
            ImGui::TextUnformatted("Unkown type");
        } else {
            ImGuiEx_BeginColumn();
            const auto &inputs = b->inputs();
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                ax::NodeEditor::BeginPin(b->inputs()[i].id, ax::NodeEditor::PinKind::Input);
                ImGui::Text("-> In1 (%s)", inputs[i].type.c_str());

                const auto   min  = ImGui::GetItemRectMin();
                const auto   max  = ImGui::GetItemRectMax();
                const ImVec2 rmin = ImVec2(min.x, (min.y + max.y) / 2.f);
                const ImVec2 rmax = ImVec2(rmin.x + 1, rmin.y + 1);
                ax::NodeEditor::PinPivotRect(rmin, rmax);
                ax::NodeEditor::PinRect(min, max);
                ax::NodeEditor::EndPin();
            }
            ImGuiEx_NextColumn();

            int         x       = ImGui::GetCursorPosX();
            const auto &outputs = b->outputs();
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                ImGui::SetCursorPosX(x + 100);
                ax::NodeEditor::BeginPin(b->outputs()[i].id, ax::NodeEditor::PinKind::Output);
                ImGui::Text("Out (%s) ->", outputs[i].type.c_str());

                const auto   min  = ImGui::GetItemRectMin();
                const auto   max  = ImGui::GetItemRectMax();
                const ImVec2 rmin = ImVec2(max.x, (min.y + max.y) / 2.f);
                const ImVec2 rmax = ImVec2(rmin.x + 1, rmin.y + 1);
                ax::NodeEditor::PinPivotRect(rmin, rmax);
                ax::NodeEditor::PinRect(min, max);

                ax::NodeEditor::EndPin();
            }
            ImGuiEx_EndColumn();
        }

        ImGui::Dummy(ImVec2(80.0f, 45.0f));

        ax::NodeEditor::EndNode();
    }

    for (auto &linkInfo : m_links) {
        ax::NodeEditor::Link(linkInfo.Id, linkInfo.InputId, linkInfo.OutputId);
    }

    // Handle creation action, returns true if editor want to create new object (node or link)
    if (ax::NodeEditor::BeginCreate()) {
        ax::NodeEditor::PinId inputPinId, outputPinId;
        if (ax::NodeEditor::QueryNewLink(&inputPinId, &outputPinId)) {
            // QueryNewLink returns true if editor want to create new link between pins.
            //
            // Link can be created only for two valid pins, it is up to you to
            // validate if connection make sense. Editor is happy to make any.
            //
            // Link always goes from input to output. User may choose to drag
            // link from output pin or input pin. This determine which pin ids
            // are valid and which are not:
            //   * input valid, output invalid - user started to drag new ling from input pin
            //   * input invalid, output valid - user started to drag new ling from output pin
            //   * input valid, output valid   - user dragged link over other pin, can be validated

            if (inputPinId && outputPinId) // both are valid, let's accept link
            {
                // ed::AcceptNewItem() return true when user release mouse button.
                if (ax::NodeEditor::AcceptNewItem()) {
                    // Since we accepted new link, lets add one to our list of links.
                    m_links.push_back({ ax::NodeEditor::LinkId(m_linkId++), inputPinId, outputPinId });

                    // Draw new link.
                    ax::NodeEditor::Link(m_links.back().Id, m_links.back().InputId, m_links.back().OutputId);
                }

                // You may choose to reject connection between these nodes
                // by calling ed::RejectNewItem(). This will allow editor to give
                // visual feedback by changing link thickness and color.
            }
        }
    }
    ax::NodeEditor::EndCreate(); // Wraps up object creation action handling.

    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();
    ax::NodeEditor::End();

    if (ImGui::IsMouseDoubleClicked(ImGuiPopupFlags_MouseButtonLeft)) {
        auto n     = ax::NodeEditor::GetDoubleClickedNode();
        auto block = m_flowGraph->findBlock(n.Get());
        if (block && block->type) {
            ImGui::OpenPopup("Block parameters");
            m_editingBlock = block;
            m_parameters.clear();
            for (auto &p : block->parameters()) {
                m_parameters.push_back(p);
            }
        }
    }

    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Block parameters")) {
        auto contentRegion = ImGui::GetContentRegionAvail();
        int w = contentRegion.x / 2;
        for (int i = 0; i < m_editingBlock->type->parameters.size(); ++i) {
            const auto &p = m_editingBlock->type->parameters[i];

            // Split the window in half and put the labels on the left and the widgets on the right
            ImGui::Text("%s", p.label.c_str());
            ImGui::SameLine(w, 0);
            ImGui::SetNextItemWidth(w);

            char label[64];
            snprintf(label, sizeof(label), "##parameter_%d", i);

            if (auto *e = std::get_if<BlockType::EnumParameter>(&p.impl)) {
                auto &value = std::get<Block::EnumParameter>(m_parameters[i]);

                if (ImGui::BeginCombo(label, value.toString().c_str())) {
                    for (int i = 0; i < e->options.size(); ++i) {
                        auto &opt      = e->options[i];

                        bool  selected = value.optionIndex == i;
                        if (ImGui::Selectable(opt.c_str(), selected)) {
                            value.optionIndex = i;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (auto *ip = std::get_if<Block::IntParameter>(&m_parameters[i])) {
                ImGui::InputInt(label, &ip->value);
            } else if (auto *rp = std::get_if<Block::RawParameter>(&m_parameters[i])) {
                rp->value.reserve(256);
                ImGui::InputText(label, rp->value.data(), rp->value.capacity());
            }
        }

        ImGui::SetCursorPosY(contentRegion.y);
        if (ImGui::Button("Ok")) {
            ImGui::CloseCurrentPopup();
            for (int i = 0; i < m_parameters.size(); ++i) {
                m_editingBlock->setParameter(i, m_parameters[i]);
            }
            m_editingBlock->update();
            m_editingBlock = nullptr;
        }

        ImGui::EndPopup();
    }

    bool openNewBlockDialog = false;
    if (backgroundClicked == ImGuiMouseButton_Right && std::abs(m_mouseDrag.x) < 10 && std::abs(m_mouseDrag.y) < 10) {
        ImGui::OpenPopup("ctx_menu");
    }
    m_mouseDrag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);

    if (ImGui::BeginPopup("ctx_menu")) {
        if (ImGui::MenuItem("New block")) {
            openNewBlockDialog = true;
        }
        ImGui::EndPopup();
    }

    if (openNewBlockDialog) {
        ImGui::OpenPopup("New block");
    }

    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("New block")) {
        if (ImGui::BeginListBox("##Available Block types", { 200, 200 })) {
            for (auto &t : m_flowGraph->blockTypes()) {
                if (ImGui::Selectable(t.first.c_str(), t.second.get() == m_selectedBlockType)) {
                    m_selectedBlockType = t.second.get();
                }
            }
            ImGui::EndListBox();
        }

        if (ImGui::Button("Ok")) {
            if (m_selectedBlockType) {
                m_flowGraph->addBlock(std::make_unique<Block>("new block", m_selectedBlockType));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}
