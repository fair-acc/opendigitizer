#include "flowgraphitem.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include "flowgraph.h"
#include "imguiutils.h"

namespace DigitizerUi
{

FlowGraphItem::FlowGraphItem(FlowGraph *fg)
             : m_flowGraph(fg)
{
    auto ed = ax::NodeEditor::CreateEditor();
    ax::NodeEditor::SetCurrentEditor(ed);

    auto &style = ax::NodeEditor::GetStyle();
    style.NodeRounding = 0;
    style.PinRounding = 0;
    style.Colors[ax::NodeEditor::StyleColor_Bg] = { 1, 1, 1, 1 };
    style.Colors[ax::NodeEditor::StyleColor_NodeBg] = { 0.94, 0.92, 1, 1 };
    style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = { 0.38, 0.38, 0.38, 1 };

    for (const auto &b : m_flowGraph->blocks()) {
        for (const auto &port : b->outputs()) {
            for (auto &conn : port.connections) {
                auto inPort = conn.block->inputs()[conn.portNumber];
                m_links.push_back({ ax::NodeEditor::LinkId(m_linkId++), port.id, inPort.id });
            }
        }
    }
}

static uint32_t colorForDataType(DataType t)
{
    switch (t) {
        case DataType::ComplexFloat64: return 0xff795548;
        case DataType::ComplexFloat32: return 0xff2196F3;
        case DataType::ComplexInt64: return 0xff8BC34A;
        case DataType::ComplexInt32: return 0xff4CAF50;
        case DataType::ComplexInt16: return 0xffFFC107;
        case DataType::ComplexInt8: return 0xff9C27B0;
        case DataType::Float64: return 0xff00BCD4;
        case DataType::Float32: return 0xffF57C00;
        case DataType::Int64: return 0xffCDDC39;
        case DataType::Int32: return 0xff009688;
        case DataType::Int16: return 0xffFFEB3B;
        case DataType::Int8: return 0xffD500F9;
        case DataType::Bits: return 0xffEA80FC;
        case DataType::AsyncMessage: return 0xffDBDBD;
        case DataType::BusConnection: return 0xffffffff;
        case DataType::Wildcard: return 0xffffffff;
        case DataType::Untyped: return 0xffffffff;
    }
    assert(0);
    return 0;
}

static uint32_t darken(uint32_t c)
{
    uint32_t r = c & 0xff000000;
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * i;
        r |= uint32_t(((c >> shift) & 0xff) * 0.5) << shift;
    }
    return r;
}

static void addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, ImVec2 &p, ImVec2 size, float spacing) {
    const bool input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2   min  = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2   max  = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 rmin = ImVec2(min.x, (min.y + max.y) / 2.f);
    const ImVec2 rmax = ImVec2(rmin.x + 1, rmin.y + 1);
    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(rmin, rmax);
    ax::NodeEditor::PinRect(min, max);
    ax::NodeEditor::EndPin();

    p.y += size.y + spacing;
};

static void drawPin(ImDrawList *drawList, ImVec2 rectSize, float spacing, float textMargin,
                    const std::string &name, DataType type) {
    auto p = ImGui::GetCursorScreenPos();
    drawList->AddRectFilled(p, p + rectSize, colorForDataType(type));
    drawList->AddRect(p, p + rectSize, darken(colorForDataType(type)));

    auto y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textMargin);
    ImGui::TextUnformatted(name.c_str());

    ImGui::SetCursorPosY(y + rectSize.y + spacing);
};

static void addBlock(const Block &b)
{
    ax::NodeEditor::BeginNode(b.id);

    const auto padding = ax::NodeEditor::GetStyle().NodePadding;

    ImGui::TextUnformatted(b.name.c_str());

    const auto curPos = ImGui::GetCursorPos();
    const auto leftPos = curPos.x - padding.x;
    const int rectHeight = 14;
    const int rectsSpacing = 5;
    const int textMargin = 2;

    if (!b.type) {
        ImGui::TextUnformatted("Unkown type");
        ax::NodeEditor::EndNode();
    } else {
        // Use a dummy to ensure a minimum sensible size on the nodees
        ImGui::Dummy(ImVec2(80.0f, 45.0f));
        ImGui::SetCursorPos(curPos);

        for (int i = 0; i < b.parameters().size(); ++i) {
            ImGui::Text("%s:", b.type->parameters[i].label.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(b.parameters()[i].toString().c_str());
        }

        ImGui::SetCursorPos(curPos);

        // auto y = curPos.y;
        const auto &inputs = b.inputs();
        auto *inputWidths = static_cast<float *>(alloca(sizeof(float) * inputs.size()));

        ImVec2 pos = { leftPos, curPos.y };
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            inputWidths[i] = ImGui::CalcTextSize(b.type->inputs[i].name.c_str()).x + textMargin * 2;
            addPin(ax::NodeEditor::PinId(&inputs[i]), ax::NodeEditor::PinKind::Input, pos, { inputWidths[i], rectHeight }, rectsSpacing);
        }

        // ImGui::SetCursorPosY(y);
        const auto &outputs = b.outputs();
        auto *outputWidths = static_cast<float *>(alloca(sizeof(float) * outputs.size()));
        auto s = ax::NodeEditor::GetNodeSize(b.id);
        pos = { leftPos + s.x, curPos.y };
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            outputWidths[i] = ImGui::CalcTextSize(b.type->outputs[i].name.c_str()).x + textMargin * 2;
            addPin(ax::NodeEditor::PinId(&outputs[i]), ax::NodeEditor::PinKind::Output, pos, { outputWidths[i], rectHeight }, rectsSpacing);
        }

        ax::NodeEditor::EndNode();

        // The input/output pins are drawn after ending the node because otherwise
        // drawing them would increase the node size, which we need to know to correctly place the
        // output pins, and that would cause the nodes to continuously grow in width

        ImGui::SetCursorPos(curPos);
        auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(b.id);

        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto &in = inputs[i];

            ImGui::SetCursorPosX(leftPos - inputWidths[i]);
            drawPin(drawList, { inputWidths[i], rectHeight }, rectsSpacing, textMargin, b.type->inputs[i].name, in.type);
        }

        ImGui::SetCursorPos(curPos);
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto &out = outputs[i];

            auto s = ax::NodeEditor::GetNodeSize(b.id);
            ImGui::SetCursorPosX(leftPos + s.x);
            drawPin(drawList, { outputWidths[i], rectHeight }, rectsSpacing, textMargin, b.type->outputs[i].name, out.type);
        }
    }
}

void FlowGraphItem::draw(const ImVec2 &size, std::span<const Block::Port> sources, std::span<const Block::Port> sinks) {
    const float left = ImGui::GetCursorPosX() + 10;
    ax::NodeEditor::Begin("My Editor", size);

    int sourceId = 2000;
    int y        = 0;
    for (int i = 0; i < sources.size(); ++i) {
        auto nodeId = sourceId++;
        ax::NodeEditor::BeginNode(nodeId);
        ax::NodeEditor::SetNodeZPosition(nodeId, 1000);
        auto p = ax::NodeEditor::ScreenToCanvas({ left, 0 });
        p.y = y;
        y += 40;

        ax::NodeEditor::SetNodePosition(nodeId, p);
        ImGui::Dummy({ 100, 10 });

        const auto nodeSize = ax::NodeEditor::GetNodeSize(nodeId);

        const ImVec2 size(20, 14);
        ImVec2 pos = p + ImVec2(nodeSize.x, (nodeSize.y - size.y) / 2);
        addPin(ax::NodeEditor::PinId(&sources[i]), ax::NodeEditor::PinKind::Output, pos, size, 0);

        ax::NodeEditor::EndNode();

        ImGui::SetCursorPos(p + ImVec2(nodeSize.x, (nodeSize.y - size.y) / 2));
        drawPin(ax::NodeEditor::GetNodeBackgroundDrawList(nodeId), size, 0, 0, "out", DataType::Wildcard);

        ImGui::SetCursorPos(p + ImVec2(10, 10));
        ImGui::Text("source %d\n", i);
    }

    int sinkId = 4000;
    y          = 0;
    for (int i = 0; i < sinks.size(); ++i) {
        auto nodeId = sinkId++;
        ax::NodeEditor::BeginNode(nodeId);
        ax::NodeEditor::SetNodeZPosition(nodeId, 1000);
        auto p = ax::NodeEditor::ScreenToCanvas({ left + size.x, 0 });
        p.x -= 140;
        p.y = y;
        y += 40;

        ax::NodeEditor::SetNodePosition(nodeId, p);
        ImGui::Dummy({ 100, 10 });

        const auto nodeSize = ax::NodeEditor::GetNodeSize(nodeId);

        const ImVec2 size(20, 14);
        ImVec2 pos = p + ImVec2(0, (nodeSize.y - size.y) / 2);
        addPin(ax::NodeEditor::PinId(&sinks[i]), ax::NodeEditor::PinKind::Input, pos, size, 0);

        ax::NodeEditor::EndNode();

        ImGui::SetCursorPos(p + ImVec2(-size.x, (nodeSize.y - size.y) / 2));
        drawPin(ax::NodeEditor::GetNodeBackgroundDrawList(nodeId), size, 0, 0, "in", DataType::Wildcard);

        ImGui::SetCursorPos(p + ImVec2(10, 10));
        ImGui::Text("sink %d\n", i);
    }

    if (m_createNewBlock) {
        auto b = std::make_unique<Block>("new block", m_selectedBlockType);
        ax::NodeEditor::SetNodePosition(b->id, m_contextMenuPosition);
        m_flowGraph->addBlock(std::move(b));
        m_createNewBlock = false;
    }

    for (const auto &b : m_flowGraph->blocks()) {
        addBlock(*b);
    }

    for (auto &linkInfo : m_links) {
        ax::NodeEditor::Link(linkInfo.Id, linkInfo.InputId, linkInfo.OutputId, { 0, 0, 0, 1 });
    }

    // Handle creation action, returns true if editor want to create new object (node or link)
    if (ax::NodeEditor::BeginCreate({  0, 0, 0, 1 })) {
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
                auto inputPort  = inputPinId.AsPointer<Block::Port>();
                auto outputPort = outputPinId.AsPointer<Block::Port>();

                if (inputPort->kind == outputPort->kind) {
                    ax::NodeEditor::RejectNewItem();
                } else {
                    bool compatibleTypes = inputPort->type == outputPort->type || inputPort->type == DataType::Wildcard || outputPort->type == DataType::Wildcard;
                    if (!compatibleTypes) {
                        ax::NodeEditor::RejectNewItem();
                    } else if (ax::NodeEditor::AcceptNewItem()) {
                        // ed::AcceptNewItem() return true when user release mouse button.
                        // Since we accepted new link, lets add one to our list of links.
                        m_links.push_back({ ax::NodeEditor::LinkId(m_linkId++), inputPinId, outputPinId });

                        // Draw new link.
                        ax::NodeEditor::Link(m_links.back().Id, m_links.back().InputId, m_links.back().OutputId, { 0, 0, 0, 1 });
                    }
                }
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
        ImGui::TextUnformatted(m_editingBlock->type ? m_editingBlock->type->name.c_str() : "Unknown type");
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
        m_contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
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
                m_createNewBlock = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}
