#include "FlowgraphPage.hpp"
#include "FlowgraphLayout.hpp"

#include <algorithm>
#include <exception>

#include <crude_json.h>
#include <cstdint>
#include <format>
#include <ranges>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"
#include "common/ImguiWrap.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <imgui_node_editor_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/DataTypeStyle.hpp"
#include "components/Splitter.hpp"
#include "components/YesNoPopup.hpp"

#include "utils/TransparentStringHash.hpp"

#include "scope_exit.hpp"

using namespace std::string_literals;

namespace {
[[nodiscard]] std::vector<DigitizerUi::UiGraphBlock*> nodeBlocks(const DigitizerUi::UiGraphBlock& root, bool withUiControls) {
    return root.childBlocks | std::views::filter([withUiControls](const auto& child) { return !child->isChart() && (withUiControls || !child->isUiControl()); }) | std::views::transform([](const auto& child) { return child.get(); }) | std::ranges::to<std::vector>();
}

bool isPortConnected(const DigitizerUi::UiGraphPort& port, const std::vector<DigitizerUi::UiGraphEdge>& edges) {
    return std::ranges::any_of(edges, [&port](const auto& edge) { //
        return edge.edgeSourcePort == &port || edge.edgeDestinationPort == &port;
    });
}

std::string simplerName(std::string_view rawName) {
    std::string result;
    result.reserve(rawName.size());

    const auto isIdentifierCharacter = [](const char c) {
        // whether this can be part of a c++ identifier
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    // remove namespace::identifiers by copying the string, and whenever we get
    // a ::, just erase backwards to the beginning of the identifier before the
    // ::
    for (size_t i = 0; i < rawName.size(); ++i) {
        if (i + 1 < rawName.size() && rawName[i] == ':' && rawName[i + 1] == ':') {
            while (!result.empty() && isIdentifierCharacter(result.back())) {
                result.pop_back();
            }
            ++i;
        } else {
            result += rawName[i];
        }
    }
    return result;
}
} // namespace

namespace DigitizerUi {

namespace {

auto displayedPorts(const UiGraphBlock& block, const std::vector<UiGraphPort>& ports) {
    auto result = ports | std::views::transform([](const auto& port) { return &port; }) | std::ranges::to<std::vector>();
    if (block.isScheduler() || block.isGraph()) {
        std::ranges::sort(result, {}, [](const auto* port) { return std::tie(port->portType, port->portName); });
    }
    return result;
}

} // namespace

void addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, const ImVec2& p, ImVec2 size) {
    const bool   input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2 min   = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2 max   = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 pivot = ImVec2(input ? min.x : max.x, (min.y + max.y) / 2.f);

    if (input) {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, 10);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_SnapLinkToPinDir, 1);
    }

    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(pivot, pivot);
    ax::NodeEditor::PinRect(min, max);
    ax::NodeEditor::EndPin();

    if (input) {
        ax::NodeEditor::PopStyleVar(3);
    }
};

std::string getDefaultExportedName(const UiGraphPort* port) { return std::format("{}.{}", port->ownerBlock ? port->ownerBlock->blockName : "UNKNOWN", port->portName); }

struct PinDrawInfo {
    ImVec2  topLeft;
    ImVec2  size;
    ImFont* font     = nullptr;
    float   fontSize = 0;
};

std::optional<std::string> exportedPortShortenedDisplayName(const UiGraphPort* port, const UiGraphBlock* exportedTo) {
    auto exportedName = port->getExportedName(exportedTo);

    if (!exportedName) {
        return exportedName;
    }

    std::string name;
    if (exportedName == getDefaultExportedName(port)) {
        name = port->portName;
    } else {
        name = *exportedName;
    }

    if (constexpr std::size_t maxDisplayNameLength = 10; name.size() > maxDisplayNameLength) {
        name = "..." + name.substr(name.size() - maxDisplayNameLength);
    }
    return name;
}

PinDrawInfo calculatePinDrawInfo(const std::optional<std::string>& displayName, std::size_t index, std::size_t numPins, float anchorX, float blockTopY, float blockHeight, bool isInput) {
    const auto& fg = LookAndFeel::instance().flowgraph;

    if (!displayName) {
        const float y = blockTopY + pinLocalPositionY(index, numPins, blockHeight, fg.pinHeight);
        const float x = isInput ? (anchorX - fg.pinHeight) : anchorX;
        return {.topLeft = {x, y}, .size = {fg.pinWidth, fg.pinHeight}};
    }

    auto* font = LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode];
    if (!font) {
        font = ImGui::GetFont();
    }
    const float fontSize = font->LegacySize;
    const auto  textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, displayName->c_str());
    const float tabW     = textSize.x + fg.exportedTabPaddingH * 2;
    const float tabH     = textSize.y + fg.exportedTabPaddingV * 2;
    const float tabY     = blockTopY + pinLocalPositionY(index, numPins, blockHeight, tabH);
    const float tabX     = isInput ? (anchorX + fg.exportedTabOverlap - tabW) : (anchorX - fg.exportedTabOverlap);

    return {.topLeft = {tabX, tabY}, .size = {tabW, tabH}, .font = font, .fontSize = fontSize};
}

std::string valToString(const gr::pmt::Value& val) {
    std::string out;
    gr::pmt::ValueVisitor([&]<typename TArg>(const TArg& arg) {
        using T = std::decay_t<TArg>;
        if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string>) {
            out = std::string(arg);
        } else if constexpr (std::same_as<T, bool>) {
            out = arg ? "true" : "false";
        } else if constexpr (std::integral<T> || std::floating_point<T>) {
            out = std::to_string(arg);
        } else {
            out.clear();
        }
    }).visit(val);
    return out;
}

namespace {
constexpr float kButtonBarPadding = 16.0f;
constexpr float kButtonBarHeight  = 37.0f;
} // namespace

FlowgraphEditor::Buttons FlowgraphEditor::drawButtons(const ImVec2& contentScreenTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio) {
    Buttons result;
    if (!(buttons.openNewBlockDialog || buttons.openNewSubGraphDialog || buttons.openRemoteSignalSelector || buttons.rearrangeBlocks || buttons.exportAllUnusedPorts || buttons.closeWindow)) {
        return result; // an overlay window without items is an ImGui error
    }

    IMW::PushCursorPosition _;

    constexpr float padding = kButtonBarPadding;
    constexpr float height  = kButtonBarHeight;

    {
        ImGui::SetNextWindowPos({contentScreenTopLeft.x, contentScreenTopLeft.y + contentSize.y - height - padding});
        ImGui::SetNextWindowSize({contentSize.x * (1 - horizontalSplitRatio), height});
        IMW::Window overlay("Button Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        // These Buttons are rendered on top of the Editor, to make them properly readable, take out the transparency
        ImVec4 buttonColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        buttonColor.w      = 1.0f;

        {
            IMW::StyleColor buttonStyle(ImGuiCol_Button, buttonColor);

            ImGui::SetCursorPosX(padding);

            if (buttons.openNewBlockDialog) {
                if (ImGui::Button("Add block...")) {
                    result.openNewBlockDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openNewSubGraphDialog) {
                if (ImGui::Button("Add sub graph...")) {
                    result.openNewSubGraphDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openRemoteSignalSelector) {
                if (ImGui::Button("Add remote signal...")) {
                    result.openRemoteSignalSelector = true;
                }
                ImGui::SameLine();
            }

            auto placeButtonRight = [posFromRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - padding](const char* text) mutable { //
                float width = ImGui::CalcTextSize(text).x;

                ImGui::SetCursorPosX(posFromRight - width);
                posFromRight -= width + padding;

                bool clicked = false;
                if (ImGui::Button(text)) {
                    clicked = true;
                }
                ImGui::SameLine();

                return clicked;
            };

            if (buttons.closeWindow) {
                result.closeWindow = placeButtonRight("Close");
            }

            if (buttons.exportAllUnusedPorts) {
                result.exportAllUnusedPorts = placeButtonRight("Export all unused ports");
            }

            if (buttons.rearrangeBlocks) {
                result.rearrangeBlocks = placeButtonRight("Rearrange blocks");
            }
        }
    }

    return result;
}

void FlowgraphEditor::drawComputeDomainTag(UiGraphBlock& block) {
    if (!block.ownerGraph) {
        assert(false && "scheduler {} had no owner graph");
        return;
    }
    std::string tagLabel = "Unmanaged graph";
    if (block.isScheduler()) {
        auto computeDomainIter = block.blockSettings.find("compute_domain");
        if (computeDomainIter == std::end(block.blockSettings)) {
            return;
        }
        auto computeDomain = gr::ComputeDomain::parse(computeDomainIter->second.value_or(std::string_view{}));
        if (computeDomain.kind.empty()) {
            return;
        }
        tagLabel = computeDomain.kind;
    }

    const auto blockId         = ax::NodeEditor::NodeId(std::addressof(block));
    const auto nodePosition    = ax::NodeEditor::GetNodePosition(blockId);
    const auto nodeSize        = ax::NodeEditor::GetNodeSize(blockId);
    const auto textRectPadding = ImGui::GetStyle().ItemInnerSpacing.x;
    const auto textSize        = ImGui::CalcTextSize(tagLabel.data(), tagLabel.data() + tagLabel.size());
    const auto topLeft         = nodePosition;
    const auto topRight        = nodePosition + ImVec2{nodeSize.x, 0.f};
    const auto availableSpace  = topRight.x - topLeft.x;
    if (textSize.x + (textRectPadding * 2.f) > availableSpace) {
        return;
    }

    const auto      lineHeight                   = ImGui::GetFrameHeight();
    constexpr float maxLabelSpaceFromLeft        = 30.f;
    constexpr float labelSpaceFromLeftPercentage = 0.2f;
    const auto      spacingLeft                  = std::min(availableSpace * labelSpaceFromLeftPercentage, maxLabelSpaceFromLeft);
    const auto      topLeftOfRect                = topLeft + ImVec2{spacingLeft, -(lineHeight / 2.f)};

    const auto  schedulerColorU32 = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorder);
    const auto  textColorU32      = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorderText);
    const auto* currentWindow     = ImGui::GetCurrentWindow();
    currentWindow->DrawList->AddRectFilled(topLeftOfRect, topLeftOfRect + textSize + ImVec2{textRectPadding * 2.f, 0.f}, schedulerColorU32);
    currentWindow->DrawList->AddText(topLeftOfRect + ImVec2{textRectPadding, 0.f}, textColorU32, tagLabel.data(), tagLabel.data() + tagLabel.size());
}

void FlowgraphEditor::drawBoundingBoxExterior(const BoundingBox& canvasSpacingBoundingBox) {
    constexpr static float fadeLengthSeconds = 1.0;
    const auto             alphaPercentage   = std::clamp(this->_timeSpentHoldingPin, 0.f, fadeLengthSeconds) / fadeLengthSeconds;

    // while in canvas space, ImGui::GetMousePos() returns the canvas space value and does not need to be converted
    if (!canvasSpacingBoundingBox.contains(ImGui::GetMousePos())) {
        this->_wasHoveringBoundingBoxExteriorThisFrame = true;
        if (this->_timeSpentHoldingPin < fadeLengthSeconds) { // accelerate quickly to the background being fully faded in
            this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime * 4;
        }
        // user dragging connection outside of bounding box.
        ax::NodeEditor::Suspend();
        if (auto tooltip = IMW::ToolTip{}) {
            ImGui::TextUnformatted("Release to export this pin...");
        }
        ax::NodeEditor::Resume();
    }

    // border hover fade animation
    auto          outlineAlpha       = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutline) * alphaPercentage);
    auto          outerAlpha         = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelection) * alphaPercentage);
    std::uint32_t outlineOpaqueColor = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutline);
    std::uint32_t outerOpaqueColor   = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelection);
    float         outlineThickness   = LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThickness;
    {
        auto       animPercentage = std::clamp(_timeSpentHoveringBoundingBoxExterior, 0.f, borderExteriorHoverFadeTransitionDurationSeconds) / borderExteriorHoverFadeTransitionDurationSeconds;
        const auto lerpColor      = [animPercentage](std::uint32_t coloru32, std::uint32_t targetu32) { //
            return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(coloru32), ImGui::ColorConvertU32ToFloat4(targetu32), animPercentage));
        };
        const auto castLerp = [animPercentage]<typename T>(T color, T target) { //
            return static_cast<T>(std::lerp(static_cast<float>(color), static_cast<float>(target), animPercentage));
        };

        // blend to hovered colors/alpha/outline thickness based on how long hovering exterior
        outlineOpaqueColor = lerpColor(outlineOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outerOpaqueColor   = lerpColor(outerOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outerAlpha         = castLerp(outerAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outlineAlpha       = castLerp(outlineAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outlineThickness   = castLerp(outlineThickness, LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered);
    }

    const auto outlineColor = rgbToImGuiABGR(outlineOpaqueColor, outlineAlpha);
    const auto outerColor   = rgbToImGuiABGR(outerOpaqueColor, outerAlpha);

    auto* const drawlist = ImGui::GetWindowDrawList();

    // consider "visible region" to be the whole window, expanding above the opendigitizer title bar and outside margins
    const auto visibleRegionMin = ax::NodeEditor::ScreenToCanvas(ImGui::GetWindowPos());
    const auto visibleRegionMax = ax::NodeEditor::ScreenToCanvas(ImGui::GetWindowPos() + ImGui::GetWindowSize());

    // draw some rectangles to demarcate the area to drag to do port exporting
    const auto& bb = canvasSpacingBoundingBox;
    drawlist->AddRect({bb.minX, bb.minY}, {bb.maxX, bb.maxY}, outlineColor, 0, ImDrawFlags_None, outlineThickness); // outline
    drawlist->AddRectFilled(visibleRegionMin, {bb.minX, visibleRegionMax.y}, outerColor);                           // left side column
    drawlist->AddRectFilled({bb.maxX, visibleRegionMin.y}, visibleRegionMax, outerColor);                           // right side column
    drawlist->AddRectFilled({bb.minX, visibleRegionMin.y}, {bb.maxX, bb.minY}, outerColor);                         // top middle between the two columns
    drawlist->AddRectFilled({bb.minX, bb.maxY}, {bb.maxX, visibleRegionMax.y}, outerColor);                         // bottom middle between the two columns
}

FlowgraphEditor::NodeDrawResult FlowgraphEditor::drawNode( //
    UiGraphBlock&                 block,                   //
    std::span<const UiGraphPort*> inputPorts,              //
    std::span<const UiGraphPort*> outputPorts,             //
    float                         pinHorizontalPadding     //
) {
    const auto            blockId        = ax::NodeEditor::NodeId(std::addressof(block));
    const auto            isGroup        = block.isScheduler() || block.isGraph();
    const ImVec4          borderColor    = isGroup ? LookAndFeel::instance().palette().flowgraphSubgraphBorder : ax::NodeEditor::GetStyle().Colors[ax::NodeEditor::StyleColor_NodeBorder];
    const auto            borderWidthVar = IMW::NodeEditor::StyleFloatVar(ax::NodeEditor::StyleVar_NodeBorderWidth, isGroup ? 3.0f : ax::NodeEditor::GetStyle().NodeBorderWidth);
    const auto            borderColorVar = IMW::NodeEditor::StyleColor(ax::NodeEditor::StyleColor_NodeBorder, borderColor);
    IMW::NodeEditor::Node node(blockId);

    if (isGroup) {
        this->drawComputeDomainTag(block);
    }

    const auto minimumBlockSize    = LookAndFeel::instance().flowgraph.minimumBlockSize;
    const auto blockScreenPosition = ImGui::GetCursorScreenPos();
    auto       blockBottomY{blockScreenPosition.y + minimumBlockSize.y}; // we have to keep track of the Node Size ourselves

    // Draw block title
    ImGui::TextUnformatted(simplerName(block.blockName).c_str());
    auto blockSize = ax::NodeEditor::GetNodeSize(blockId);

    // Draw block properties
    {
        IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
        for (const auto& [propertyKey, propertyValue] : block.blockSettings) {
            if (propertyKey == "description" || propertyKey.contains("::")) {
                continue;
            }

            const auto& currentPropertyMetaInformation = block.blockSettingsMetaInformation[std::string(propertyKey)];
            if (!currentPropertyMetaInformation.isVisible) {
                continue;
            }
            std::string value = valToString(propertyValue);
            ImGui::Text("%s: %s", currentPropertyMetaInformation.description.c_str(), value.c_str());
        }

        ImGui::Spacing();

        const bool isFilter = _filterBlock == std::addressof(block);

        // Make radio-button a bit smaller since we also made the properties smaller, looks huge otherwise
        IMW::StyleVar    styleVar(ImGuiStyleVar_FramePadding, GImGui->Style.FramePadding - ImVec2{0, 3});
        IMW::ChangeStrId changeId(block.blockUniqueName.c_str());

        if (ImGui::RadioButton("Filter", isFilter)) {
            if (isFilter) {
                _filterBlock = nullptr;
            } else {
                _filterBlock = std::addressof(block);
            }
        }
    }

    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    // Register ports with node editor, actual drawing comes later
    auto* exportTarget = exportPortTargetBlock();
    auto  registerPins = [exportTarget, &pinHorizontalPadding, &blockSize](auto& ports, auto position, auto pinType) {
        if (pinType == ax::NodeEditor::PinKind::Output) {
            position.x += blockSize.x - pinHorizontalPadding;
        }

        const float blockY  = position.y - ax::NodeEditor::GetStyle().NodePadding.y;
        const bool  isInput = pinType == ax::NodeEditor::PinKind::Input;

        for (std::size_t i = 0; i < ports.size(); ++i) {
            auto portDisplayName = exportedPortShortenedDisplayName(ports[i], exportTarget);
            auto info            = calculatePinDrawInfo(portDisplayName, i, ports.size(), position.x, blockY, blockSize.y, isInput);
            auto pinPos          = isInput ? ImVec2{info.topLeft.x + info.size.x, info.topLeft.y} : info.topLeft;
            addPin(ax::NodeEditor::PinId(ports[i]), pinType, pinPos, info.size);
        }
    };

    ImVec2 position = {blockScreenPosition.x - pinHorizontalPadding, blockScreenPosition.y};
    registerPins(inputPorts, position, ax::NodeEditor::PinKind::Input);
    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    registerPins(outputPorts, blockScreenPosition, ax::NodeEditor::PinKind::Output);
    blockBottomY = std::max(blockBottomY, ImGui::GetCursorPosY());

    ImGui::SetCursorScreenPos({position.x, blockBottomY});

    ImGui::Dummy(ImVec2(0.f, 0.f));
    return NodeDrawResult{position, blockBottomY};
}

void FlowgraphEditor::sendPinsConnectedGraphMessage(ax::NodeEditor::PinId startPinId, ax::NodeEditor::PinId endPinId) {
    // both are valid, let's accept link
    auto* startPort = startPinId.AsPointer<UiGraphPort>();
    auto* endPort   = endPinId.AsPointer<UiGraphPort>();

    if (startPort->portDirection == endPort->portDirection) {
        ax::NodeEditor::RejectNewItem();

    } else {
        // the pins arrive in drag order (start, end), so normalize by port direction
        auto* outputPort = startPort->portDirection == gr::PortDirection::OUTPUT ? startPort : endPort;
        auto* inputPort  = startPort->portDirection == gr::PortDirection::INPUT ? startPort : endPort;

        if (ax::NodeEditor::AcceptNewItem()) {
            // AcceptNewItem() return true when user release mouse button.
            gr::Message message;
            message.cmd      = gr::message::Command::Set;
            message.endpoint = gr::scheduler::property::kEmplaceEdge;
            auto owner       = ownersForRoot();
            if (!owner) {
                return;
            }
            message.serviceName = owner->scheduler;

            message.data = gr::property_map{                                                                                  //
                {"_targetGraph", owner->graph},                                                                               //
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), outputPort->ownerBlock->blockUniqueName},     //
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), outputPort->portName},                         //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), inputPort->ownerBlock->blockUniqueName}, //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), inputPort->portName},                     //
                {std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},                         //
                {std::pmr::string(gr::serialization_fields::EDGE_WEIGHT), 1},                                                 //
                {std::pmr::string(gr::serialization_fields::EDGE_NAME), "edge"}};

            // an exported input port cannot also have an internal edge, so unexport it before connecting
            if (const auto exportedName = inputPort->getExportedName(exportPortTargetBlock())) {
                ExportPortMessageData unexportMessage{
                    .uniqueBlockName = inputPort->ownerBlock->blockUniqueName,
                    .portDirection   = "input",
                    .portName        = inputPort->portName,
                    .exportedName    = "", // -Wmissing-designated-field-initializers
                    .exportFlag      = false,
                };

                if (hasExternalEdgesForExportedPort(*exportedName)) {
                    // defer to a confirmation popup, external edges would be disconnected
                    _popupEdgeConflict  = EdgeConflict::UnexportingPortHasExternalConnection;
                    unexportPortRequest = UnexportPortRequest{.message = std::move(unexportMessage), .exportedName = *exportedName, .thenEmplaceEdge = std::move(message)};
                    return;
                }
                requestExportPort(unexportMessage);
            }

            _graphModel->sendMessage(std::move(message));
        }
    }
}

void FlowgraphEditor::handlePinDrag(BoundingBox boundingBox, ImVec4 linkColor) {
    // Handle creation action, returns true if editor want to create new object (node or link)
    if (auto creation = IMW::NodeEditor::Creation(linkColor, 1.0f)) {
        // allow the user to drag outside the bounds of a subgraph to export a port
        if (_editorLevel > 0) {
            this->drawBoundingBoxExterior(boundingBox);
        }
        this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime;

        ax::NodeEditor::PinId startPinId, endPinId;
        if (ax::NodeEditor::QueryNewLink(&startPinId, &endPinId)) {
            // QueryNewLink returns true if editor wants to create new link between pins.
            //
            // Link can be created only for two valid pins, it is up to you to
            // validate if connection make sense. Editor is happy to make any.
            //
            // The pins are yielded in drag order (start, end), regardless of
            // their direction: the user may drag from either an input or an
            // output pin. The end pin is only valid once the user dragged the
            // link over another pin.
            if (startPinId && endPinId) {
                this->sendPinsConnectedGraphMessage(startPinId, endPinId);
            }
        }

        ax::NodeEditor::PinId heldPinId;
        if (_editorLevel > 0 && ax::NodeEditor::QueryNewNode(&heldPinId)) {
            auto* port = heldPinId.AsPointer<UiGraphPort>();
            assert(port);

            if (!port->isExportedTo(exportPortTargetBlock())) {
                _draggingPinExportRequest = ExportPortMessageData{
                    .uniqueBlockName = port->ownerBlock ? port->ownerBlock->blockUniqueName : "UNKNOWN"s,
                    .portDirection   = port->portDirection == gr::PortDirection::INPUT ? "input"s : "output"s,
                    .portName        = port->portName,
                    .exportedName    = (port->ownerBlock ? port->ownerBlock->blockName : "UNKNOWN"s) + "." + port->portName,
                    .exportFlag      = true,
                };
            }
        }
    } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (_draggingPinExportRequest && !boundingBox.contains(ImGui::GetMousePos())) {
            if (internalEdgeForInputPort(*_draggingPinExportRequest)) {
                // defer to a confirmation popup, an exported input port cannot also have an internal edge
                _popupEdgeConflict    = EdgeConflict::InputHasInternalConnection;
                exportConflictRequest = *_draggingPinExportRequest;
            } else {
                requestExportPort(*_draggingPinExportRequest);
            }
        }
        _draggingPinExportRequest  = {};
        this->_timeSpentHoldingPin = 0.f;
    }
}

void FlowgraphEditor::drawGraph(const ImVec2& size /*, const UiGraphBlock*& filterBlock*/) {
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        return;
    }

    const auto* exportTarget = exportPortTargetBlock();

    const std::vector<UiGraphBlock*> graphBlocks = nodeBlocks(*rootBlock, showUiControlBlocks);
    const auto&                      graphEdges  = rootBlock->childEdges;

    makeCurrent();
    dropReferencesToDeletedBlocks();

    for (const auto& block : graphBlocks) {
        if (block->storedXY) {
            ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(block), {block->storedXY->x, block->storedXY->y});
        } else if (_firstDraw) {
            _rearrangeRequested = true;
        }
    }
    if (!graphBlocks.empty()) {
        _firstDraw = false;
    }

    // NodeEditor applies dragging when its Editor scope ends.
    Digitizer::utils::scope_exit capturePositions = [&] {
        for (const auto& block : graphBlocks) {
            const auto position = ax::NodeEditor::GetNodePosition(ax::NodeEditor::NodeId(block));
            block->storedXY     = UiGraphBlock::StoredXY{position.x, position.y};
        }
    };
    const int                    nHiddenBorderColours = LookAndFeel::instance().flowgraph.canvasBorder ? 0 : 2;
    Digitizer::utils::scope_exit showBorderAgain      = [nHiddenBorderColours] { ImGui::PopStyleColor(nHiddenBorderColours); };
    IMW::NodeEditor::Editor      nodeEditor(_editorName.c_str(), size);
    Digitizer::utils::scope_exit hideBorder = [nHiddenBorderColours] {
        for (const ImGuiCol colour : {ImGuiCol_Border, ImGuiCol_BorderShadow}) {
            if (nHiddenBorderColours > 0) {
                ImGui::PushStyleColor(colour, ImVec4{});
            }
        }
    };
    const auto padding = ax::NodeEditor::GetStyle().NodePadding;

    std::optional<BoundingBox> boundingBox;
    const auto                 addRectangleToBoundingBox = [&boundingBox](ImVec2 rectPosition, ImVec2 rectSize) {
        if (boundingBox.has_value()) {
            boundingBox->addRectangle(rectPosition, rectSize);
        } else {
            boundingBox = BoundingBox{
                                .minX = rectPosition.x,
                                .minY = rectPosition.y,
                                .maxX = rectPosition.x + rectSize.x,
                                .maxY = rectPosition.y + rectSize.y,
            };
        }
    };

    // to save result of expensive recursion
    std::vector<ax::NodeEditor::NodeId> filteredOutNodes;

    // over-reserved, but minimizes allocations
    filteredOutNodes.reserve(_filterBlock ? graphBlocks.size() : 0);

    // Draw every block before measuring and arranging.
    for (auto& block : graphBlocks) {
        const auto blockId     = ax::NodeEditor::NodeId(block);
        auto       inputPorts  = displayedPorts(*block, block->inputPorts());
        auto       outputPorts = displayedPorts(*block, block->outputPorts());

        const bool filteredOut = _filterBlock && !_graphModel->blockInTree(*block, *_filterBlock);

        // If filteredOut, set opacity to 25% until we exit the scope
        float                        originalAlpha = std::exchange(ImGui::GetStyle().Alpha, (filteredOut ? 0.25f : ImGui::GetStyle().Alpha));
        Digitizer::utils::scope_exit restoreStyle  = [&] { ImGui::GetStyle().Alpha = originalAlpha; };

        if (filteredOut) {
            filteredOutNodes.push_back(blockId);
        }

        const auto blockPosition = this->drawNode(*block, inputPorts, outputPorts, padding.x);
        const auto blockSize     = ax::NodeEditor::GetNodeSize(blockId);

        // Update bounding box
        if (block->storedXY) {
            addRectangleToBoundingBox(ax::NodeEditor::GetNodePosition(blockId), blockSize);
        }

        // The input/output pins are drawn after ending the node because otherwise
        // drawing them would increase the node size, which we need to know to correctly place the
        // output pins, and that would cause the nodes to continuously grow in width
        {
            auto leftPos = blockPosition.topLeft.x - padding.x;

            ImGui::SetCursorScreenPos(blockPosition.topLeft);
            auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(blockId);

            auto drawPorts = [&](auto& ports, auto portLeftPos, bool isInput) {
                const auto& fg        = LookAndFeel::instance().flowgraph;
                const float anchorX   = portLeftPos + padding.x;
                const float blockTopY = blockPosition.topLeft.y - ax::NodeEditor::GetStyle().NodePadding.y;

                for (std::size_t i = 0; i < ports.size(); ++i) {
                    auto portExportedDisplayName = exportedPortShortenedDisplayName(ports[i], exportTarget);
                    auto info                    = calculatePinDrawInfo(portExportedDisplayName, i, ports.size(), anchorX, blockTopY, blockSize.y, isInput);

                    if (!portExportedDisplayName) {
                        if (drawPin(drawList, info.topLeft, info.size, ports[i]->portType)) {
                            // node editor tooltips need a suspended canvas, see imgui-node-editor/examples/widgets-example
                            ax::NodeEditor::Suspend();
                            ImGui::SetTooltip("%s (%s)", ports[i]->portName.c_str(), ports[i]->portType.c_str());
                            ax::NodeEditor::Resume();
                        }
                        continue;
                    }

                    const auto& typeStyle = styleForDataType(ports[i]->portType);
                    const auto  textColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphBg);
                    drawList->AddRectFilled(info.topLeft, info.topLeft + info.size, typeStyle.color);
                    drawList->AddRect(info.topLeft, info.topLeft + info.size, darkenOrLighten(typeStyle.color));
                    drawList->AddText(info.font, info.fontSize, ImVec2{info.topLeft.x + fg.exportedTabPaddingH, info.topLeft.y + fg.exportedTabPaddingV}, textColor, portExportedDisplayName->c_str());
                }
            };

            drawPorts(inputPorts, leftPos, true);
            drawPorts(outputPorts, leftPos + blockSize.x, false);
        }
    }

    for (auto& block : graphBlocks) {
        if (!block->storedXY) {
            const auto   bounds = boundingBox.value_or(defaultBoundingBox);
            const ImVec2 position{bounds.minX, bounds.maxY + 32.f};
            const auto   blockId = ax::NodeEditor::NodeId(block);
            ax::NodeEditor::SetNodePosition(blockId, position);
            addRectangleToBoundingBox(position, ax::NodeEditor::GetNodeSize(blockId));
        }
    }

    // the node editor restores the previous view when resized: a fit followed by a resize is repeated
    const ImVec2 canvasSize    = ax::NodeEditor::GetScreenSize();
    const bool   canvasResized = canvasSize.x != _lastCanvasSize.x || canvasSize.y != _lastCanvasSize.y;
    _lastCanvasSize            = canvasSize;
    if (std::exchange(_fitJustApplied, false) && canvasResized) {
        _fitRequested = true;
    }

    // Arrange only after drawing has measured every block.
    if (std::exchange(_rearrangeRequested, false)) {
        sortNodes(rootBlock);
        _fitRequested = true;
    } else if (_fitRequested) {
        _fitRequested = false;
        fitIntoView(*rootBlock, showEditorControls || closeRequestedCallback ? kButtonBarHeight + kButtonBarPadding : 0.f);
        _fitJustApplied = true;
    }

    const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];
    for (auto& edge : graphEdges) {
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

    // fade out the bounding box effect. though handlePinDrag() may call drawBoundingBoxExterior() and stop this from happening
    this->_wasHoveringBoundingBoxExteriorThisFrame = false;

    this->handlePinDrag(
        [&boundingBox]() {
            constexpr static std::size_t marginPixels = 10;
            auto                         out          = boundingBox.value_or(defaultBoundingBox);
            out.minX -= marginPixels;
            out.minY -= marginPixels;
            out.maxX += marginPixels;
            out.maxY += marginPixels;
            return out;
        }(),
        linkColor);

    // play fade animation for hovering outside the bounding box
    if (this->_wasHoveringBoundingBoxExteriorThisFrame) {
        if (this->_timeSpentHoveringBoundingBoxExterior < borderExteriorHoverFadeTransitionDurationSeconds) {
            this->_timeSpentHoveringBoundingBoxExterior += ImGui::GetIO().DeltaTime;
        }
    } else if (this->_timeSpentHoveringBoundingBoxExterior > 0) {
        this->_timeSpentHoveringBoundingBoxExterior -= ImGui::GetIO().DeltaTime;
    }
}

void FlowgraphEditor::draw(const ImVec2& contentTopLeft, const ImVec2& contentSize, bool isCurrentEditor) {
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        // maybe the graph doesn't exist yet, can happen after exchanging the graph + we still have an out of date root block name from the old graph
        auto* exportTarget  = exportPortTargetBlock();
        auto* schedulerInfo = exportTarget ? std::get_if<UiGraphBlock::SchedulerBlockInfo>(&exportTarget->blockCategoryInfo) : nullptr;
        if (schedulerInfo && schedulerInfo->childrenLoaded && !exportTarget->childBlocks.empty()) {
            _rootBlockUniqueName = exportTarget->childBlocks.front()->blockUniqueName;
            rootBlock            = exportTarget->childBlocks.front().get();
            _firstDraw           = true;
        } else {
            return;
        }
    }

    makeCurrent();

    IMW::PushCursorPosition origCursorPos;

    ImGui::SetCursorPos(contentTopLeft);
    const ImVec2 contentScreenTopLeft = ImGui::GetCursorScreenPos(); // SetNextWindowPos takes screen coordinates

    if (!isCurrentEditor) {
        // If this is not the enabled (top level editor) just draw the
        // flowgraph and no UI controls
        IMW::Disabled enableOnlyCurrent(!isCurrentEditor);
        drawGraph(contentSize);
        return;
    }

    const bool      horizontalSplit   = contentSize.x > contentSize.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(_splitter, contentSize, horizontalSplit, splitterWidth, 0.2f, !_editPaneContext.selectedBlock());

    const auto clicked = drawButtons(contentScreenTopLeft, contentSize,
        {
            .openNewBlockDialog       = showEditorControls && openNewBlockSelectorCallback,
            .openNewSubGraphDialog    = showEditorControls && openNewSubGraphSelectorCallback,
            .openRemoteSignalSelector = showEditorControls && openAddRemoteSignalCallback,
            .rearrangeBlocks          = showEditorControls,
            .exportAllUnusedPorts     = showEditorControls && _editorLevel > 0,
            .closeWindow              = static_cast<bool>(closeRequestedCallback),
        },
        horizontalSplit ? (ratio) : 1.0f);

    if (clicked.rearrangeBlocks) {
        _rearrangeRequested = true;
    }

    if (clicked.exportAllUnusedPorts) {
        exportAllUnusedPorts();
    }

    if (clicked.closeWindow && closeRequestedCallback) {
        closeRequestedCallback();
        return;
    }

    if (openNewBlockSelectorCallback && clicked.openNewBlockDialog) {
        openNewBlockSelectorCallback(_graphModel);
    }

    if (openNewSubGraphSelectorCallback && clicked.openNewSubGraphDialog) {
        openNewSubGraphSelectorCallback(_graphModel);
    }

    if (openAddRemoteSignalCallback && clicked.openRemoteSignalSelector) {
        openAddRemoteSignalCallback(_graphModel);
    }

    if (exportPortRequest) {
        bool shouldShowExportPortPopup = true;
        ImGui::OpenPopup("Exported port name");
        if (ImGui::BeginPopupModal("Exported port name", &shouldShowExportPortPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter the name the port will be visible as:");
            ImGui::InputText("##Input", &exportPortTextField);

            {
                IMW::Disabled _(exportPortTextField.empty());
                if (ImGui::Button("OK")) {
                    exportPortRequest->exportedName = exportPortTextField;
                    requestExportPort(*exportPortRequest);

                    exportPortRequest.reset();
                    exportPortTextField.clear();
                    shouldShowExportPortPopup = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                exportPortRequest.reset();
                exportPortTextField.clear();
                shouldShowExportPortPopup = false;
            }
            ImGui::EndPopup();
        }
    }

    constexpr static const char* edgeConflictPopupId = "##Edge conflict";
    if (unexportPortRequest || exportConflictRequest) {
        ImGui::OpenPopup(edgeConflictPopupId);

        using namespace components;
        const YesNoPopupOptions popupOptions = [this] {
            switch (_popupEdgeConflict) {
            case EdgeConflict::InputHasInternalConnection:
                return YesNoPopupOptions{
                    .yesText   = "Disconnect internal edge before exporting",
                    .noText    = "Cancel exporting",
                    .titleText = "An input port can only have one connection",
                };
            case EdgeConflict::UnexportingPortHasExternalConnection:
                return YesNoPopupOptions{
                    .yesText   = "Yes, unexport and disconnect all",
                    .noText    = "Cancel and keep external connections",
                    .titleText = "Unexporting port will disconnect external edges. Are you sure?",
                };
            }
            std::unreachable();
        }();

        const auto popupResult = beginYesNoPopup(edgeConflictPopupId, popupOptions, ImGuiWindowFlags_AlwaysAutoResize);
        if (isPopupConfirmed(popupResult)) {
            if (unexportPortRequest) {
                for (const UiGraphEdge* externalEdge : externalEdgesForExportedPort(unexportPortRequest->exportedName)) {
                    requestEdgeRemoval(*externalEdge);
                }
                requestExportPort(unexportPortRequest->message);
                if (unexportPortRequest->thenEmplaceEdge) {
                    _graphModel->sendMessage(std::move(*unexportPortRequest->thenEmplaceEdge));
                }
            }
            if (exportConflictRequest) {
                if (const UiGraphEdge* internalEdge = internalEdgeForInputPort(*exportConflictRequest)) {
                    requestEdgeRemoval(*internalEdge);
                }
                if (exportConflictRequest->exportedName.empty()) {
                    exportPortRequest = std::move(*exportConflictRequest); // ask for the exported name next
                } else {
                    requestExportPort(*exportConflictRequest);
                }
            }
        }
        if (isPopupOpen(popupResult)) {
            ImGui::EndPopup();
        }

        if (!ImGui::IsPopupOpen(edgeConflictPopupId)) {
            // popup closed, so the user's request is either cancelled or applied
            unexportPortRequest.reset();
            exportConflictRequest.reset();
        }
    }

    auto originalFilterBlock = _filterBlock;
    drawGraph(contentSize);

    // don't open properties pane if just clicking on the radio button
    const bool filterRadioPressed = originalFilterBlock != _filterBlock;

    const auto mouseDrag         = ImLengthSqr(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right));
    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();

    if (!filterRadioPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouseDrag < 200 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();

        if (!block) {
            _editPaneContext.setSelectedBlock(nullptr, nullptr);
        } else if (auto ownerNames = ownersForRoot()) {
            _editPaneContext.targetGraph = ownerNames->graph;
            _editPaneContext.setSelectedBlock(block, _graphModel);
            _editPaneContext.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
        }
    }

    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        auto n     = ax::NodeEditor::GetDoubleClickedNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block && (block->isGraph() || block->isScheduler())) {
            requestGraphEdit(block);
        }
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();
        if (block) {
            ImGui::OpenPopup("block_ctx_menu");
            _selectedBlock = block;
        }
    }

    if (backgroundClicked == ImGuiMouseButton_Right && mouseDrag < 200) {
        ImGui::OpenPopup("ctx_menu");
        _contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    }

    if (auto menu = IMW::Popup("ctx_menu", 0)) {
        if (ImGui::MenuItem("Refresh graph")) {
            _graphModel->requestFullUpdate();
            _graphModel->requestAvailableBlocksTypesUpdate();
        }

        drawGroupingMenuItems(selectedBlockUniqueNames());
    }

    drawBlockContextMenu();

    if (_pendingGroupBlocksRequest) {
        if (openGroupBlocksSelectorCallback) {
            openGroupBlocksSelectorCallback(std::move(*_pendingGroupBlocksRequest));
        }
        _pendingGroupBlocksRequest.reset();
    }

    if (!requestBlockControlsPanel) {
        return;
    }
    if (horizontalSplit) {
        const float w = contentSize.x * ratio;
        requestBlockControlsPanel(_editPaneContext, {contentScreenTopLeft.x + contentSize.x - w + halfSplitterWidth, contentScreenTopLeft.y}, {w - halfSplitterWidth, contentSize.y}, true);
    } else {
        const float h = contentSize.y * ratio;
        requestBlockControlsPanel(_editPaneContext, {contentScreenTopLeft.x, contentScreenTopLeft.y + contentSize.y - h + halfSplitterWidth}, {contentSize.x, h - halfSplitterWidth}, false);
    }
}

void FlowgraphEditor::drawPortsMenu(const char* text, const char* portDirection, const auto& blockPorts) {
    if (blockPorts.empty()) {
        return;
    }

    const auto* exportTarget = exportPortTargetBlock();
    if (!exportTarget) {
        return; // probably in the root graph. in any case there is nowhere to export to, so this menu would be useless
    }

    auto portsSubMenu = IMW::Menu{text, /*enabled*/ true};
    if (!portsSubMenu) {
        return;
    }

    for (const UiGraphPort& port : blockPorts) {
        bool exported = port.isExportedTo(exportTarget);

        // only one item in the menu: a checkbox to toggle whether exported
        if (!ImGui::Checkbox(std::format("{}##{}-{}", port.portName, port.ownerBlock->blockUniqueName, port.portName).c_str(), &exported)) {
            continue;
        }

        if (exported) {
            ExportPortMessageData exportMessage{
                .uniqueBlockName = _selectedBlock->blockUniqueName,
                .portDirection   = portDirection,
                .portName        = port.portName,
                .exportedName    = "", // -Wmissing-designated-field-initializers
                .exportFlag      = true,
            };
            exportPortTextField = getDefaultExportedName(&port);

            if (internalEdgeForInputPort(exportMessage)) {
                // defer to a confirmation popup, an exported input port cannot also have an internal edge
                _popupEdgeConflict    = EdgeConflict::InputHasInternalConnection;
                exportConflictRequest = std::move(exportMessage);
            } else {
                exportPortRequest = std::move(exportMessage);
            }
            continue;
        }

        ExportPortMessageData unexportMessage{
            .uniqueBlockName = _selectedBlock->blockUniqueName,
            .portDirection   = portDirection,
            .portName        = port.portName,
            .exportedName    = "", // -Wmissing-designated-field-initializers
            .exportFlag      = false,
        };

        if (const auto exportedName = port.getExportedName(exportTarget); exportedName && hasExternalEdgesForExportedPort(*exportedName)) {
            // defer to a confirmation popup, external edges would be disconnected
            _popupEdgeConflict  = EdgeConflict::UnexportingPortHasExternalConnection;
            unexportPortRequest = UnexportPortRequest{.message = std::move(unexportMessage), .exportedName = *exportedName};
        } else {
            requestExportPort(unexportMessage);
        }
    }
}

void FlowgraphEditor::drawBlockContextMenu() {
    auto menu = IMW::Popup("block_ctx_menu", 0);
    if (!menu) {
        return;
    }

    if (_selectedBlock == nullptr) {
        // the block was deleted while the menu was open
        ImGui::CloseCurrentPopup();
        return;
    }

    if (ImGui::MenuItem("Delete this block")) {
        requestBlockDeletion(_selectedBlock->blockUniqueName);
    }

    if (_selectedBlock->blockCategory == "TransparentBlockGroup" || _selectedBlock->blockCategory == "ScheduledBlockGroup") {
        if (ImGui::MenuItem("Edit block graph...")) {
            requestGraphEdit(_selectedBlock);
        }
    }

    auto groupNames = selectedBlockUniqueNames();
    if (std::ranges::find(groupNames, _selectedBlock->blockUniqueName) == groupNames.end()) {
        groupNames.push_back(_selectedBlock->blockUniqueName);
    }

    if ((_selectedBlock->isGraph() || _selectedBlock->isScheduler()) && ImGui::MenuItem("Ungroup blocks")) {
        requestBlocksUngrouping(_selectedBlock->blockUniqueName);
    }

    drawGroupingMenuItems(std::move(groupNames));

    auto typeParams = _graphModel->availableParametrizationsFor(_selectedBlock->blockTypeName);
    if (typeParams.availableParametrizations && typeParams.availableParametrizations->size() > 1) {
        if (IMW::Menu blockTypesMenu("Change type to...", /*enabled*/ true); blockTypesMenu) {
            for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                if (availableParametrization != typeParams.parametrization && ImGui::MenuItem(availableParametrization.c_str())) {
                    if (auto owner = ownersForRoot()) {
                        gr::Message message;
                        message.cmd         = gr::message::Command::Set;
                        message.endpoint    = gr::scheduler::property::kReplaceBlock;
                        message.serviceName = owner->scheduler;
                        message.data        = gr::property_map{                                         //
                            {"uniqueName", _selectedBlock->blockUniqueName},                     //
                            {"type", std::move(typeParams.baseType) + availableParametrization}, //
                            {"_targetGraph", owner->graph}};

                        _graphModel->sendMessage(std::move(message));
                    }
                }
            }
        }
    }

    if (_editorLevel > 0) {
        this->drawPortsMenu("Exported input ports...", "input", _selectedBlock->_inputPorts);
        this->drawPortsMenu("Exported output ports...", "output", _selectedBlock->_outputPorts);
    }
}

void FlowgraphEditor::sortNodes(UiGraphBlock* rootBlock) const {
    const std::vector<UiGraphBlock*> blocks = nodeBlocks(*rootBlock, showUiControlBlocks);

    std::vector<flowgraph_layout::Size> nodeSizes;
    nodeSizes.reserve(blocks.size());
    struct PortLocation {
        std::size_t nodeIndex;
        float       y;
    };
    std::unordered_map<const UiGraphPort*, PortLocation> portLocations;
    for (std::size_t i = 0UZ; i < blocks.size(); ++i) {
        auto&      block     = *blocks[i];
        const auto blockId   = ax::NodeEditor::NodeId(&block);
        const auto blockSize = ax::NodeEditor::GetNodeSize(blockId);
        nodeSizes.push_back({.width = std::max(blockSize.x, 80.f), .height = std::max(blockSize.y, 40.f)});
        const auto addPortLocations = [&](const auto& ports) {
            const auto orderedPorts = displayedPorts(block, ports);
            for (std::size_t port = 0UZ; port < orderedPorts.size(); ++port) {
                portLocations.emplace(orderedPorts[port], PortLocation{i, pinLocalPositionY(port, orderedPorts.size(), blockSize.y, 0.f)});
            }
        };
        addPortLocations(block.inputPorts());
        addPortLocations(block.outputPorts());
    }

    std::vector<flowgraph_layout::Edge> layoutEdges;
    layoutEdges.reserve(rootBlock->childEdges.size());
    for (const auto& edge : rootBlock->childEdges) {
        const auto source = portLocations.find(edge.edgeSourcePort);
        const auto target = portLocations.find(edge.edgeDestinationPort);
        if (source == portLocations.end() || target == portLocations.end()) {
            continue;
        }

        layoutEdges.push_back({
            .source      = source->second.nodeIndex,
            .target      = target->second.nodeIndex,
            .sourcePortY = source->second.y,
            .targetPortY = target->second.y,
        });
    }

    const auto positions = flowgraph_layout::compute(nodeSizes, layoutEdges);

    for (std::size_t i = 0UZ; i < blocks.size(); ++i) {
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(blocks[i]), ImVec2(positions[i].x, positions[i].y));
    }
}

void FlowgraphEditor::fitIntoView(const UiGraphBlock& rootBlock, float reservedBottomPixels) const {
    const std::vector<UiGraphBlock*> blocks = nodeBlocks(rootBlock, showUiControlBlocks);
    if (blocks.empty()) {
        return;
    }
    ImRect bounds(ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), ImVec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
    for (UiGraphBlock* block : blocks) {
        const auto blockId  = ax::NodeEditor::NodeId(block);
        const auto position = ax::NodeEditor::GetNodePosition(blockId);
        bounds.Add(ImRect(position, position + ax::NodeEditor::GetNodeSize(blockId)));
    }

    // NavigateTo widens the rect by half of this fraction of its larger dimension per side
    constexpr float kNavigationZoomMargin = 0.1f;

    const ImVec2 viewSize      = ax::NodeEditor::GetScreenSize();
    const float  usableHeight  = std::max(1.f, viewSize.y - reservedBottomPixels);
    const float  contentMargin = std::max(bounds.GetWidth(), bounds.GetHeight()) * kNavigationZoomMargin * 0.5f;
    const float  zoom          = std::min({1.f, viewSize.x / (bounds.GetWidth() + 2.f * contentMargin), usableHeight / (bounds.GetHeight() + 2.f * contentMargin)});

    const ImVec2 visibleSize = viewSize / zoom;
    const float  visibleTop  = bounds.GetCenter().y - 0.5f * usableHeight / zoom;
    const ImRect visible(ImVec2(bounds.GetCenter().x - 0.5f * visibleSize.x, visibleTop), ImVec2(bounds.GetCenter().x + 0.5f * visibleSize.x, visibleTop + visibleSize.y));

    const float viewMargin = std::max(visibleSize.x, visibleSize.y) * kNavigationZoomMargin / (2.f * (1.f + kNavigationZoomMargin));
    auto*       editor     = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ax::NodeEditor::GetCurrentEditor());
    editor->NavigateTo(ImRect(visible.Min + ImVec2(viewMargin, viewMargin), visible.Max - ImVec2(viewMargin, viewMargin)), true);
}

void FlowgraphEditor::requestBlockDeletion(const std::string& blockName) {
    // Send message to delete block
    if (auto owner = ownersForRoot()) {
        gr::Message message;
        message.endpoint    = gr::scheduler::property::kRemoveBlock;
        message.serviceName = owner->scheduler;
        message.data        = gr::property_map{//
            {"uniqueName", blockName},  //
            {"_targetGraph", owner->graph}};
        _graphModel->sendMessage(std::move(message));
    }
}

void FlowgraphEditor::requestBlocksGrouping(std::string graphType, const std::vector<std::string>& uniqueNames) {
    if (auto owner = ownersForRoot()) {
        gr::Tensor<gr::pmt::Value> names(gr::extents_from, {uniqueNames.size()});
        for (std::size_t i = 0; i < uniqueNames.size(); ++i) {
            names[i] = uniqueNames[i];
        }

        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kGroupBlocks;
        message.serviceName = owner->scheduler;
        message.data        = gr::property_map{       //
            {"type", std::move(graphType)},    //
            {"uniqueNames", std::move(names)}, //
            {"_targetGraph", owner->graph}};
        _graphModel->sendMessage(std::move(message));
    }
}

void FlowgraphEditor::requestBlocksUngrouping(const std::string& uniqueName) {
    if (auto owner = ownersForRoot()) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kUngroupBlocks;
        message.serviceName = owner->scheduler;
        message.data        = gr::property_map{{"uniqueName", uniqueName}, {"_targetGraph", owner->graph}};
        _graphModel->sendMessage(std::move(message));
    }
}

void FlowgraphEditor::dropReferencesToDeletedBlocks() {
    if (_seenBlockDestructionCount == _graphModel->blockDestructionCount) {
        return;
    }
    _seenBlockDestructionCount = _graphModel->blockDestructionCount;

    // unfortunately we must always clear the selection if any block has been
    // destroyed, because we do not have a way to identify whether a given
    // NodeId points to a now-freed block. Could be solved by smart pointers
    // or generation/index handles
    makeCurrent();
    ax::NodeEditor::ClearSelection();

    const auto isLiveChild = [this](const UiGraphBlock* block) { //
        if (auto* rootBlock = this->rootBlock()) {
            return std::ranges::contains(rootBlock->childBlocks, block, [](const auto& child) { return child.get(); });
        }
        return false;
    };
    if (_selectedBlock != nullptr && !isLiveChild(_selectedBlock)) {
        _selectedBlock = nullptr;
    }
    if (_filterBlock != nullptr && !isLiveChild(_filterBlock)) {
        _filterBlock = nullptr;
    }
}

std::vector<std::string> FlowgraphEditor::selectedBlockUniqueNames() {
    makeCurrent();
    dropReferencesToDeletedBlocks();
    std::vector<ax::NodeEditor::NodeId> selectedNodes(static_cast<std::size_t>(ax::NodeEditor::GetSelectedObjectCount()));
    const auto                          nodeCount = ax::NodeEditor::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(nodeCount));
    for (const auto& nodeId : selectedNodes | std::views::take(nodeCount)) {
        if (const auto* block = nodeId.AsPointer<UiGraphBlock>()) {
            result.push_back(block->blockUniqueName);
        }
    }
    return result;
}

void FlowgraphEditor::drawGroupingMenuItems(std::vector<std::string> uniqueNames) {
    const bool haveSelection = !uniqueNames.empty();
    const bool singular      = uniqueNames.size() == 1;
    if (ImGui::MenuItem(singular ? "Group block" : "Group blocks", nullptr, false, haveSelection)) {
        requestBlocksGrouping("gr::Graph", uniqueNames);
    }
    if (ImGui::MenuItem(singular ? "Group block and pick graph type..." : "Group blocks and pick graph type...", nullptr, false, haveSelection)) {
        _pendingGroupBlocksRequest = std::move(uniqueNames);
    }
}

void FlowgraphEditor::requestExportPort(const ExportPortMessageData& request) {
    gr::Message message;

    message.cmd         = gr::message::Command::Set;
    message.endpoint    = gr::graph::property::kSubgraphExportPort;
    message.serviceName = _exportPortTargetBlockUniqueName;
    message.data        = gr::property_map{                  //
        {"uniqueBlockName", request.uniqueBlockName}, //
        {"portDirection", request.portDirection},     //
        {"portName", request.portName},               //
        {"exportedName", request.exportedName},       //
        {"exportFlag", request.exportFlag}};
    graphModel()->sendMessage(std::move(message));
}

std::vector<const UiGraphEdge*> FlowgraphEditor::externalEdgesForExportedPort(const std::string& exportedName) const {
    const auto* exportTarget = exportPortTargetBlock();
    const auto* parentGraph  = exportTarget ? exportTarget->parentBlock : nullptr;
    if (!parentGraph) {
        return {};
    }

    const auto isExternalEdge = [exportTarget, &exportedName](const UiGraphEdge& edge) {
        const auto matches = [&](const UiGraphPort* port) { return port && port->ownerBlock == exportTarget && port->portName == exportedName; };
        return matches(edge.edgeSourcePort) || matches(edge.edgeDestinationPort);
    };
    return parentGraph->childEdges | std::views::filter(isExternalEdge) | std::views::transform([](const UiGraphEdge& edge) { return &edge; }) | std::ranges::to<std::vector>();
}

const UiGraphEdge* FlowgraphEditor::internalEdgeForInputPort(const ExportPortMessageData& request) const {
    if (request.portDirection != "input") {
        return nullptr;
    }
    const auto* root = rootBlock();
    if (!root) {
        return nullptr;
    }

    const auto it = std::ranges::find_if(root->childEdges, [&request](const UiGraphEdge& edge) {
        return edge.edgeDestinationPort && edge.edgeDestinationPort->ownerBlock &&                 //
               edge.edgeDestinationPort->ownerBlock->blockUniqueName == request.uniqueBlockName && //
               edge.edgeDestinationPort->portName == request.portName;
    });
    return it != root->childEdges.end() ? std::addressof(*it) : nullptr;
}

void FlowgraphEditor::requestEdgeRemoval(const UiGraphEdge& edge) {
    // This has a bug where it disconnects unrelated edges which share this output port. TODO: implement disconnecting edges by destination port in gnuradio
    auto* sourceBlock = edge.getBlock(UiGraphPort::Role::Source);
    if (!sourceBlock || !sourceBlock->parentBlock) {
        return;
    }

    components::Notification::error("Edge removal behavior is currently unimplemented");

    // TODO: implement this message when a new message type is added to GR which
    // uniquely identifies edges (this method selects by source port, which may
    // include edges that are not connected to the exported port)
    //
    // auto* owningGraph = sourceBlock->parentBlock;
    // gr::Message message;
    // message.cmd         = gr::message::Command::Set;
    // message.endpoint    = gr::scheduler::property::kRemoveEdge;
    // message.serviceName = owningGraph->ownerSchedulerUniqueName();
    // message.data        = gr::property_map{                                                                   //
    //     {"_targetGraph", owningGraph->blockUniqueName},                                                //
    //     {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), sourceBlock->blockUniqueName}, //
    //     {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), edge.edgeSourcePort->portName}};
    // _graphModel->sendMessage(std::move(message));
}

void FlowgraphEditor::exportAllUnusedPorts() {
    if (_editorLevel == 0) {
        return;
    }
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        return;
    }

    const auto& edges        = rootBlock->childEdges;
    const auto* exportTarget = exportPortTargetBlock();

    for (const auto& block : rootBlock->childBlocks) {
        auto exportUnconnected = [this, exportTarget, &block, &edges](const std::vector<UiGraphPort>& ports, const std::string& direction) {
            for (const auto& port : ports) {
                if (isPortConnected(port, edges)) {
                    continue;
                }
                if (port.isExportedTo(exportTarget)) {
                    continue;
                }
                requestExportPort(ExportPortMessageData{
                    .uniqueBlockName = block->blockUniqueName,
                    .portDirection   = direction,
                    .portName        = port.portName,
                    .exportedName    = getDefaultExportedName(&port),
                    .exportFlag      = true,
                });
            }
        };

        exportUnconnected(block->_inputPorts, "input");
        exportUnconnected(block->_outputPorts, "output");
    }
}

void sendEmplaceBlockMessage(UiGraphModel& graphModel, const FlowgraphEditor::SchedulerGraphPair& owner, std::string type) {
    gr::Message message;
    message.cmd         = gr::message::Command::Set;
    message.endpoint    = gr::scheduler::property::kEmplaceBlock;
    message.serviceName = owner.scheduler;
    message.data        = gr::property_map{{"type", std::move(type)}, {"_targetGraph", owner.graph}};
    graphModel.sendMessage(std::move(message));
}

FlowgraphPage::~FlowgraphPage() = default;

void FlowgraphPage::reset() { _editors.clear(); }

void FlowgraphPage::pushEditor(std::string name, UiGraphModel& graphModel, UiGraphBlock* rootBlock) {
    assert(rootBlock && "An editor needs to have a root block defined");
    assert(!rootBlock->blockUniqueName.empty() && !rootBlock->blockCategory.empty() && "An editor needs to have a root block defined and initialized");

    auto& editor = _editors.emplace_back(name, graphModel, rootBlock, _editors.size());

    editor.updateStyle();
    editor.requestBlockControlsPanel = requestBlockControlsPanel;
    editor.showEditorControls        = showEditorControls;
    editor.showUiControlBlocks       = showUiControlBlocks;

    editor.requestGraphEdit = [&](UiGraphBlock* block) { pushEditor(block->blockUniqueName, graphModel, block); };

    // This lambda is owned by editor, so it is safe to take it by reference
    editor.openNewBlockSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownBlockTypes;
            _newBlockSelector.open([graphModel = editor.graphModel(), owner = std::move(*owner)](std::string type) { sendEmplaceBlockMessage(*graphModel, owner, std::move(type)); });
        }
    };

    // This lambda is owned by editor, so it is safe to take it by reference
    editor.openNewSubGraphSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownSchedulerTypes;
            _newBlockSelector.open([graphModel = editor.graphModel(), owner = std::move(*owner)](std::string type) { sendEmplaceBlockMessage(*graphModel, owner, std::move(type)); });
        }
    };

    // This lambda is owned by editor, so it is safe to take it by reference
    editor.openGroupBlocksSelectorCallback = [this, &editor](std::vector<std::string> uniqueNames) {
        _newBlockSelector.data = editor.graphModel()->knownSchedulerTypes;
        _newBlockSelector.open([&editor, uniqueNames = std::move(uniqueNames)](std::string graphType) { //
            editor.requestBlocksGrouping(std::move(graphType), uniqueNames);
        });
    };

    // We can add remote signals only to the root graph
    if (_dashboard && _editors.size() == 1) {
        editor.openAddRemoteSignalCallback = [this](UiGraphModel* editorGraphModel) {
            if (!editorGraphModel) {
                return;
            }
            try {
                if (!_remoteSignalSelector) {
                    _remoteSignalSelector = std::make_unique<SignalSelector>(*editorGraphModel);
                }
                _remoteSignalSelector->open();
            } catch (const std::exception& error) {
                components::Notification::error(std::format("Failed to open signal selector: {}", error.what()));
            }
        };
    }

    if (_editors.size() > 1) {
        editor.closeRequestedCallback = [this] { popEditor(); };
    }
}

void FlowgraphPage::popEditor() {
    _editors.pop_back();
    if (_editors.size() > 0) {
        currentEditor().graphModel()->requestFullUpdate();
    }
}

void FlowgraphPage::updateStyle() {
    for (auto& editor : _editors) {
        editor.updateStyle();
    }
}

void FlowgraphPage::drawLocalNodeEditor() {
    if (!_editors.empty()) {
        _currentTabIsFlowGraph = true;
        drawNodeEditorTab();
    } else if (_graphModel && !_graphModel->rootBlock.blockUniqueName.empty()) {
        pushEditor("rootBlock node editor", *_graphModel, std::addressof(_graphModel->rootBlock));
    }
}

void FlowgraphPage::drawNodeEditorTab() {
    auto contentSize    = ImGui::GetContentRegionAvail();
    auto contentTopLeft = ImGui::GetCursorPos();

    std::size_t level = 0UZ;
    // for (const auto& [level, editor] : std::views::enumerate(_editors)) { TODO: Use once we bump to an EMSDK that supports it
    for (auto& editor : _editors) {
        if (level != 0) {
            static constexpr float levelPadding = 16.0f;
            contentTopLeft += ImVec2(levelPadding, levelPadding);
            contentSize -= ImVec2(2 * levelPadding, 2 * levelPadding);
        }

        if (&editor == &_editors.back()) {
            editor.draw(contentTopLeft, contentSize, &editor == &_editors.back());

            if (_remoteSignalSelector) {
                for (const auto& selectedRemoteSignal : _remoteSignalSelector->drawAndReturnSelected()) {
                    _dashboard->addRemoteSignal(selectedRemoteSignal);
                }
            }

            _newBlockSelector.draw();
        }
        level++;
    }
}

void FlowgraphPage::drawLocalYamlTab() {
    if (ImGui::Button("Reset") || _currentTabIsFlowGraph) {
        // Reload yaml whenever "Local - YAML" tab is selected
        _currentTabIsFlowGraph = false;

        if (!_editors.empty()) {
            if (auto owner = _editors.front().ownersForRoot()) {
                gr::Message message;
                message.cmd         = gr::message::Command::Get;
                message.endpoint    = gr::scheduler::property::kGraphGRC;
                message.serviceName = owner->scheduler;
                _graphModel->sendMessage(std::move(message));
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        if (auto owner = _editors.front().ownersForRoot()) {
            gr::Message message;
            message.cmd         = gr::message::Command::Set;
            message.endpoint    = gr::scheduler::property::kGraphGRC;
            message.data        = gr::property_map{{"value", _graphModel->m_localFlowgraphGrc}};
            message.serviceName = owner->scheduler;
            _graphModel->sendMessage(std::move(message));
        }
    }

    ImGui::InputTextMultiline("##grc", &_graphModel->m_localFlowgraphGrc, ImGui::GetContentRegionAvail());
}

void FlowgraphPage::drawRemoteYamlTab(Dashboard::Service& service) {
    std::string tabTitle = "Remote YAML for " + service.name;
    if (auto item = IMW::TabItem(tabTitle.c_str(), nullptr, 0)) {
        if (ImGui::Button("Reload from service")) {
            service.reload();
        }
        ImGui::SameLine();
        if (ImGui::Button("Execute on service")) {
            service.execute();
        }

        // TODO: For demonstration purposes only, remove
        // once we have a proper server-side graph editor
        // if (::getenv("DIGITIZER_UI_SHOW_SERVER_TEST_BUTTONS")) {
        ImGui::SameLine();
        if (ImGui::Button("Create a block")) {
            service.emplaceBlock("gr::basic::DataSink", "float");
        }
        // }

        ImGui::InputTextMultiline("##grc", &service.grc, ImGui::GetContentRegionAvail());
    }
}

void FlowgraphPage::draw() noexcept {
    if (_graphModel == nullptr) {
        return;
    }
    // TODO: tab-bar is optional and should be eventually eliminated to optimise viewing area for data
    if (!showEditorControls) {
        drawLocalNodeEditor();
        return;
    }

    IMW::TabBar tabBar("maintabbar", 0);

    if (auto item = IMW::TabItem("Local", nullptr, 0)) {
        drawLocalNodeEditor();
    }

    if (auto item = IMW::TabItem("Local - YAML", nullptr, 0)) {
        drawLocalYamlTab();
    }

    if (_dashboard == nullptr) {
        return;
    }
    for (auto& service : _dashboard->services) {
        drawRemoteYamlTab(service);
    }
}

} // namespace DigitizerUi
