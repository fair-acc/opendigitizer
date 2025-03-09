#include "FlowgraphItem.hpp"

#include <algorithm>

#include <crude_json.h>
#include <fmt/format.h>

#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/Dialog.hpp"
#include "components/ImGuiNotify.hpp"
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

FlowGraphItem::FlowGraphItem() {
    m_editorConfig.SettingsFile = nullptr;
    m_editorConfig.UserPointer  = std::addressof(m_graphModel);
    reset();
}

FlowGraphItem::~FlowGraphItem() { ax::NodeEditor::DestroyEditor(m_editor); }

void FlowGraphItem::reset() {
    m_graphModel.reset();

    if (m_editor) {
        ax::NodeEditor::SetCurrentEditor(nullptr);
        ax::NodeEditor::DestroyEditor(m_editor);
    }

    m_editor = ax::NodeEditor::CreateEditor(&m_editorConfig);
    ax::NodeEditor::SetCurrentEditor(m_editor);
    setEditorStyle(m_editor, LookAndFeel::instance().style);
}

void FlowGraphItem::setStyle(LookAndFeel::Style s) { setEditorStyle(m_editor, s); }

struct DataTypeStyle {
    std::uint32_t color;
    bool          unsignedMarker = false;
    bool          datasetMarker  = false;
};
static const DataTypeStyle& styleForDataType(std::string_view type) {
    struct transparent_string_hash // why isn't std::hash<std::string> this exact same thing?
    {
        using hash_type      = std::hash<std::string_view>;
        using is_transparent = void;

        size_t operator()(const char* str) const { return hash_type{}(str); }
        size_t operator()(std::string_view str) const { return hash_type{}(str); }
        size_t operator()(std::string const& str) const { return hash_type{}(str); }
    };
    using DataTypeStyleMap = std::unordered_map<std::string, DataTypeStyle, transparent_string_hash, std::equal_to<>>;

    auto withDataSetColors = [](DataTypeStyleMap&& map) {
        DataTypeStyleMap result;
        while (map.begin() != map.end()) {
            auto it          = map.begin();
            auto datasetName = "gr::DataSet<"s + it->first + ">"s;

            result[datasetName]               = it->second;
            result[datasetName].datasetMarker = true;

            result.insert(map.extract(it));
        }

        return result;
    };

    static auto styleForDataTypeLight = withDataSetColors({
        {"float32"s, {0xffF57C00}}, //
        {"float64"s, {0xff00BCD4}}, //

        {"int8"s, {0xffD500F9}},                  //
        {"int16"s, {0xffFFEB3B}},                 //
        {"int32"s, {0xff009688}},                 //
        {"int64"s, {0xffCDDC39}},                 //
        {"uint8"s, {0xffD500F9, true}},           //
        {"uint16"s, {0xffFFEB3B, true}},          //
        {"uint32"s, {0xff009688, true}},          //
        {"uint64"s, {0xffCDDC39, true}},          //
                                                  //
        {"std::complex<float32>"s, {0xff2196F3}}, //
        {"std::complex<float64>"s, {0xff795548}}, //
                                                  //
        {"std::complex<int8>"s, {0xff9C27B0}},    //
        {"std::complex<int16>"s, {0xffFFC107}},   //
        {"std::complex<int32>"s, {0xff4CAF50}},   //
        {"std::complex<int64>"s, {0xff8BC34A}},   //

        {"gr::DataSet<float32>"s, {0xffF57C00, false, true}}, //
        {"gr::DataSet<float64>"s, {0xff00BCD4, false, true}}, //
                                                              //
        {"gr::Message"s, {0xffDBDBDB}},                       //

        {"Bits"s, {0xffEA80FC}},          //
        {"BusConnection"s, {0xffffffff}}, //
        {"Wildcard"s, {0xffffffff}},      //
        {"Untyped"s, {0xffffffff}},       //
    });

    static auto styleForDataTypeDark = withDataSetColors({
        {"float32"s, {0xff0a83ff}}, //
        {"float64"s, {0xffff432b}}, //

        {"int8"s, {0xff2aff06}},         //
        {"int16"s, {0xff0014c4}},        //
        {"int32"s, {0xffff6977}},        //
        {"int64"s, {0xff3223c6}},        //
        {"uint8"s, {0xff2aff06, true}},  //
        {"uint16"s, {0xff0014c4, true}}, //
        {"uint32"s, {0xffff6977, true}}, //
        {"uint64"s, {0xff3223c6, true}}, //

        {"std::complex<float32>"s, {0xffde690c}}, //
        {"std::complex<float64>"s, {0xff86aab8}}, //

        {"std::complex<int8>"s, {0xff63d84f}},  //
        {"std::complex<int16>"s, {0xff003ef8}}, //
        {"std::complex<int32>"s, {0xffb350af}}, //
        {"std::complex<int64>"s, {0xff743cb5}}, //

        {"gr::DataSet<float64>"s, {0xffff432b}}, //
        {"gr::DataSet<float32>"s, {0xff0a83ff}}, //

        {"gr::Message"s, {0xff242424}}, //

        {"Bits"s, {0xff158003}},          //
        {"BusConnection"s, {0xff000000}}, //
        {"Wildcard"s, {0xff000000}},      //
        {"Untyped"s, {0xff000000}},       //

    });

    auto& map = LookAndFeel::instance().style == LookAndFeel::Style::Light ? styleForDataTypeLight : styleForDataTypeDark;
    auto  it  = map.find(type);
    if (it == map.cend()) {
        fmt::print("Warning: Color not defined for {}\n", type);
        return {0x00000000};
    } else {
        return it->second;
    }
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

static void drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, bool rightAlign, float spacing, float textMargin, const std::string& name, const std::string& type) {

    const auto& style = styleForDataType(type);
    drawList->AddRectFilled(pinPosition, pinPosition + pinSize, style.color);
    drawList->AddRect(pinPosition, pinPosition + pinSize, darkenOrLighten(style.color));
    ImGui::SetCursorPosX(pinPosition.x + textMargin);
    ImGui::SetCursorPosY(pinPosition.y - spacing);

    ImGui::TextUnformatted(name.c_str());
};

void valToString(const pmtv::pmt& val, std::string& str) {
    std::visit(gr::meta::overloaded{
                   [&](const std::string& s) { str = s; },
                   [&](const auto& /* a */) { str = "na"; },
               },
        val);
}

void drawGraph(UiGraphModel& graphModel, const ImVec2& size) {
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
        const int    textMargin = 4;
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
                auto registerPins = [&padding, &pinHeight, &blockId, &blockSize](auto& ports, auto& widths, auto position, auto pinType) {
                    widths.resize(ports.size());
                    if (pinType == ax::NodeEditor::PinKind::Output) {
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

                auto leftPos = blockPosition.topLeft.x - padding.x;

                ImGui::SetCursorScreenPos(blockPosition.topLeft);
                auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(blockId);

                auto drawPorts = [&](auto& ports, auto& widths, auto portLeftPos, bool rightAlign) {
                    auto pinPositionY = blockPosition.topLeft.y;
                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        auto pinPositionX = portLeftPos + padding.x - (rightAlign ? widths[i] : 0);
                        drawPin(drawList, {pinPositionX, pinPositionY}, {widths[i], pinHeight}, rightAlign, pinSpacing, textMargin, ports[i].portName, ports[i].portType);
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
                                {"weight"s, 1},                                                //
                                {"edgeName"s, std::string()}};
                            App::instance().sendMessage(message);
                        }
                    }
                }
            }
        }
    }
}

void FlowGraphItem::drawNodeEditor(const ImVec2& size) {

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
        sortNodes();
    }

    drawGraph(m_graphModel, size);

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
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block) {
            ImGui::OpenPopup("block_ctx_menu");
            m_selectedBlock = block;
        }
    }

    if (backgroundClicked == ImGuiMouseButton_Right && mouseDrag < 200) {
        ImGui::OpenPopup("ctx_menu");
        m_contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    }

    bool openNewBlockDialog       = false;
    bool openRemoteSignalSelector = false;

    if (auto menu = IMW::Popup("ctx_menu", 0)) {
        if (ImGui::MenuItem("Add block...")) {
            openNewBlockDialog = true;
        }
        if (ImGui::MenuItem("Add remote signal...")) {
            openRemoteSignalSelector = true;
        }
        if (ImGui::MenuItem("Rearrange blocks")) {
            sortNodes();
        }
        if (ImGui::MenuItem("Refresh graph")) {
            m_graphModel.requestGraphUpdate();
            m_graphModel.requestAvailableBlocksTypesUpdate();
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

        auto typeParams = m_graphModel.availableParametrizationsFor(m_selectedBlock->blockTypeName);
        if (typeParams.availableParametrizations) {
            if (typeParams.availableParametrizations->size() > 1) {
                for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                    if (availableParametrization != typeParams.parametrization) {
                        auto name = std::string{"Change Type to "} + availableParametrization;
                        if (ImGui::MenuItem(name.c_str())) {
                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::graph::property::kReplaceBlock;
                            message.data     = gr::property_map{
                                    {"uniqueName"s, m_selectedBlock->blockUniqueName},                    //
                                    {"type"s, std::move(typeParams.baseType) + availableParametrization}, //
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
            if (ImGui::Button("Add block...")) {
                openNewBlockDialog = true;
            }
            ImGui::SameLine();

            if (ImGui::Button("Add remote signal...")) {
                openRemoteSignalSelector = true;
            }
            ImGui::SameLine();

            float relayoutGraphButtonPos = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Rearrange blocks").x - 15;
            ImGui::SetCursorPosX(relayoutGraphButtonPos);
            if (ImGui::Button("Rearrange blocks")) {
                m_layoutGraph = true;
            }
        }

        if (openNewBlockDialog) {
            m_newBlockSelector.open();
        }
        if (openRemoteSignalSelector) {
            m_remoteSignalSelector.open();
        }

        m_remoteSignalSelector.draw();
        m_newBlockSelector.draw(m_graphModel.knownBlockTypes);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }
}

void FlowGraphItem::draw(Dashboard& dashboard) noexcept {
    // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
    IMW::TabBar tabBar("maintabbar", 0);
    if (auto item = IMW::TabItem("Local", nullptr, 0)) {
        auto contentRegion = ImGui::GetContentRegionAvail();
        drawNodeEditor(contentRegion);
    }

    if (auto item = IMW::TabItem("Local - YAML", nullptr, 0)) {
        if (ImGui::Button("Reset")) {
            // localFlowgraphGrc = dashboard->localFlowGraph.grc();
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply")) {

            // auto sinkNames = [](const auto& blocks) {
            //     using namespace std;
            //     auto isPlotSink = [](const auto& b) { return b->type().isPlotSink(); };
            //     auto getName    = [](const auto& b) { return b->name; };
            //     auto namesView  = blocks | views::filter(isPlotSink) | views::transform(getName);
            //     auto names      = std::vector(namesView.begin(), namesView.end());
            //     ranges::sort(names);
            //     names.erase(std::unique(names.begin(), names.end()), names.end());
            //     return names;
            // };

            // const auto oldNames = sinkNames(app->dashboard->localFlowGraph.blocks());

            try {
                // app->dashboard->localFlowGraph.parse(localFlowgraphGrc);
                // const auto               newNames = sinkNames(app->dashboard->localFlowGraph.blocks());
                // std::vector<std::string> toRemove;
                // std::ranges::set_difference(oldNames, newNames, std::back_inserter(toRemove));
                // std::vector<std::string> toAdd;
                // std::ranges::set_difference(newNames, oldNames, std::back_inserter(toAdd));
                // for (const auto& name : toRemove) {
                //     app->dashboard->removeSinkFromPlots(name);
                // }
                // for (const auto& newName : toAdd) {
                //     app->dashboardPage.newPlot(*app->dashboard);
                //     app->dashboard->plots().back().sourceNames.push_back(newName);
                // }
            } catch (const std::exception& e) {
                // TODO show error message
                auto msg = fmt::format("Error parsing YAML: {}", e.what());
                components::Notification::error(msg);
            }
        }

        std::string localFlowgraphGrc;
        ImGui::InputTextMultiline("##grc", &localFlowgraphGrc, ImGui::GetContentRegionAvail());
    }

    for (auto& s : dashboard.remoteServices()) {
        std::string tabTitle = "Remote YAML for " + s.name;
        if (auto item = IMW::TabItem(tabTitle.c_str(), nullptr, 0)) {
            if (ImGui::Button("Reload from service")) {
                s.reload();
            }
            ImGui::SameLine();
            if (ImGui::Button("Execute on service")) {
                s.execute();
            }

            // TODO: For demonstration purposes only, remove
            // once we have a proper server-side graph editor
            // if (::getenv("DIGITIZER_UI_SHOW_SERVER_TEST_BUTTONS")) {
            ImGui::SameLine();
            if (ImGui::Button("Create a block")) {
                s.emplaceBlock("gr::basic::DataSink", "float");
            }
            // }

            ImGui::InputTextMultiline("##grc", &s.grc, ImGui::GetContentRegionAvail());
        }
    }
}

void FlowGraphItem::sortNodes() {
    fmt::print("Sorting blocks\n");
    auto blockLevels = topologicalSort(m_graphModel.blocks(), m_graphModel.edges());

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

} // namespace DigitizerUi
