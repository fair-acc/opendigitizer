#include "FlowgraphItem.hpp"

#include <algorithm>

#include <crude_json.h>
#include <fmt/format.h>

#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/Dialog.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/ListBox.hpp"
#include "components/Splitter.hpp"

#include "App.hpp"

using namespace std::string_literals;

namespace DigitizerUi {

inline auto topologicalSort(const std::vector<UiGraphBlock>& blocks, const std::vector<UiGraphEdge>& edges) {
    struct SortLevel {
        std::vector<const UiGraphBlock*> blocks;
    };

    struct BlockConnections {
        std::unordered_set<const UiGraphBlock*> parents;
        std::unordered_set<const UiGraphBlock*> children;
    };

    std::unordered_map<const UiGraphBlock*, BlockConnections> graphConnections;
    std::vector<SortLevel>                                    result;

    for (const auto& block : blocks) {
        graphConnections[std::addressof(block)];
    }

    for (const auto& edge : edges) {
        graphConnections[edge.edgeSourcePort->ownerBlock].children.insert(edge.edgeDestinationPort->ownerBlock);
        graphConnections[edge.edgeDestinationPort->ownerBlock].parents.insert(edge.edgeSourcePort->ownerBlock);
    }

    while (!graphConnections.empty()) {
        SortLevel newLevel;
        for (const auto& [block, connections] : graphConnections) {
            if (connections.parents.empty()) {
                newLevel.blocks.push_back(block);
            }
        }

        for (const auto* block : newLevel.blocks) {
            graphConnections.erase(block);
            for (auto& [_, connections] : graphConnections) {
                connections.parents.erase(block);
                // TODO(NOW) Proper top sort would use this to initialize the next level blocks
            }
        }

        if (newLevel.blocks.empty()) {
            break;
        }

        result.push_back(std::move(newLevel));
    }

    // If there are blocks in graphConnections, we have at lease one cycle,
    // those blocks will not be sorted. Put them in the last level.
    if (!graphConnections.empty()) {
        SortLevel newLevel;
        std::ranges::transform(graphConnections, std::back_inserter(newLevel.blocks), [](const auto& kvp) { return kvp.first; });
    }

    return result;
}

FlowGraphItem::FlowGraphItem() {}

FlowGraphItem::~FlowGraphItem() = default;

FlowGraphItem::Context::Context() {
    config.SettingsFile = nullptr;
    config.UserPointer  = this;
    config.SaveSettings = [](const char* data, size_t size, ax::NodeEditor::SaveReasonFlags /* reason */, void* userPointer) {
        auto* c     = static_cast<Context*>(userPointer);
        c->settings = std::string(data, size);
        return true;
    };
    config.LoadSettings = [](char* data, void* userPointer) {
        auto* c = static_cast<Context*>(userPointer);
        if (data) {
            memcpy(data, c->settings.data(), c->settings.size());
        }
        return c->settings.size();
    };
}

std::string FlowGraphItem::settings(FlowGraph* fg) const {
    // The nodes in the settings are saved with their NodeId, which is just a pointer
    // to the Blocks. Since that will change between runs save also the name of the blocks.
    auto it = m_editors.find(fg);
    if (it == m_editors.end()) {
        return {};
    }

    auto  json  = crude_json::value::parse(it->second.settings);
    auto& nodes = json["nodes"];
    if (nodes.type() == crude_json::type_t::object) {
        for (auto& n : nodes.get<crude_json::object>()) {
            if (n.first.starts_with("node:")) {
                auto  id         = std::atoll(n.first.data() + 5);
                auto* block      = reinterpret_cast<Block*>(id);
                n.second["name"] = block->name;
            }
        }
    }
    return json.dump();
}

static void setEditorStyle(ax::NodeEditor::EditorContext* ed, LookAndFeel::Style s) {
    ax::NodeEditor::SetCurrentEditor(ed);
    auto& style        = ax::NodeEditor::GetStyle();
    style.NodeRounding = 0;
    style.PinRounding  = 0;

    switch (s) {
    case LookAndFeel::Style::Dark:
        style.Colors[ax::NodeEditor::StyleColor_Bg]         = {0.1f, 0.1f, 0.1f, 1.f};
        style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = {0.2f, 0.2f, 0.2f, 1.f};
        style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = {0.7f, 0.7f, 0.7f, 1.f};
        break;
    case LookAndFeel::Style::Light:
        style.Colors[ax::NodeEditor::StyleColor_Bg]         = {1.f, 1.f, 1.f, 1.f};
        style.Colors[ax::NodeEditor::StyleColor_NodeBg]     = {0.94f, 0.92f, 1.f, 1.f};
        style.Colors[ax::NodeEditor::StyleColor_NodeBorder] = {0.38f, 0.38f, 0.38f, 1.f};
        break;
    }
}

void FlowGraphItem::setSettings(FlowGraph* fg, const std::string& settings) {
    auto& c              = m_editors[fg];
    c.config.UserPointer = &c;
    if (c.editor) {
        ax::NodeEditor::DestroyEditor(c.editor);
    }

    auto json = crude_json::value::parse(settings);
    if (json.type() == crude_json::type_t::object) {
        // recover the correct NodeIds using the block names;
        auto& nodes = json["nodes"];
        if (nodes.type() == crude_json::type_t::object) {
            crude_json::value newnodes;
            for (auto& n : nodes.get<crude_json::object>()) {
                auto  name  = n.second["name"].get<std::string>();
                auto* block = fg->findBlock(name);

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
    setEditorStyle(c.editor, LookAndFeel::instance().style);

    m_layoutGraph = true;
}

void FlowGraphItem::setStyle(LookAndFeel::Style s) {
    for (auto& e : m_editors) {
        setEditorStyle(e.second.editor, s);
    }
}

void FlowGraphItem::clear() { m_editors.clear(); }

static uint32_t colorForDataType(DataType t) {
    if (LookAndFeel::instance().style == LookAndFeel::Style::Light) {
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
        case DataType::DataSetFloat64: return 0xff00BCD4;
        case DataType::DataSetFloat32: return 0xffF57C00;
        case DataType::UInt64: return 0xffCDDC39; // TODO: cross check which colours to use
        case DataType::UInt32: return 0xff009688; // TODO: cross check which colours to use
        case DataType::UInt16: return 0xffFFEB3B; // TODO: cross check which colours to use
        case DataType::UInt8: return 0xffD500F9;  // TODO: cross check which colours to use
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
        case DataType::DataSetFloat64: return 0xffff432b;
        case DataType::DataSetFloat32: return 0xff0a83ff;
        case DataType::UInt64: return 0xffCDDC39; // TODO: cross check which colours to use
        case DataType::UInt32: return 0xff009688; // TODO: cross check which colours to use
        case DataType::UInt16: return 0xffFFEB3B; // TODO: cross check which colours to use
        case DataType::UInt8: return 0xffD500F9;  // TODO: cross check which colours to use
        }
    }
    assert(0);
    return 0;
}

static uint32_t darkenOrLighten(uint32_t color) {
    if (LookAndFeel::instance().style == LookAndFeel::Style::Light) {
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

static void addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, const ImVec2& p, ImVec2 size) {
    const bool   input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2 min   = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2 max   = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 rmin  = ImVec2(input ? min.x : max.x, (min.y + max.y) / 2.f);
    const ImVec2 rmax  = ImVec2(rmin.x + 1, rmin.y + 1);

    if (input) {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_SnapLinkToPinDir, 1);
    }

    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(rmin, rmax);
    ax::NodeEditor::PinRect(min, max);
    ax::NodeEditor::EndPin();

    if (input) {
        ax::NodeEditor::PopStyleVar(3);
    }
};

static void newDrawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, float spacing, float textMargin, const std::string& name, DataType type) {
    drawList->AddRectFilled(pinPosition, pinPosition + pinSize, colorForDataType(type));
    drawList->AddRect(pinPosition, pinPosition + pinSize, darkenOrLighten(colorForDataType(type)));

    ImGui::SetCursorPosX(pinPosition.x + textMargin);
    ImGui::SetCursorPosY(pinPosition.y - spacing);
    ImGui::TextUnformatted(name.c_str());
};

static void drawPin(ImDrawList* drawList, ImVec2 rectSize, float spacing, float textMargin, const std::string& name, DataType type) {
    auto p = ImGui::GetCursorScreenPos();
    drawList->AddRectFilled(p, p + rectSize, colorForDataType(type));
    drawList->AddRect(p, p + rectSize, darkenOrLighten(colorForDataType(type)));

    auto y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textMargin);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - spacing);
    ImGui::TextUnformatted(name.c_str());

    ImGui::SetCursorPosY(y + rectSize.y + spacing);
};

template<auto GetPorts, decltype(Connection::src) Connection::*ConnectionEnd>
static bool blockInTreeHelper(const Block* block, const Block* start) {
    if (block == start) {
        return true;
    }

    for (const auto& port : GetPorts(start)) {
        for (auto* c : port.portConnections) {
            if (blockInTreeHelper<GetPorts, ConnectionEnd>(block, (c->*ConnectionEnd).uiBlock)) {
                return true;
            }
        }
    }
    return false;
}

static bool blockInTree(const Block* block, const Block* start) {
    constexpr auto inputs  = [](const Block* b) { return b->inputs(); };
    constexpr auto outputs = [](const Block* b) { return b->outputs(); };
    return blockInTreeHelper<inputs, &Connection::src>(block, start) || blockInTreeHelper<outputs, &Connection::dst>(block, start);
}

void valToString(const pmtv::pmt& val, std::string& str) {
    std::visit(gr::meta::overloaded{
                   [&](const std::string& s) { str = s; },
                   [&](const auto& /* a */) { str = "na"; },
               },
        val);
}

void FlowGraphItem::addBlock(const Block& b, std::optional<ImVec2> nodePos, Alignment alignment) {
    const auto nodeId  = ax::NodeEditor::NodeId(&b);
    const auto padding = ax::NodeEditor::GetStyle().NodePadding;

    const bool filteredOut = [&]() {
        if (m_filterBlock && !blockInTree(&b, m_filterBlock)) {
            return true;
        }
        return false;
    }();

    {
        IMW::Disabled disabled(filteredOut);

        if (nodePos) {
            ax::NodeEditor::SetNodeZPosition(nodeId, 1000);

            auto p = nodePos.value();
            if (alignment == Alignment::Right) {
                float       width = 80;
                std::string value;
                for (const auto& val : b.settings()) {
                    valToString(val.second, value);
                    float w = ImGui::CalcTextSize("%s: %s", val.first.c_str(), value.c_str()).x;
                    width   = std::max(width, w);
                }
                width += padding.x + padding.z;
                p.x -= width;
            }

            ax::NodeEditor::SetNodePosition(nodeId, p);
        }
        ax::NodeEditor::BeginNode(nodeId);
        auto nodeBeginPos = ImGui::GetCursorScreenPos();

        ImGui::TextUnformatted(b.name.c_str());

        auto         curPos       = ImGui::GetCursorScreenPos();
        auto         leftPos      = curPos.x - padding.x;
        const int    rectHeight   = 14;
        const int    rectsSpacing = 5;
        const int    textMargin   = 2;
        const ImVec2 minSize{80.0f, 70.0f};
        auto         yMax{minSize.y}; // we have to keep track of the Node Size ourselves

        std::string value;
        for (const auto& val : b.settings()) {
            const auto metaKey = val.first + "::visible";
            const auto it      = b.metaInformation().find(metaKey);
            if (it != b.metaInformation().end()) {
                if (const auto visiblePtr = std::get_if<bool>(&it->second); visiblePtr && !(*visiblePtr)) {
                    continue;
                }
            }
            valToString(val.second, value);
            ImGui::Text("%s: %s", val.first.c_str(), value.c_str());
        }
        auto positionAfterTexts = ImGui::GetCursorScreenPos();

        ImGui::SetCursorScreenPos(curPos);
        const auto& inputs      = b.inputs();
        auto*       inputWidths = static_cast<float*>(alloca(sizeof(float) * inputs.size()));

        auto   curScreenPos = ImGui::GetCursorScreenPos();
        ImVec2 pos          = {curScreenPos.x - padding.x, curScreenPos.y};

        for (std::size_t i = 0; i < inputs.size(); ++i) {
            inputWidths[i] = ImGui::CalcTextSize(b.currentInstantiation().inputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&inputs[i]), ax::NodeEditor::PinKind::Input, pos, {inputWidths[i], rectHeight});
            }
            pos.y += rectHeight + rectsSpacing;
        }
        // make sure the node ends up being tall enough to fit all the pins
        yMax = std::max(yMax, pos.y - curPos.y);

        const auto& outputs      = b.outputs();
        auto*       outputWidths = static_cast<float*>(alloca(sizeof(float) * outputs.size()));
        ImVec2      nodeSize     = ax::NodeEditor::GetNodeSize(nodeId);
        pos                      = {curPos.x - padding.x + nodeSize.x, curPos.y};
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            outputWidths[i] = ImGui::CalcTextSize(b.currentInstantiation().outputs[i].name.c_str()).x + textMargin * 2;
            if (!filteredOut) {
                addPin(ax::NodeEditor::PinId(&outputs[i]), ax::NodeEditor::PinKind::Output, pos, {outputWidths[i], rectHeight});
            }
            pos.y += rectHeight + rectsSpacing;
        }
        // likewise for the output pins
        yMax = std::max(yMax, pos.y - curScreenPos.y);

        // Now for the Filter Button
        ImGui::SetCursorScreenPos(positionAfterTexts);
        curScreenPos            = ImGui::GetCursorScreenPos();
        auto   filterButtonSize = (ImGui::CalcTextSize("Dummy").y + padding.y + padding.w + 20);
        auto   myHeight         = curScreenPos.y - nodeBeginPos.y + filterButtonSize - (curPos.y - nodeBeginPos.y);
        ImVec2 filterPos;

        if (myHeight < yMax) {
            // Find the lower end, deduct myHeight
            filterPos = curPos;
            filterPos.y += yMax - filterButtonSize;
            ImGui::SetCursorScreenPos(filterPos);
        }

        {
            IMW::ChangeStrId newId(b.name.c_str());
            if (ImGui::RadioButton("Filter", m_filterBlock == &b)) {
                if (m_filterBlock == &b) {
                    m_filterBlock = nullptr;
                } else {
                    m_filterBlock = &b;
                }
            }
        }

        ax::NodeEditor::EndNode();

        // The input/output pins are drawn after ending the node because otherwise
        // drawing them would increase the node size, which we need to know to correctly place the
        // output pins, and that would cause the nodes to continuously grow in width

        ImGui::SetCursorScreenPos(curPos);
        auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(nodeId);

        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& in = inputs[i];

            ImGui::SetCursorPosX(leftPos - inputWidths[i]);
            drawPin(drawList, {inputWidths[i], rectHeight}, rectsSpacing, textMargin, b.currentInstantiation().inputs[i].name, in.portDataType);
        }

        ImGui::SetCursorScreenPos(curPos);
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            const auto& out = outputs[i];

            nodeSize = ax::NodeEditor::GetNodeSize(nodeId);
            ImGui::SetCursorPosX(leftPos + nodeSize.x);
            drawPin(drawList, {outputWidths[i], rectHeight}, rectsSpacing, textMargin, b.currentInstantiation().outputs[i].name, out.portDataType);
        }

        ImGui::SetCursorScreenPos(curPos);
    }

    const auto size = ax::NodeEditor::GetNodeSize(nodeId);
    ImGui::SetCursorScreenPos(ax::NodeEditor::GetNodePosition(nodeId) + ImVec2(padding.x, size.y - padding.y - padding.w - 20));
}

void newDrawGraph(UiGraphModel& graphModel, const ImVec2& size) {
    //
    IMW::NodeEditor::Editor nodeEditor("My Editor", ImVec2{size.x, size.y}); // ImGui::GetContentRegionAvail());
    const auto              padding = ax::NodeEditor::GetStyle().NodePadding;

    {
        struct BoundingBox {
            // TODO: proper initial min max values
            float minX = 0, minY = 0, maxX = 0, maxY = 0;

            void addRectangle(ImVec2 position, ImVec2 size) {
                minX = std::min(minX, position[0]);
                minY = std::min(minY, position[1]);
                maxX = std::min(maxX, position[0] + size[0]);
                maxY = std::min(maxY, position[1] + size[1]);
            }
        };

        BoundingBox boundingBox;

        // TODO: Move to the theme definition
        const int    pinHeight  = 14;
        const int    pinSpacing = 5;
        const int    textMargin = 2;
        const ImVec2 minimumBlockSize{80.0f, 0.0f};

        // We need to pass all blocks in order for NodeEditor to calculate
        // the sizes. Then, we can arrange those that are newly created
        for (auto& block : graphModel.blocks()) {
            auto blockId = ax::NodeEditor::NodeId(std::addressof(block));

            const auto& inputPorts       = block.inputPorts;
            const auto& outputPorts      = block.outputPorts;
            auto&       inputPortWidths  = block.inputPortWidths;
            auto&       outputPortWidths = block.outputPortWidths;

            auto blockPosition = [&] {
                IMW::NodeEditor::Node node(blockId);

                const auto blockScreenPosition = ImGui::GetCursorScreenPos();
                auto       blockBottomY{blockScreenPosition.y + minimumBlockSize.y}; // we have to keep track of the Node Size ourselves

                // Draw block title
                auto name = block.blockName;
                ImGui::TextUnformatted(name.c_str());
                auto blockSize = ax::NodeEditor::GetNodeSize(blockId);

                // Draw block properties
                std::string value;
                for (const auto& [propertyKey, propertyValue] : block.blockSettings) {
                    if (propertyKey == "description" || propertyKey.contains("::")) {
                        continue;
                    }

                    const auto metaKey = propertyKey + "::visible";
                    const auto it      = block.blockMetaInformation.find(metaKey);
                    if (it != block.blockMetaInformation.end()) {
                        if (const auto visiblePtr = std::get_if<bool>(&it->second); visiblePtr && !(*visiblePtr)) {
                            continue;
                        }
                    }

                    valToString(propertyValue, value);
                    ImGui::Text("%s: %s", propertyKey.c_str(), value.c_str());
                }

                blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

                // Update bounding box
                if (block.view.has_value()) {
                    auto position = ax::NodeEditor::GetNodePosition(blockId);
                    block.view->x = position[0];
                    block.view->y = position[1];
                    boundingBox.addRectangle(position, blockSize);
                }

                // Register ports with node editor, actual drawing comes later
                auto registerPins = [&padding, &pinHeight, &blockId](auto& ports, auto& widths, auto position, auto pinType) {
                    widths.resize(ports.size());
                    if (pinType == ax::NodeEditor::PinKind::Output) {
                        auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
                        position.x += blockSize.x - padding.x;
                    }

                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        widths[i] = ImGui::CalcTextSize(ports[i].portName.c_str()).x + textMargin * 2;
                        // TODO Reimplement block visual filtering
                        // if (!filteredOut) {
                        addPin(ax::NodeEditor::PinId(&ports[i]), pinType, position, {widths[i], pinHeight});
                        // }
                        position.y += pinHeight + pinSpacing;
                    }
                };

                ImVec2 position = {blockScreenPosition.x - padding.x, blockScreenPosition.y};
                registerPins(inputPorts, inputPortWidths, position, ax::NodeEditor::PinKind::Input);
                blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

                registerPins(outputPorts, outputPortWidths, blockScreenPosition, ax::NodeEditor::PinKind::Output);
                blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

                ImGui::SetCursorScreenPos({position.x, blockBottomY});

                struct result {
                    ImVec2 topLeft;
                    float  bottomY;
                };
                return result{position, blockBottomY};
            }();

            // The input/output pins are drawn after ending the node because otherwise
            // drawing them would increase the node size, which we need to know to correctly place the
            // output pins, and that would cause the nodes to continuously grow in width
            {
                const auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
                // const auto blockPosition          = ax::NodeEditor::GetNodePosition(blockId);

                auto leftPos = blockPosition.topLeft.x - padding.x;

                ImGui::SetCursorScreenPos(blockPosition.topLeft);
                auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(blockId);

                auto drawPorts = [&](auto& ports, auto& widths, auto leftPos, bool rightAlign) {
                    auto pinPositionY = blockPosition.topLeft.y;
                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        auto pinPositionX = leftPos + padding.x - (rightAlign ? widths[i] : 0);
                        newDrawPin(drawList, {pinPositionX, pinPositionY}, {widths[i], pinHeight}, pinSpacing, textMargin, ports[i].portName, {} /* TODO in.portType */);
                        pinPositionY += pinHeight + pinSpacing;
                    }
                };

                drawPorts(inputPorts, inputPortWidths, leftPos, true);
                drawPorts(outputPorts, outputPortWidths, leftPos + blockSize.x, false);
            }
        }

        for (auto& block : graphModel.blocks()) {
            if (!block.view.has_value()) {
                auto blockId   = ax::NodeEditor::NodeId(std::addressof(block));
                auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
                block.view     = UiGraphBlock::ViewData{//
                        .x      = boundingBox.minX,     //
                        .y      = boundingBox.maxY,     //
                        .width  = blockSize[0],         //
                        .height = blockSize[1]};
                ax::NodeEditor::SetNodePosition(blockId, ImVec2(block.view->x, block.view->y));
                boundingBox.minX += blockSize[0] + padding.x;
            }
        }

        const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
        for (auto& edge : graphModel.edges()) {
            ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), //
                ax::NodeEditor::PinId(edge.edgeSourcePort),     //
                ax::NodeEditor::PinId(edge.edgeDestinationPort), linkColor);
        }

        // Handle creation action, returns true if editor want to create new object (node or link)
        if (auto creation = IMW::NodeEditor::Creation(linkColor, 1.0f)) {
            ax::NodeEditor::PinId inputPinId, outputPinId;
            if (ax::NodeEditor::QueryNewLink(&outputPinId, &inputPinId)) {
                // QueryNewLink returns true if editor wants to create new link between pins.
                //
                // Link can be created only for two valid pins, it is up to you to
                // validate if connection make sense. Editor is happy to make any.
                //
                // Link always goes from input to output. User may choose to drag
                // link from output pin or input pin. This determines which pin ids
                // are valid and which are not:
                //   * input valid, output invalid - user started to drag new link from input pin
                //   * input invalid, output valid - user started to drag new link from output pin
                //   * input valid, output valid   - user dragged link over other pin, can be validated

                if (inputPinId && outputPinId) // both are valid, let's accept link
                {
                    auto* inputPort  = inputPinId.AsPointer<UiGraphPort>();
                    auto* outputPort = outputPinId.AsPointer<UiGraphPort>();

                    if (inputPort->portDirection == outputPort->portDirection) {
                        ax::NodeEditor::RejectNewItem();

                    } else {
                        if (ax::NodeEditor::AcceptNewItem()) {
                            // AcceptNewItem() return true when user release mouse button.
                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::graph::property::kEmplaceEdge;
                            message.data     = gr::property_map{                                   //
                                {"sourceBlock"s, outputPort->ownerBlock->blockUniqueName},     //
                                {"sourcePort"s, outputPort->portName},                         //
                                {"destinationBlock"s, inputPort->ownerBlock->blockUniqueName}, //
                                {"destinationPort"s, inputPort->portName},                     //
                                {"minBufferSize"s, gr::Size_t(4096)},                          //
                                {"weight"s, std::int32_t(1)},                                  //
                                {"edgeName"s, std::string()}};
                            App::instance().sendMessage(message);
                        }
                    }
                }
            }
        }
    }
}

void FlowGraphItem::draw(FlowGraph* fg, const ImVec2& size) {

    auto& c = m_editors[fg];
    if (!c.editor) {
        return;
    }

    c.config.UserPointer = &c;
    ax::NodeEditor::SetCurrentEditor(c.editor);

    const ImVec2 origCursorPos = ImGui::GetCursorScreenPos();
    const float  left          = ImGui::GetCursorPosX();
    const float  top           = ImGui::GetCursorPosY();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPaneContext.block);

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    if (m_layoutGraph) {
        m_layoutGraph = false;
        sortNodes(fg);
    }

    newDrawGraph(fg->graphModel, size);

    auto mouseDrag         = ImLengthSqr(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right));
    auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouseDrag < 200 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();

        if (!block) {
            m_editPaneContext.block = nullptr;
        } else {
            m_editPaneContext.block     = block;
            m_editPaneContext.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        }
    }

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        auto n     = ax::NodeEditor::GetDoubleClickedNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block) {
            ImGui::OpenPopup("Block settings");
            m_selectedBlock = block;
            // m_parameters.clear();
            // for (auto &p : block->parameters()) {
            //     m_parameters.push_back(p);
            // }
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block) {
            ImGui::OpenPopup("block_ctx_menu");
            m_selectedBlock = block;
        }
    }

    bool openNewBlockDialog = false;
    if (backgroundClicked == ImGuiMouseButton_Right && mouseDrag < 200) {
        ImGui::OpenPopup("ctx_menu");
        m_contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    }

    if (auto menu = IMW::Popup("ctx_menu", 0)) {
        if (ImGui::MenuItem("New block")) {
            openNewBlockDialog = true;
        }
        if (ImGui::MenuItem("Rearrange blocks")) {
            sortNodes(fg);
        }
        if (ImGui::MenuItem("Refresh graph")) {
            fg->graphModel.requestGraphUpdate();
        }
    }

    if (auto menu = IMW::Popup("block_ctx_menu", 0)) {
        if (ImGui::MenuItem("Delete")) {
            // Send message to delete block
            gr::Message message;
            message.endpoint = gr::graph::property::kRemoveBlock;
            message.data     = gr::property_map{{"uniqueName"s, m_selectedBlock->blockUniqueName}};
            App::instance().sendMessage(std::move(message));
        }

        std::string_view blockType         = m_selectedBlock->blockTypeName;
        auto             blockTypeSplitter = std::ranges::find(blockType, '<');
        if (blockTypeSplitter != blockType) {
            const auto& types = BlockRegistry::instance().types();

            const auto currentBlockBaseType            = std::string(blockType.cbegin(), blockTypeSplitter);
            const auto currentBlockParametrizationType = std::string(blockTypeSplitter + 1, blockType.cend() - 1);
            const auto currentBlockBaseTypeInfoIt      = types.find(currentBlockBaseType);
            if (currentBlockBaseTypeInfoIt != types.cend()) {
                for (const auto& availableParametrization : currentBlockBaseTypeInfoIt->second->availableParametrizations) {
                    if (availableParametrization != currentBlockParametrizationType) {
                        auto name = std::string{"Change Type to "} + availableParametrization;
                        if (ImGui::MenuItem(name.c_str())) {
                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::graph::property::kReplaceBlock;
                            message.data     = gr::property_map{
                                    {"uniqueName"s, m_selectedBlock->blockUniqueName}, //
                                    {"type"s, std::move(currentBlockBaseType)},        //
                                    {"parameters"s, availableParametrization}          //
                            };
                            App::instance().sendMessage(message);
                        }
                    }
                }
            }
        }
    }

    // Create a new ImGui window for an overlay over the NodeEditor , where we can place our buttons
    // if we don't put the buttons in this overlay, the click events will go to the editor instead of the buttons
    if (horizontalSplit) {
        ImGui::SetNextWindowPos({origCursorPos.x, origCursorPos.y + size.y - 37.f}, ImGuiCond_Always);
    } else {
        ImGui::SetNextWindowPos({origCursorPos.x, origCursorPos.y + size.y * (1.f - ratio) - 39.f}, ImGuiCond_Always); // on vertical, we need some extra space for the splitter
    }

    ImGui::SetNextWindowSize({size.x * ((ratio > 0.f) && horizontalSplit ? 1.f - ratio : 1.f), 37.f});
    {
        IMW::Window overlay("Button Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        // These Buttons are rendered on top of the Editor, to make them properly readable, take out the opacity
        ImVec4 buttonColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        buttonColor.w      = 1.0f;

        {
            IMW::StyleColor buttonStyle(ImGuiCol_Button, buttonColor);

            ImGui::SetCursorPosX(15);
            if (ImGui::Button("Add signal")) {
                m_signalSelector.open();
            }

            ImGui::SameLine();

            float newSinkButtonPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("New Sink").x - 15;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + newSinkButtonPos / 2 - ImGui::CalcTextSize("Re-Layout Graph").x);
            if (ImGui::Button("Re-Layout Graph")) {
                m_layoutGraph = true;
            }

            ImGui::SameLine();

            ImGui::SetCursorPosX(newSinkButtonPos);
            if (ImGui::Button("New sink") && newSinkCallback) {
                m_nodesToArrange.push_back(newSinkCallback(fg));
            }
        }

        if (openNewBlockDialog) {
            ImGui::OpenPopup("New block");
        }
        drawNewBlockDialog(fg);
        m_signalSelector.draw(fg);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }
}

void FlowGraphItem::drawNewBlockDialog(FlowGraph* /* fg */) {
    ImGui::SetNextWindowSize({600, 300}, ImGuiCond_Once);
    if (auto menu = IMW::ModalPopup("New block", nullptr, 0)) {
        auto ret = components::FilteredListBox("blocks", BlockRegistry::instance().types(), [](auto& it) -> std::pair<BlockDefinition*, std::string> {
            if (it.second->isSource) {
                return {};
            }
            return std::pair{it.second.get(), it.first};
        });

        if (ret) {
            const auto& selected      = ret.value().first;
            m_selectedBlockDefinition = selected;

        } else {
            m_selectedBlockDefinition = nullptr;
        }

        if (components::DialogButtons() == components::DialogButton::Ok) {
            if (m_selectedBlockDefinition) {
                // TODO kill m_createNewBlock
                m_createNewBlock = true;
                // sendMessage
                gr::Message message;
                std::string type   = m_selectedBlockDefinition->name;
                std::string params = "float";
                message.cmd        = gr::message::Command::Set;
                message.endpoint   = gr::graph::property::kEmplaceBlock;
                message.data       = gr::property_map{
                          {"type"s, std::move(type)},        //
                          {"parameters"s, std::move(params)} //
                };
                App::instance().sendMessage(message);
            }
        }
    }
}

void FlowGraphItem::sortNodes(FlowGraph* fg) {
    auto blockLevels = topologicalSort(fg->graphModel.blocks(), fg->graphModel.edges());

    constexpr float ySpacing = 32;
    constexpr float xSpacing = 200;

    float x = 0;
    for (const auto& level : blockLevels) {
        float y          = 0;
        float levelWidth = 0;

        for (const auto& block : level.blocks) {
            auto blockId = ax::NodeEditor::NodeId(block);
            ax::NodeEditor::SetNodePosition(blockId, ImVec2(x, y));
            auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
            y += blockSize.y + ySpacing;
            levelWidth = std::max(levelWidth, blockSize.x);
        }

        x += levelWidth + xSpacing;
    }
}

void FlowGraphItem::arrangeUnconnectedNodes(FlowGraph* fg, const std::vector<const Block*>& blocks) {
    float x_max = 0, y_max = 0, x_min = std::numeric_limits<float>::max();
    for (const auto& b : fg->blocks()) {
        if (std::ranges::any_of(blocks, [&](auto* n) { return n == b.get(); }) /*|| !b*/) {
            continue;
        }
        auto id  = ax::NodeEditor::NodeId(b.get());
        auto pos = ax::NodeEditor::GetNodePosition(id);
        auto k   = pos + ax::NodeEditor::GetNodeSize(id);
        x_max    = std::max(x_max, k.x);
        y_max    = std::max(y_max, k.y);
        x_min    = std::min(x_min, pos.x);
    }

    enum Arrange { Left, Middle, Right } arrange;
    std::map<Arrange, float> columnOffsets{{Left, 0}, {Middle, 0}, {Right, 0}};
    for (auto b : blocks) {
        auto id   = ax::NodeEditor::NodeId(b);
        auto size = ax::NodeEditor::GetNodeSize(id);

        const float padding = 50;

        if (b->inputs().size() == 0 && b->outputs().size() > 0) {
            arrange = Left;
        } else if (b->inputs().size() > 0 && b->outputs().size() == 0) {
            arrange = Right;
        } else {
            arrange = Middle;
        }

        ImVec2 position{x_min, y_max + padding};
        if (arrange == Left) {
        } else if (arrange == Middle) {
            position.x = x_max / 2 - size.x / 2;
        } else {
            position.x = x_max - size.x;
        }
        position.y += columnOffsets[arrange];
        columnOffsets[arrange] += padding + size.y;

        ax::NodeEditor::SetNodePosition(id, position);
    }
}

} // namespace DigitizerUi
