#include "flowgraphitem.h"

#include <algorithm>
#include <crude_json.h>
#include <imgui.h>
#include <imgui_node_editor.h>

#include <fmt/format.h>
#include <misc/cpp/imgui_stdlib.h>

#include "app.h"
#include "flowgraph.h"
#include "imguiutils.h"

#include "flowgraph/datasource.h"

namespace DigitizerUi {

FlowGraphItem::FlowGraphItem() {
}

FlowGraphItem::~FlowGraphItem() = default;

FlowGraphItem::Context::Context() {
    config.SettingsFile = nullptr;
    config.UserPointer  = this;
    config.SaveSettings = [](const char *data, size_t size, ax::NodeEditor::SaveReasonFlags reason, void *userPointer) {
        auto *c     = static_cast<Context *>(userPointer);
        c->settings = std::string(data, size);
        return true;
    };
    config.LoadSettings = [](char *data, void *userPointer) {
        auto *c = static_cast<Context *>(userPointer);
        if (data) {
            memcpy(data, c->settings.data(), c->settings.size());
        }
        return c->settings.size();
    };
}

std::string FlowGraphItem::settings(FlowGraph *fg) const {
    // The nodes in the settings are saved with their NodeId, which is just a pointer
    // to the Blocks. Since that will change between runs save also the name of the blocks.
    auto it = m_editors.find(fg);
    if (it == m_editors.end()) {
        return {};
    }

    auto  json  = crude_json::value::parse(it->second.settings);
    auto &nodes = json["nodes"];
    if (nodes.type() == crude_json::type_t::object) {
        for (auto &n : nodes.get<crude_json::object>()) {
            if (n.first.starts_with("node:")) {
                auto  id         = std::atoll(n.first.data() + 5);
                auto *block      = reinterpret_cast<Block *>(id);
                n.second["name"] = block->name;
            }
        }
    }
    return json.dump();
}

static void setEditorStyle(ax::NodeEditor::EditorContext *ed, Style s) {
    ax::NodeEditor::SetCurrentEditor(ed);
    auto &style        = ax::NodeEditor::GetStyle();
    style.NodeRounding = 0;
    style.PinRounding  = 0;

    switch (s) {
    case Style::Dark:
        style.Colors[ax::NodeEditor::StyleColor_Bg]         = { 0.1, 0.1, 0.1, 1 };
        style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = { 0.2, 0.2, 0.2, 1 };
        style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = { 0.7, 0.7, 0.7, 1 };
        break;
    case Style::Light:
        style.Colors[ax::NodeEditor::StyleColor_Bg]         = { 1, 1, 1, 1 };
        style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = { 0.94, 0.92, 1, 1 };
        style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = { 0.38, 0.38, 0.38, 1 };
        break;
    }
}

void FlowGraphItem::setSettings(FlowGraph *fg, const std::string &settings) {
    auto &c              = m_editors[fg];
    c.config.UserPointer = &c;
    if (c.editor) {
        ax::NodeEditor::DestroyEditor(c.editor);
    }

    auto json = crude_json::value::parse(settings);
    if (json.type() == crude_json::type_t::object) {
        // recover the correct NodeIds using the block names;
        auto &nodes = json["nodes"];
        if (nodes.type() == crude_json::type_t::object) {
            crude_json::value newnodes;
            for (auto &n : nodes.get<crude_json::object>()) {
                auto  name  = n.second["name"].get<std::string>();
                auto *block = fg->findBlock(name);

                if (block) {
                    auto newname      = fmt::format("node:{}", reinterpret_cast<uintptr_t>(block));
                    newnodes[newname] = n.second;
                }
            }
            json["nodes"] = newnodes;
        }
        c.settings = json.dump();
    } else {
        c.settings = {};
    }

    c.editor = ax::NodeEditor::CreateEditor(&c.config);
    ax::NodeEditor::SetCurrentEditor(c.editor);
    setEditorStyle(c.editor, App::instance().style());
}

void FlowGraphItem::setStyle(Style s) {
    for (auto &e : m_editors) {
        setEditorStyle(e.second.editor, s);
    }
}

void FlowGraphItem::clear() {
    m_editors.clear();
}

static uint32_t colorForDataType(DataType t) {
    if (App::instance().style() == Style::Light) {
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
        case DataType::AsyncMessage: return 0xffDBDBDB;
        case DataType::BusConnection: return 0xffffffff;
        case DataType::Wildcard: return 0xffffffff;
        case DataType::Untyped: return 0xffffffff;
        }
    } else {
        switch (t) {
        case DataType::ComplexFloat64: return 0xff86aab8;
        case DataType::ComplexFloat32: return 0xffde690c;
        case DataType::ComplexInt64: return 0xff743cb5;
        case DataType::ComplexInt32: return 0xffb350af;
        case DataType::ComplexInt16: return 0xff003ef8;
        case DataType::ComplexInt8: return 0xff63d84f;
        case DataType::Float64: return 0xffff432b;
        case DataType::Float32: return 0xff0a83ff;
        case DataType::Int64: return 0xff3223c6;
        case DataType::Int32: return 0xffff6977;
        case DataType::Int16: return 0xff0014c4;
        case DataType::Int8: return 0xff2aff06;
        case DataType::Bits: return 0xff158003;
        case DataType::AsyncMessage: return 0xff242424;
        case DataType::BusConnection: return 0xff000000;
        case DataType::Wildcard: return 0xff000000;
        case DataType::Untyped: return 0xff000000;
        }
    }
    assert(0);
    return 0;
}

static uint32_t darkenOrLighten(uint32_t color) {
    if (App::instance().style() == Style::Light) {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int shift = 8 * i;
            r |= uint32_t(((color >> shift) & 0xff) * 0.5) << shift;
        }
        return r;
    } else {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int      shift   = 8 * i;
            uint32_t channel = (color >> shift) & 0xff;
            channel          = 0xff - ((0xff - channel) / 2);
            r |= channel << shift;
        }
        return r;
    }
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
    drawList->AddRect(p, p + rectSize, darkenOrLighten(colorForDataType(type)));

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
        const auto &inputs       = b.inputs();
        auto       *inputWidths  = static_cast<float *>(alloca(sizeof(float) * inputs.size()));

        auto        curScreenPos = ImGui::GetCursorScreenPos();
        ImVec2      pos          = { curScreenPos.x - padding.x, curScreenPos.y };
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            inputWidths[i] = ImGui::CalcTextSize(b.type->inputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&inputs[i]), ax::NodeEditor::PinKind::Input, pos, { inputWidths[i], rectHeight });
            }
            pos.y += rectHeight + rectsSpacing;
        }

        // make sure the node ends up being tall enough to fit all the pins
        ImGui::SetCursorPos(curPos);
        ImGui::Dummy(ImVec2(10, pos.y - curScreenPos.y));

        // ImGui::SetCursorPosY(y);
        const auto &outputs      = b.outputs();
        auto       *outputWidths = static_cast<float *>(alloca(sizeof(float) * outputs.size()));
        auto        s            = ax::NodeEditor::GetNodeSize(nodeId);
        pos                      = { curScreenPos.x - padding.x + s.x, curScreenPos.y };
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            outputWidths[i] = ImGui::CalcTextSize(b.type->outputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&outputs[i]), ax::NodeEditor::PinKind::Output, pos, { outputWidths[i], rectHeight });
            }
            pos.y += rectHeight + rectsSpacing;
        }

        // likewise for the output pins
        ImGui::SetCursorPos(curPos);
        ImGui::Dummy(ImVec2(10, pos.y - curScreenPos.y));

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
    ImGui::SetCursorScreenPos(ax::NodeEditor::GetNodePosition(nodeId) + ImVec2(padding.x, size.y - padding.y - padding.w - 20));

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

void FlowGraphItem::draw(FlowGraph *fg, const ImVec2 &size) {
    auto &c = m_editors[fg];
    if (!c.editor) {
        return;
    }

    c.config.UserPointer = &c;
    ax::NodeEditor::SetCurrentEditor(c.editor);

    const float     left              = ImGui::GetCursorPosX();
    const float     top               = ImGui::GetCursorPosY();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = ImGuiUtils::splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPane.block);

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    ImGui::BeginChild("##canvas", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth),
            false, ImGuiWindowFlags_NoScrollbar);

    ax::NodeEditor::Begin("My Editor", ImGui::GetContentRegionAvail());

    int y = 0;

    for (auto &s : fg->sourceBlocks()) {
        auto p = ax::NodeEditor::ScreenToCanvas({ left + 10, 0 });
        p.y    = y;

        addBlock(*s, p);
        y += ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(s.get())).y + 10;
    }

    y = 0;
    for (auto &s : fg->sinkBlocks()) {
        auto p = ax::NodeEditor::ScreenToCanvas({ ImGui::GetContentRegionMax().x - 10, 0 });
        p.y    = y;

        addBlock(*s, p, Alignment::Right);
        y += ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(s.get())).y + 10;
    }

    if (m_createNewBlock) {
        auto b = m_selectedBlockType->createBlock(m_selectedBlockType->name);
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(b.get()), m_contextMenuPosition);
        fg->addBlock(std::move(b));
        m_createNewBlock = false;
    }

    for (const auto &b : fg->blocks()) {
        addBlock(*b);
    }

    const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
    for (auto &c : fg->connections()) {
        ax::NodeEditor::Link(ax::NodeEditor::LinkId(&c), ax::NodeEditor::PinId(c.ports[0]), ax::NodeEditor::PinId(c.ports[1]),
                linkColor);
    }

    // Handle creation action, returns true if editor want to create new object (node or link)
    if (ax::NodeEditor::BeginCreate(linkColor)) {
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
                        fg->connect(inputPort, outputPort);
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
            fg->deleteBlock(b);
            if (m_filterBlock == b) {
                m_filterBlock = nullptr;
            }
        } else if (ax::NodeEditor::QueryDeletedLink(&linkId, &pinId1, &pinId2)) {
            ax::NodeEditor::AcceptDeletedItem(true);
            auto *c = linkId.AsPointer<Connection>();
            fg->disconnect(c);
        }
    }
    ax::NodeEditor::EndDelete();

    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();
    ax::NodeEditor::End();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<Block>();

        if (!block) {
            m_editPane.block = nullptr;
        } else {
            m_editPane.block     = block;
            m_editPane.closeTime = std::chrono::system_clock::now() + App::instance().editPaneCloseDelay;
        }
    }

    ImGui::EndChild();

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
            } else if (auto *ip = std::get_if<Block::NumberParameter<int>>(&m_parameters[i])) {
                ImGui::InputInt(label, &ip->value);
            } else if (auto *fp = std::get_if<Block::NumberParameter<float>>(&m_parameters[i])) {
                ImGui::InputFloat(label, &fp->value);
            } else if (auto *rp = std::get_if<Block::RawParameter>(&m_parameters[i])) {
                rp->value.reserve(256);
                ImGui::InputText(label, rp->value.data(), rp->value.capacity());
            }
        }

        if (ImGuiUtils::drawDialogButtons() == ImGuiUtils::DialogButton::Ok) {
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
            fg->deleteBlock(m_selectedBlock);
        }
        ImGui::EndPopup();
    }

    if (openNewBlockDialog) {
        ImGui::OpenPopup("New block");
    }

    // Create a new ImGui window for an overlay over the NodeEditor , where we can place our buttons
    // if we don't put the buttons in this overlay, they will be overdrawn by the editor
    ImGui::SetNextWindowPos(ImVec2(0, top + size.y - 30), ImGuiCond_Always);
    ImGui::Begin("Button Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    if (ImGui::Button("Add signal")) {
        ImGui::OpenPopup("addSignalPopup");
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(left + size.x - 80);
    if (ImGui::Button("New sink") && newSinkCallback) {
        newSinkCallback(fg);
    }

    drawAddSourceDialog(fg);
    drawNewBlockDialog(fg);

    ImGui::End(); // overlay

    if (horizontalSplit) {
        const float w = size.x * ratio;
        ImGuiUtils::drawBlockControlsPanel(m_editPane, { left + size.x - w + halfSplitterWidth, top }, { w - halfSplitterWidth, size.y }, true);
    } else {
        const float h = size.y * ratio;
        ImGuiUtils::drawBlockControlsPanel(m_editPane, { left, top + size.y - h + halfSplitterWidth }, { size.x, h - halfSplitterWidth }, false);
    }
}

void FlowGraphItem::drawNewBlockDialog(FlowGraph *fg) {
    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("New block")) {
        auto ret            = ImGuiUtils::filteredListBox("blocks", BlockType::registry().types(), [](auto &it) -> std::pair<BlockType *, std::string> {
            if (it.second->isSource) {
                return {};
            }
            return std::pair{ it.second.get(), it.first };
        });
        m_selectedBlockType = ret ? ret.value().first : nullptr;

        if (ImGuiUtils::drawDialogButtons() == ImGuiUtils::DialogButton::Ok) {
            if (m_selectedBlockType) {
                m_createNewBlock = true;
            }
        }
        ImGui::EndPopup();
    }
}

void FlowGraphItem::drawAddSourceDialog(FlowGraph *fg) {
    ImGui::SetNextWindowSize({ 800, 600 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("addSignalPopup", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        static BlockType *sel = nullptr;
        if (ImGui::BeginChild(1, { 0, ImGui::GetContentRegionAvail().y - 50 })) {
            struct Cat {
                std::string              name;
                std::vector<BlockType *> types;
            };
            static std::vector<Cat> cats;
            cats.clear();
            cats.push_back({ "Remote signals", {} });
            for (const auto &t : BlockType::registry().types()) {
                if (t.second->isSource && !t.second->category.empty()) {
                    auto it = std::find_if(cats.begin(), cats.end(), [&](const auto &c) {
                        return c.name == t.second->category;
                    });
                    if (it == cats.end()) {
                        cats.push_back({ t.second->category, { t.second.get() } });
                    } else {
                        it->types.push_back(t.second.get());
                    }
                }
            }
            cats.push_back({ "Query signals", {} });

            for (const auto &c : cats) {
                const bool isRemote = c.name == "Remote signals";
                if (ImGui::TreeNode(c.name.c_str())) {
                    for (auto *t : c.types) {
                        if (ImGui::Selectable(t->label.c_str(), sel == t, ImGuiSelectableFlags_DontClosePopups)) {
                            sel = t;
                        }
                    }

                    if (c.name == "Query signals") {
                        querySignalFilters.drawFilters();

                        float windowWidth = ImGui::GetWindowWidth();
                        float buttonPosX  = windowWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Add Filter").x;
                        ImGui::SetCursorPosX(buttonPosX);
                        if (ImGui::Button("Add Filter")) {
                            querySignalFilters.emplace_back(QueryFilterElement{ querySignalFilters });
                        }
                        ImGui::Separator();
                        ImGui::SetNextWindowSize(ImGui::GetContentRegionAvail(), ImGuiCond_Once);
                        ImGui::BeginChild("Signals");
                        signalList.drawElements();

                        float refreshButtonPosX = ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Refresh").x;
                        float refreshButtonPosY = ImGui::GetWindowHeight() - ImGui::GetStyle().ItemSpacing.y - ImGui::GetStyle().FramePadding.y - ImGui::CalcTextSize("Refresh").y;
                        ImGui::SetCursorPos({ refreshButtonPosX, refreshButtonPosY });
                        if (ImGui::Button("Refresh")) {
                            signalList.update();
                        }

                        ImGui::EndChild();
                    }

                    if (isRemote) {
                        if (!m_addRemoteSignal) {
                            if (ImGui::Button("Add remote signal")) {
                                m_addRemoteSignal             = true;
                                m_addRemoteSignalDialogOpened = true;
                                m_addRemoteSignalUri          = {};
                            }
                        } else {
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("URI:");
                            ImGui::SameLine();
                            if (m_addRemoteSignalDialogOpened) {
                                ImGui::SetKeyboardFocusHere();
                                m_addRemoteSignalDialogOpened = false;
                            }
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                            ImGui::InputText("##uri", &m_addRemoteSignalUri);

                            if (ImGui::Button("Ok")) {
                                m_addRemoteSignal = false;
                                fg->addRemoteSource(m_addRemoteSignalUri);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                m_addRemoteSignal = false;
                            }
                        }
                    }
                    ImGui::TreePop();
                } else if (isRemote) {
                    m_addRemoteSignal = false;
                }
            }
        }
        ImGui::EndChild();

        if (ImGuiUtils::drawDialogButtons(sel) == ImGuiUtils::DialogButton::Ok) {
            fg->addSourceBlock(sel->createBlock({}));
        }
        ImGui::EndPopup();
    }
}

} // namespace DigitizerUi
