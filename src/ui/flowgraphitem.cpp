#include "flowgraphitem.h"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <misc/cpp/imgui_stdlib.h>

#include "flowgraph.h"
#include "imguiutils.h"

namespace DigitizerUi {

FlowGraphItem::FlowGraphItem(FlowGraph *fg)
    : m_flowGraph(fg) {
#ifdef EMSCRIPTEN
    // In emscripten we don't have access to the filesystem so we can't read the settings file
    m_config.SettingsFile = nullptr;
#endif

    auto ed = ax::NodeEditor::CreateEditor(&m_config);
    ax::NodeEditor::SetCurrentEditor(ed);

    auto &style                                         = ax::NodeEditor::GetStyle();
    style.NodeRounding                                  = 0;
    style.PinRounding                                   = 0;
    style.Colors[ax::NodeEditor::StyleColor_Bg]         = { 1, 1, 1, 1 };
    style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = { 0.94, 0.92, 1, 1 };
    style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = { 0.38, 0.38, 0.38, 1 };
}

static uint32_t colorForDataType(DataType t) {
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

static uint32_t darken(uint32_t c) {
    uint32_t r = c & 0xff000000;
    for (int i = 0; i < 3; ++i) {
        int shift = 8 * i;
        r |= uint32_t(((c >> shift) & 0xff) * 0.5) << shift;
    }
    return r;
}

static void addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, const ImVec2 &p, ImVec2 size) {
    const bool   input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2 min   = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2 max   = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 rmin  = ImVec2(input ? min.x : max.x, (min.y + max.y) / 2.f);
    const ImVec2 rmax  = ImVec2(rmin.x + 1, rmin.y + 1);

    if (input) {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 10);
    }

    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(rmin, rmax);
    ax::NodeEditor::PinRect(min, max);
    ax::NodeEditor::EndPin();

    if (input) {
        ax::NodeEditor::PopStyleVar(2);
    }
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

template<auto GetPorts, int ConnectionEnd>
static bool blockInTreeHelper(const Block *block, const Block *start) {
    if (block == start) {
        return true;
    }

    for (const auto &port : GetPorts(start)) {
        for (auto *c : port.connections) {
            if (blockInTreeHelper<GetPorts, ConnectionEnd>(block, c->ports[ConnectionEnd]->block)) {
                return true;
            }
        }
    }
    return false;
}

static bool blockInTree(const Block *block, const Block *start) {
    constexpr auto inputs  = [](const Block *b) { return b->inputs(); };
    constexpr auto outputs = [](const Block *b) { return b->outputs(); };
    return blockInTreeHelper<inputs, 0>(block, start) || blockInTreeHelper<outputs, 1>(block, start);
}

void FlowGraphItem::addBlock(const Block &b, std::optional<ImVec2> nodePos, Alignment alignment) {
    auto       nodeId      = ax::NodeEditor::NodeId(&b);
    const auto padding     = ax::NodeEditor::GetStyle().NodePadding;

    const bool filteredOut = [&]() {
        if (m_filterBlock && !blockInTree(&b, m_filterBlock)) {
            return true;
        }
        return false;
    }();

    if (filteredOut) {
        ImGui::BeginDisabled();
    }

    if (nodePos) {
        ax::NodeEditor::SetNodeZPosition(nodeId, 1000);

        auto p = nodePos.value();
        if (alignment == Alignment::Right) {
            float width = 80;
            if (b.type) {
                for (int i = 0; i < b.parameters().size(); ++i) {
                    float w = ImGui::CalcTextSize("%s: %s", b.type->parameters[i].label.c_str(), b.parameters()[i].toString().c_str()).x;
                    width   = std::max(width, w);
                }
                width += padding.x + padding.z;
            }
            p.x -= width;
        }

        ax::NodeEditor::SetNodePosition(nodeId, p);
    }
    ax::NodeEditor::BeginNode(nodeId);

    ImGui::TextUnformatted(b.name.c_str());

    auto      curPos       = ImGui::GetCursorPos();
    auto      leftPos      = curPos.x - padding.x;
    const int rectHeight   = 14;
    const int rectsSpacing = 5;
    const int textMargin   = 2;

    if (!b.type) {
        ImGui::TextUnformatted("Unkown type");
        ax::NodeEditor::EndNode();
    } else {
        // Use a dummy to ensure a minimum sensible size on the nodees
        ImGui::Dummy(ImVec2(80.0f, 45.0f));
        ImGui::SetCursorPos(curPos);

        for (int i = 0; i < b.parameters().size(); ++i) {
            ImGui::Text("%s: %s", b.type->parameters[i].label.c_str(), b.parameters()[i].toString().c_str());
        }

        ImGui::SetCursorPos(curPos);

        // auto y = curPos.y;
        const auto &inputs      = b.inputs();
        auto       *inputWidths = static_cast<float *>(alloca(sizeof(float) * inputs.size()));

        ImVec2      pos         = { leftPos, curPos.y };
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            inputWidths[i] = ImGui::CalcTextSize(b.type->inputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&inputs[i]), ax::NodeEditor::PinKind::Input, pos, { inputWidths[i], rectHeight });
            }
            pos.y += rectHeight + rectsSpacing;
        }

        // make sure the node ends up being tall enough to fit all the pins
        ImGui::SetCursorPos(curPos);
        ImGui::Dummy(ImVec2(10, pos.y - curPos.y));

        // ImGui::SetCursorPosY(y);
        const auto &outputs      = b.outputs();
        auto       *outputWidths = static_cast<float *>(alloca(sizeof(float) * outputs.size()));
        auto        s            = ax::NodeEditor::GetNodeSize(nodeId);
        pos                      = { leftPos + s.x, curPos.y };
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            outputWidths[i] = ImGui::CalcTextSize(b.type->outputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&outputs[i]), ax::NodeEditor::PinKind::Output, pos, { outputWidths[i], rectHeight });
            }
            pos.y += rectHeight + rectsSpacing;
        }

        // likewise for the output pins
        ImGui::SetCursorPos(curPos);
        ImGui::Dummy(ImVec2(10, pos.y - curPos.y));

        ax::NodeEditor::EndNode();

        // The input/output pins are drawn after ending the node because otherwise
        // drawing them would increase the node size, which we need to know to correctly place the
        // output pins, and that would cause the nodes to continuously grow in width

        ImGui::SetCursorPos(curPos);
        auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(nodeId);

        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto &in = inputs[i];

            ImGui::SetCursorPosX(leftPos - inputWidths[i]);
            drawPin(drawList, { inputWidths[i], rectHeight }, rectsSpacing, textMargin, b.type->inputs[i].name, in.type);
        }

        ImGui::SetCursorPos(curPos);
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto &out = outputs[i];

            auto        s   = ax::NodeEditor::GetNodeSize(nodeId);
            ImGui::SetCursorPosX(leftPos + s.x);
            drawPin(drawList, { outputWidths[i], rectHeight }, rectsSpacing, textMargin, b.type->outputs[i].name, out.type);
        }
    }

    if (filteredOut) {
        ImGui::EndDisabled();
    }

    ImGui::SetCursorPos(curPos);
    const auto size = ax::NodeEditor::GetNodeSize(nodeId);
    ImGui::SetCursorPosY(ax::NodeEditor::GetNodePosition(nodeId).y + size.y - padding.w - 20);

    ImGui::PushID(b.name.c_str());
    if (ImGui::RadioButton("Filter", m_filterBlock == &b)) {
        if (m_filterBlock == &b) {
            m_filterBlock = nullptr;
        } else {
            m_filterBlock = &b;
        }
    }
    ImGui::PopID();
}

void FlowGraphItem::draw(const ImVec2 &size) {
    const float left = ImGui::GetCursorPosX();
    const float top  = ImGui::GetCursorPosY();
    ax::NodeEditor::Begin("My Editor", size);

    int y = 0;

    for (auto &s : m_flowGraph->sourceBlocks()) {
        auto p = ax::NodeEditor::ScreenToCanvas({ left + 10, 0 });
        p.y    = y;

        addBlock(*s, p);
        y += ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(s.get())).y + 10;
    }

    y = 0;
    for (auto &s : m_flowGraph->sinkBlocks()) {
        auto p = ax::NodeEditor::ScreenToCanvas({ left + size.x - 10, 0 });
        p.y    = y;

        addBlock(*s, p, Alignment::Right);
        y += ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(s.get())).y + 10;
    }

    if (m_createNewBlock) {
        auto b = m_selectedBlockType->createBlock(m_selectedBlockType->name);
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(b.get()), m_contextMenuPosition);
        m_flowGraph->addBlock(std::move(b));
        m_createNewBlock = false;
    }

    for (const auto &b : m_flowGraph->blocks()) {
        addBlock(*b);
    }

    for (auto &c : m_flowGraph->connections()) {
        ax::NodeEditor::Link(ax::NodeEditor::LinkId(&c), ax::NodeEditor::PinId(c.ports[0]), ax::NodeEditor::PinId(c.ports[1]),
                { 0, 0, 0, 1 });
    }

    // Handle creation action, returns true if editor want to create new object (node or link)
    if (ax::NodeEditor::BeginCreate({ 0, 0, 0, 1 })) {
        ax::NodeEditor::PinId inputPinId, outputPinId;
        if (ax::NodeEditor::QueryNewLink(&outputPinId, &inputPinId)) {
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
                    } else if (inputPort->connections.empty() && ax::NodeEditor::AcceptNewItem()) {
                        // AcceptNewItem() return true when user release mouse button.
                        m_flowGraph->connect(inputPort, outputPort);
                    }
                }
            }
        }
    }
    ax::NodeEditor::EndCreate(); // Wraps up object creation action handling.

    if (ax::NodeEditor::BeginDelete()) {
        ax::NodeEditor::NodeId nodeId;
        ax::NodeEditor::LinkId linkId;
        ax::NodeEditor::PinId  pinId1, pinId2;
        if (ax::NodeEditor::QueryDeletedNode(&nodeId)) {
            ax::NodeEditor::AcceptDeletedItem(true);
            auto *b = nodeId.AsPointer<Block>();
            m_flowGraph->deleteBlock(b);
            if (m_filterBlock == b) {
                m_filterBlock = nullptr;
            }
        } else if (ax::NodeEditor::QueryDeletedLink(&linkId, &pinId1, &pinId2)) {
            ax::NodeEditor::AcceptDeletedItem(true);
            auto *c = linkId.AsPointer<Connection>();
            m_flowGraph->disconnect(c);
        }
    }
    ax::NodeEditor::EndDelete();

    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();
    ax::NodeEditor::End();

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        auto n     = ax::NodeEditor::GetDoubleClickedNode();
        auto block = n.AsPointer<Block>();
        if (block && block->type) {
            ImGui::OpenPopup("Block parameters");
            m_selectedBlock = block;
            m_parameters.clear();
            for (auto &p : block->parameters()) {
                m_parameters.push_back(p);
            }
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<Block>();
        if (block) {
            ImGui::OpenPopup("block_ctx_menu");
            m_selectedBlock = block;
        }
    }

    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("Block parameters")) {
        auto contentRegion = ImGui::GetContentRegionAvail();
        int  w             = contentRegion.x / 2;
        ImGui::TextUnformatted(m_selectedBlock->type ? m_selectedBlock->type->name.c_str() : "Unknown type");
        for (int i = 0; i < m_selectedBlock->type->parameters.size(); ++i) {
            const auto &p = m_selectedBlock->type->parameters[i];

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
                m_selectedBlock->setParameter(i, m_parameters[i]);
            }
            m_selectedBlock->update();
            m_selectedBlock = nullptr;
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

    if (ImGui::BeginPopup("block_ctx_menu")) {
        if (ImGui::MenuItem("Delete")) {
            m_flowGraph->deleteBlock(m_selectedBlock);
        }
        ImGui::EndPopup();
    }

    if (openNewBlockDialog) {
        ImGui::OpenPopup("New block");
    }

    ImGui::SetCursorPosX(left + size.x - 80);
    ImGui::SetCursorPosY(top + size.y - 20);
    if (ImGui::Button("New sink") && newSinkCallback) {
        newSinkCallback();
    }

    drawNewBlockDialog();
}

static void ensureItemVisible() {
    auto scroll = ImGui::GetScrollY();
    auto min    = ImGui::GetWindowContentRegionMin().y + scroll;
    auto max    = ImGui::GetWindowContentRegionMax().y + scroll;

    auto h      = ImGui::GetItemRectSize().y;
    auto y      = ImGui::GetCursorPosY() - scroll;
    if (y > max) {
        ImGui::SetScrollHereY(1);
    } else if (y - h < min) {
        ImGui::SetScrollHereY(0);
    }
}

void FlowGraphItem::drawNewBlockDialog() {
    auto completeBlockName = [](ImGuiInputTextCallbackData *d) -> int {
        auto *this_ = static_cast<FlowGraphItem *>(d->UserData);
        if (d->EventKey == ImGuiKey_Tab) {
            std::vector<std::string> candidates;
            std::size_t              shortest = -1;
            for (auto &t : this_->m_flowGraph->blockTypes()) {
                if (t.first.starts_with(std::string_view(d->Buf, d->BufTextLen))) {
                    candidates.push_back(t.first);
                    shortest = std::min(shortest, t.first.size());
                }
            }

            if (candidates.empty()) {
                return 0;
            }

            for (auto &c : candidates) {
                c.resize(shortest);
            }

            while (candidates.size() > 1) {
                auto s = candidates.size();
                if (candidates[s - 2] == candidates[s - 1]) {
                    candidates.pop_back();
                    continue;
                }

                shortest--;
                assert(shortest > 0);
                for (auto &c : candidates) {
                    c.resize(shortest);
                }
            }
            auto *str = candidates.front().c_str();
            d->InsertChars(d->BufTextLen, &str[d->BufTextLen], &str[candidates.front().size()]);
        }
        return 0;
    };

    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("New block")) {
        auto y = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        ImGui::SetCursorPosY(y);

        if (ImGui::IsWindowAppearing() || m_filterInputReclaimFocus) {
            ImGui::SetKeyboardFocusHere();
            m_filterInputReclaimFocus = false;
        }
        bool scrollToSelected = ImGui::InputText("##filterBlockType", &m_typesListFilter, ImGuiInputTextFlags_CallbackCompletion, completeBlockName, this);

        if (ImGui::BeginListBox("##Available Block types", { 200, 200 })) {
            auto filter = [this](const std::string &name) {
                if (!m_typesListFilter.empty()) {
                    auto it = std::search(name.begin(), name.end(), m_typesListFilter.begin(), m_typesListFilter.end(),
                            [](int a, int b) { return std::tolower(a) == std::tolower(b); });
                    if (it == name.end()) {
                        return false;
                    }
                }
                return true;
            };

            if (m_selectedBlockType && !filter(m_selectedBlockType->name)) {
                m_selectedBlockType = nullptr;
            }

            int selectOffset = 0;
            if (m_selectedBlockType) {
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                    ++selectOffset;
                    scrollToSelected = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                    --selectOffset;
                    scrollToSelected = true;
                }
            }

            m_filteredBlockTypes.clear();
            for (auto &t : m_flowGraph->blockTypes()) {
                if (filter(t.first)) {
                    m_filteredBlockTypes.push_back(t.second.get());
                }
            }

            for (auto it = m_filteredBlockTypes.begin(); it != m_filteredBlockTypes.end(); ++it) {
                if (!m_selectedBlockType) {
                    m_selectedBlockType = *it;
                }

                if (selectOffset == -1 && *(it + 1) == m_selectedBlockType) {
                    m_selectedBlockType = *it;
                    selectOffset        = 0;
                } else if (selectOffset == 1 && *it == m_selectedBlockType && (it + 1) != m_filteredBlockTypes.end()) {
                    m_selectedBlockType = nullptr;
                    selectOffset        = 0;
                }

                if (ImGui::Selectable((*it)->name.c_str(), *it == m_selectedBlockType)) {
                    m_selectedBlockType       = *it;
                    m_filterInputReclaimFocus = true;
                }
                if (m_selectedBlockType == *it && scrollToSelected) {
                    ensureItemVisible();
                }
            }
            ImGui::EndListBox();
        }

        ImGui::Separator();

        if (ImGui::Button("Ok") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (m_selectedBlockType) {
                m_createNewBlock = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
} // namespace DigitizerUi
