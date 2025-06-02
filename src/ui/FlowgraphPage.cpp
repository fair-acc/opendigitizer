#include "FlowgraphPage.hpp"

#include <algorithm>

#include <crude_json.h>
#include <cstdint>
#include <format>

#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"
#include "common/ImguiWrap.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/Dialog.hpp"
#include "components/ImGuiNotify.hpp"
#include "components/Splitter.hpp"

#include "App.hpp"
#include "scope_exit.hpp"

using namespace std::string_literals;

namespace DigitizerUi {

inline auto topologicalSort(const std::vector<std::unique_ptr<UiGraphBlock>>& blocks, const std::vector<UiGraphEdge>& edges) {
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
        graphConnections[block.get()];
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

        std::ranges::reverse(newLevel.blocks);

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

FlowgraphPage::FlowgraphPage() {
    m_editorConfig.SettingsFile = nullptr;
    m_editorConfig.UserPointer  = this;
}

FlowgraphPage::~FlowgraphPage() { ax::NodeEditor::DestroyEditor(m_editor); }

void FlowgraphPage::reset() {
    if (m_dashboard) {
        m_dashboard->graphModel().reset();
    }

    m_filterBlock = nullptr;

    if (m_editor) {
        ax::NodeEditor::SetCurrentEditor(nullptr);
        ax::NodeEditor::DestroyEditor(m_editor);
    }

    m_editor = ax::NodeEditor::CreateEditor(&m_editorConfig);
    ax::NodeEditor::SetCurrentEditor(m_editor);
    setEditorStyle(m_editor, LookAndFeel::instance().style);
}

void FlowgraphPage::setStyle(LookAndFeel::Style s) {
    if (m_editor) {
        setEditorStyle(m_editor, s);
    }
}

const FlowgraphPage::DataTypeStyle& FlowgraphPage::styleForDataType(std::string_view type) {
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
        std::print("Warning: Color not defined for {}\n", type);
        static DataTypeStyle none{0x00000000};
        return none;
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

std::string valToString(const pmtv::pmt& _val) {
    return std::visit(gr::meta::overloaded{                                //
                          [&](double val) { return std::to_string(val); }, //
                          [&](float val) { return std::to_string(val); },  //
                          [&](auto&& val) {
                              using T = std::remove_cvref_t<decltype(val)>;
                              if constexpr (std::integral<T>) {
                                  std::string x = std::format("{}", val);
                                  return std::to_string(val);
                              } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                  return std::string(val);
                              }
                              return ""s;
                          }},
        _val);
}

float FlowgraphPage::pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight) {
    const float spacing = blockHeight / (static_cast<float>(numPins) + 1);
    return spacing * (static_cast<float>(index) + 1) - (pinHeight / 2);
}

void FlowgraphPage::drawGraph(UiGraphModel& graphModel, const ImVec2& size, const UiGraphBlock*& filterBlock) {
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
        const int    pinWidth  = 10;
        const int    pinHeight = 10;
        const ImVec2 minimumBlockSize{80.0f, 0.0f};

        // to save result of expensive recursion
        std::vector<ax::NodeEditor::NodeId> filteredOutNodes;

        // over-reserved, but minimizes allocations
        filteredOutNodes.reserve(filterBlock ? graphModel.blocks().size() : 0);

        // We need to pass all blocks in order for NodeEditor to calculate
        // the sizes. Then, we can arrange those that are newly created
        for (auto& block : graphModel.blocks()) {
            auto blockId = ax::NodeEditor::NodeId(block.get());

            const auto& inputPorts  = block->inputPorts;
            const auto& outputPorts = block->outputPorts;

            const bool filteredOut = filterBlock && !graphModel.blockInTree(*block.get(), *filterBlock);

            // If filteredOut, set opacity to 25% until we exit the scope
            float                        originalAlpha = std::exchange(ImGui::GetStyle().Alpha, (filteredOut ? 0.25f : ImGui::GetStyle().Alpha));
            Digitizer::utils::scope_exit restoreStyle  = [&] { ImGui::GetStyle().Alpha = originalAlpha; };

            auto blockPosition = [&] {
                if (filteredOut) {
                    filteredOutNodes.push_back(blockId);
                }

                IMW::NodeEditor::Node node(blockId);

                const auto blockScreenPosition = ImGui::GetCursorScreenPos();
                auto       blockBottomY{blockScreenPosition.y + minimumBlockSize.y}; // we have to keep track of the Node Size ourselves

                // Draw block title
                auto name = block->blockName;
                ImGui::TextUnformatted(name.c_str());
                auto blockSize = ax::NodeEditor::GetNodeSize(blockId);

                // Draw block properties
                {
                    IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
                    for (const auto& [propertyKey, propertyValue] : block->blockSettings) {
                        if (propertyKey == "description" || propertyKey.contains("::")) {
                            continue;
                        }

                        const auto& currentPropertyMetaInformation = block->blockSettingsMetaInformation[propertyKey];
                        if (!currentPropertyMetaInformation.isVisible) {
                            continue;
                        }
                        std::string value = valToString(propertyValue);
                        ImGui::Text("%s: %s", currentPropertyMetaInformation.description.c_str(), value.c_str());
                    }

                    ImGui::Spacing();

                    const bool isFilter = filterBlock == block.get();

                    // Make radio-button a bit smaller since we also made the properties smaller, looks huge otherwise
                    IMW::StyleVar    styleVar(ImGuiStyleVar_FramePadding, GImGui->Style.FramePadding - ImVec2{0, 3});
                    IMW::ChangeStrId changeId(block->blockUniqueName.c_str());

                    if (ImGui::RadioButton("Filter", isFilter)) {
                        if (isFilter) {
                            filterBlock = nullptr;
                        } else {
                            filterBlock = block.get();
                        }
                    }
                }

                blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

                // Update bounding box
                if (block->view.has_value()) {
                    auto position  = ax::NodeEditor::GetNodePosition(blockId);
                    block->view->x = position[0];
                    block->view->y = position[1];
                    boundingBox.addRectangle(position, blockSize);
                }

                // Register ports with node editor, actual drawing comes later
                auto registerPins = [&padding, &pinHeight, &blockSize](auto& ports, auto position, auto pinType) {
                    if (pinType == ax::NodeEditor::PinKind::Output) {
                        position.x += blockSize.x - padding.x;
                    }

                    const float blockY = position.y - ax::NodeEditor::GetStyle().NodePadding.y;

                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        position.y = blockY + pinLocalPositionY(i, ports.size(), blockSize.y, pinHeight);
                        addPin(ax::NodeEditor::PinId(&ports[i]), pinType, position, {pinWidth, pinHeight});
                    }
                };

                ImVec2 position = {blockScreenPosition.x - padding.x, blockScreenPosition.y};
                registerPins(inputPorts, position, ax::NodeEditor::PinKind::Input);
                blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

                registerPins(outputPorts, blockScreenPosition, ax::NodeEditor::PinKind::Output);
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

                auto drawPorts = [&](auto& ports, auto portLeftPos, bool rightAlign) {
                    for (std::size_t i = 0; i < ports.size(); ++i) {
                        const auto pinPositionX = portLeftPos + padding.x - (rightAlign ? pinHeight : 0);
                        const auto pinPositionY = blockPosition.topLeft.y - ax::NodeEditor::GetStyle().NodePadding.y + pinLocalPositionY(i, ports.size(), blockSize.y, pinHeight);
                        FlowgraphPage::drawPin(drawList, {pinPositionX, pinPositionY}, {pinWidth, pinHeight}, ports[i].portName, ports[i].portType);
                    }
                };

                drawPorts(inputPorts, leftPos, true);
                drawPorts(outputPorts, leftPos + blockSize.x, false);
            }
        }

        for (auto& block : graphModel.blocks()) {
            if (!block->view.has_value()) {
                auto blockId   = ax::NodeEditor::NodeId(block.get());
                auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
                block->view    = UiGraphBlock::ViewData{//
                       .x      = boundingBox.minX,      //
                       .y      = boundingBox.maxY,      //
                       .width  = blockSize[0],          //
                       .height = blockSize[1]};
                ax::NodeEditor::SetNodePosition(blockId, ImVec2(block->view->x, block->view->y));
                boundingBox.minX += blockSize[0] + padding.x;
            }
        }

        const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
        for (auto& edge : graphModel.edges()) {
            const auto sourceBlockId      = ax::NodeEditor::NodeId(edge.edgeSourcePort->ownerBlock);
            const auto destinationBlockId = ax::NodeEditor::NodeId(edge.edgeDestinationPort->ownerBlock);
            if (!std::any_of(filteredOutNodes.begin(), filteredOutNodes.end(), [&](const ax::NodeEditor::NodeId& nodeId) { //
                    return nodeId == sourceBlockId || nodeId == destinationBlockId;
                })) {
                ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), //
                    ax::NodeEditor::PinId(edge.edgeSourcePort),     //
                    ax::NodeEditor::PinId(edge.edgeDestinationPort), linkColor);
            }
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
                            message.endpoint = gr::scheduler::property::kEmplaceEdge;
                            message.data     = gr::property_map{                                   //
                                {"sourceBlock"s, outputPort->ownerBlock->blockUniqueName},     //
                                {"sourcePort"s, outputPort->portName},                         //
                                {"destinationBlock"s, inputPort->ownerBlock->blockUniqueName}, //
                                {"destinationPort"s, inputPort->portName},                     //
                                {"minBufferSize"s, gr::Size_t(4096)},                          //
                                {"weight"s, 1},                                                //
                                {"edgeName"s, std::string()}};
                            graphModel.sendMessage(std::move(message));
                        }
                    }
                }
            }
        }
    }
}

void FlowgraphPage::drawNodeEditor(const ImVec2& size) {

    const ImVec2 origCursorPos = ImGui::GetCursorScreenPos();
    const float  left          = ImGui::GetCursorPosX();
    const float  top           = ImGui::GetCursorPosY();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPaneContext.selectedBlock());

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    if (m_dashboard->graphModel().rearrangeBlocks()) {
        sortNodes(false);
    }

    auto originalFilterBlock = m_filterBlock;
    drawGraph(m_dashboard->graphModel(), size, m_filterBlock);

    // don't open properties pane if just clicking on the radio button
    const bool filterRadioPressed = originalFilterBlock != m_filterBlock;

    auto mouseDrag         = ImLengthSqr(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right));
    auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();

    if (!filterRadioPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouseDrag < 200 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();

        if (!block) {
            m_editPaneContext.setSelectedBlock(nullptr, nullptr);
        } else {
            m_editPaneContext.setSelectedBlock(block, &m_dashboard->graphModel());
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
            sortNodes(true);
        }
        if (ImGui::MenuItem("Refresh graph")) {
            m_dashboard->graphModel().requestGraphUpdate();
            m_dashboard->graphModel().requestAvailableBlocksTypesUpdate();
        }

        if (m_dashboard->scheduler()) {
            auto state = m_dashboard->scheduler()->state();

            if (state == gr::lifecycle::State::RUNNING) {
                if (ImGui::MenuItem("Pause scheduler")) {
                    m_dashboard->scheduler()->pause();
                }
                if (ImGui::MenuItem("Stop scheduler")) {
                    m_dashboard->scheduler()->stop();
                }

            } else if (state == gr::lifecycle::State::PAUSED) {
                if (ImGui::MenuItem("Resume scheduler")) {
                    m_dashboard->scheduler()->resume();
                }

            } else if (state == gr::lifecycle::State::STOPPED) {
                if (ImGui::MenuItem("Start scheduler")) {
                    m_dashboard->scheduler()->start();
                }
            }
        }
    }

    if (auto menu = IMW::Popup("block_ctx_menu", 0)) {
        if (m_dashboard->scheduler()) {
            if (ImGui::MenuItem("Delete this block")) {
                deleteBlock(m_selectedBlock->blockUniqueName);
            }

            auto typeParams = m_dashboard->graphModel().availableParametrizationsFor(m_selectedBlock->blockTypeName);
            if (typeParams.availableParametrizations) {
                if (typeParams.availableParametrizations->size() > 1) {
                    for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                        if (availableParametrization != typeParams.parametrization) {
                            auto name = std::string{"Change Type to "} + availableParametrization;
                            if (ImGui::MenuItem(name.c_str())) {
                                gr::Message message;
                                message.cmd      = gr::message::Command::Set;
                                message.endpoint = gr::scheduler::property::kReplaceBlock;
                                message.data     = gr::property_map{
                                        {"uniqueName"s, m_selectedBlock->blockUniqueName},                    //
                                        {"type"s, std::move(typeParams.baseType) + availableParametrization}, //
                                };
                                m_dashboard->graphModel().sendMessage(std::move(message));
                            }
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
                sortNodes(true);
            }
        }

        if (openNewBlockDialog) {
            m_newBlockSelector.open();
        }

        if (m_remoteSignalSelector) {
            if (openRemoteSignalSelector) {
                m_remoteSignalSelector->open();
            }

            for (const auto& selectedRemoteSignal : m_remoteSignalSelector->drawAndReturnSelected()) {
                m_dashboard->addRemoteSignal(selectedRemoteSignal);
            }
        }
        m_newBlockSelector.draw(m_dashboard->graphModel().knownBlockTypes);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        requestBlockControlsPanel(m_editPaneContext, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }
}

void FlowgraphPage::draw() noexcept {
    // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
    IMW::TabBar tabBar("maintabbar", 0);
    if (auto item = IMW::TabItem("Local", nullptr, 0)) {
        m_currentTabIsFlowGraph = true;
        auto contentRegion      = ImGui::GetContentRegionAvail();
        drawNodeEditor(contentRegion);
    }

    if (auto item = IMW::TabItem("Local - YAML", nullptr, 0)) {
        if (ImGui::Button("Reset") || m_currentTabIsFlowGraph) {
            // Reload yaml whenever "Local - YAML" tab is selected
            m_currentTabIsFlowGraph = false;

            gr::Message message;
            message.cmd      = gr::message::Command::Get;
            message.endpoint = gr::scheduler::property::kGraphGRC;
            m_dashboard->graphModel().sendMessage(std::move(message));
        }

        ImGui::SameLine();
        if (ImGui::Button("Apply")) {
            gr::Message message;
            message.cmd      = gr::message::Command::Set;
            message.endpoint = gr::scheduler::property::kGraphGRC;
            message.data     = gr::property_map{{"value", m_dashboard->graphModel().m_localFlowgraphGrc}};
            m_dashboard->graphModel().sendMessage(std::move(message));
        }

        ImGui::InputTextMultiline("##grc", &m_dashboard->graphModel().m_localFlowgraphGrc, ImGui::GetContentRegionAvail());
    }

    for (auto& s : m_dashboard->remoteServices()) {
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

void FlowgraphPage::sortNodes(bool all) {
    auto blockLevels = topologicalSort(m_dashboard->graphModel().blocks(), m_dashboard->graphModel().edges());

    constexpr float ySpacing = 32;
    constexpr float xSpacing = 200;

    float x = 0;
    for (auto& level : blockLevels) {
        float y          = 0;
        float levelWidth = 0;

        for (auto& block : level.blocks) {

            const auto blockId        = ax::NodeEditor::NodeId(block);
            const bool userPositioned = ax::NodeEditor::GetWasUserPositioned(blockId);
            if (all || !userPositioned) {
                ax::NodeEditor::SetNodePosition(blockId, ImVec2(x, y));
            }
            auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
            y += blockSize.y + ySpacing;
            levelWidth = std::max(levelWidth, blockSize.x);
        }

        x += levelWidth + xSpacing;
    }

    m_dashboard->graphModel().setRearrangedBlocks();
}

void FlowgraphPage::drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& name, const std::string& type, bool mainFlowGraph) {
    const auto& style = FlowgraphPage::styleForDataType(type);

    std::uint32_t alphaClearMask = 0x00ffffff;
    std::uint32_t alphaSetMask   = 0xff000000;
    if (ImGui::GetStyle().Alpha < 0.9f) {
        alphaSetMask = static_cast<std::uint32_t>(ImGui::GetStyle().Alpha * 255);
        alphaSetMask <<= 3 * 8;
    }

    const auto color = (style.color & alphaClearMask) | alphaSetMask;
    drawList->AddRectFilled(pinPosition, pinPosition + pinSize, color);
    drawList->AddRect(pinPosition, pinPosition + pinSize, darkenOrLighten(color));
    ImGui::SetCursorPos(pinPosition);

    if (ImGui::IsMouseHoveringRect(pinPosition, pinPosition + pinSize)) {
        // Node editor has very limited support for tooltips.
        // See imgui-node-editor/examples/widgets-example/widgets-example.cpp for workarounds
        // such as this one:

        if (mainFlowGraph) {
            ax::NodeEditor::Suspend();
        }

        ImGui::SetTooltip("%s (%s)", name.c_str(), type.c_str());

        if (mainFlowGraph) {
            ax::NodeEditor::Resume();
        }
    }
};

void FlowgraphPage::deleteBlock(const std::string& blockName) {
    // Send message to delete block
    gr::Message message;
    message.endpoint = gr::scheduler::property::kRemoveBlock;
    message.data     = gr::property_map{{"uniqueName"s, blockName}};
    m_dashboard->graphModel().sendMessage(std::move(message));
}

} // namespace DigitizerUi
