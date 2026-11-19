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
#include "MapUtils.hpp"
#include "PaneBlocks.hpp"
#include "common/ImguiNodeEditorWrap.hpp"
#include "common/Imw.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <imgui_node_editor_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "components/DataTypeStyle.hpp"
#include "components/OverlayButton.hpp"
#include "components/Splitter.hpp"
#include "components/StyledWidgets.hpp"
#include "components/YesNoPopup.hpp"

#include <gnuradio-4.0/meta/utils.hpp>

#include <cmath>

using namespace gr::od;

using namespace std::string_literals;

namespace {
[[nodiscard]] std::vector<UiGraphBlock*> nodeBlocks(const UiGraphBlock& root) {
    return root.childBlocks | std::views::filter([](const auto& child) { return !child->isChart() && !child->isUiControl(); }) | std::views::transform([](const auto& child) { return child.get(); }) | std::ranges::to<std::vector>();
}

// UI control blocks only exist in the root graph
[[nodiscard]] std::vector<UiGraphBlock*> uiControlBlocks(UiGraphModel& graphModel) {
    return graphModel.rootGraphBlock()->childBlocks | std::views::filter([](const auto& child) { return child->isUiControl(); }) | std::views::transform([](const auto& child) { return child.get(); }) | std::ranges::to<std::vector>();
}

bool isPortConnected(const UiGraphPort& port, const std::vector<UiGraphEdge>& edges) {
    return std::ranges::any_of(edges, [&port](const auto& edge) { //
        return edge.edgeSourcePort == &port || edge.edgeDestinationPort == &port;
    });
}

std::string simplerName(std::string_view rawName) {
    std::string result;
    result.reserve(rawName.size());

    const auto isIdentifierCharacter = [](const char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };

    // drops namespace qualifiers: at each ::, the identifier before it is erased
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

namespace gr::od {

namespace {

auto displayedPorts(const UiGraphBlock& block, const std::vector<UiGraphPort>& ports) {
    auto result = ports | std::views::transform([](const auto& port) { return &port; }) | std::ranges::to<std::vector>();
    if (block.isScheduler() || block.isGraph()) {
        std::ranges::sort(result, {}, [](const auto* port) { return std::tie(port->portType, port->portName); });
    }
    return result;
}

bool isPendingMultiSelectConnection(const UiGraphBlock& control, std::string_view hoveredProperty, const UiGraphBlock& block) { //
    return &control != &block && block.isConnectableProperty(hoveredProperty, control.uiControlCompatibleTypeMap());
}

void drawDashedLink(ImDrawList* drawList, ImVec2 start, ImVec2 end, ImU32 color) {
    const int         previousDrawChannel = drawList->_Splitter._Current;
    gr::on_scope_exit restoreDrawChannel  = [drawList, previousDrawChannel] { drawList->ChannelsSetCurrent(previousDrawChannel); };

    // the node editor's channel between edges and nodes (c_LinkChannel_Links): dashed edges draw like regular ones, under the nodes
    constexpr int kImGuiNodeEditorEdgeDrawChannel = 7;
    drawList->ChannelsSetCurrent(kImGuiNodeEditorEdgeDrawChannel);

    const auto&  lookAndFeel  = LookAndFeel::instance().flowgraph;
    const ImVec2 arrowBase    = end - ImVec2{lookAndFeel.pinArrowSize, 0.f};
    const float  halfDistance = ImLength(arrowBase - start) * .5f;
    const float  strength     = halfDistance < lookAndFeel.linkStrength ? lookAndFeel.linkStrength * ImSin(IM_PI * .5f * halfDistance / lookAndFeel.linkStrength) : lookAndFeel.linkStrength;
    const ImVec2 control0     = start + ImVec2{strength, 0.f};
    const ImVec2 control1     = arrowBase - ImVec2{strength, 0.f};
    const float  period       = lookAndFeel.eventLinkDashLength + lookAndFeel.eventLinkGapLength;
    const int    nSamples     = std::max(16, static_cast<int>(halfDistance));
    ImVec2       previous     = start;
    float        travelled    = 0.f;
    for (int i = 1; i <= nSamples; ++i) {
        const ImVec2 point = ImBezierCubicCalc(start, control0, control1, arrowBase, static_cast<float>(i) / static_cast<float>(nSamples));
        if (std::fmod(travelled, period) < lookAndFeel.eventLinkDashLength) {
            drawList->AddLine(previous, point, color);
        }
        travelled += ImLength(point - previous);
        previous = point;
    }
    const ImVec2 halfArrowWidth{0.f, lookAndFeel.pinArrowWidth * .5f};
    drawList->AddTriangleFilled(end, arrowBase + halfArrowWidth, arrowBase - halfArrowWidth, color);
}

/// `style` nullptr: drawn client side, which the node editor supports for rectangular port shapes
ImVec2 addPin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, const ImVec2& p, ImVec2 size, const DataTypeStyle* style) {
    const bool   input = kind == ax::NodeEditor::PinKind::Input;
    const ImVec2 min   = input ? p - ImVec2(size.x, 0) : p;
    const ImVec2 max   = input ? p + ImVec2(0, size.y) : p + size;
    const ImVec2 pivot = ImVec2(input ? min.x : max.x, (min.y + max.y) / 2.f);

    if (input) {
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowSize, LookAndFeel::instance().flowgraph.pinArrowSize);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_PinArrowWidth, LookAndFeel::instance().flowgraph.pinArrowWidth);
        ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_SnapLinkToPinDir, 1);
    }

    ax::NodeEditor::BeginPin(id, kind);
    ax::NodeEditor::PinPivotRect(pivot, pivot);
    ax::NodeEditor::PinRect(min, max);
    if (style) {
        ax::NodeEditor::PinDrawCallbacks(&drawPinForNodeEditor, &drawHoveredPinForNodeEditor, const_cast<DataTypeStyle*>(style));
    }
    ax::NodeEditor::EndPin();

    if (input) {
        ax::NodeEditor::PopStyleVar(3);
    }
    return pivot;
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

    if (constexpr std::size_t kMaxDisplayNameLength = 10; name.size() > kMaxDisplayNameLength) {
        name = "..." + name.substr(name.size() - kMaxDisplayNameLength);
    }
    return name;
}

float minimumPinAreaHeight(std::size_t numPins) {
    const auto& fg = LookAndFeel::instance().flowgraph;
    return numPins == 0 ? 0.f : static_cast<float>(numPins + 1) * (fg.pinHeight + fg.minimumPinGap);
}

PinDrawInfo calculatePinDrawInfo(const std::optional<std::string>& displayName, std::size_t index, std::size_t numPins, float anchorX, float blockTopY, float blockHeight, bool isInput) {
    const auto& fg = LookAndFeel::instance().flowgraph;

    if (!displayName) {
        const float y = blockTopY + pinLocalPositionY(index, numPins, blockHeight, fg.pinHeight);
        const float x = anchorX - fg.pinWidth / 2;
        return {.topLeft = {x, y}, .size = {fg.pinWidth, fg.pinHeight}};
    }

    auto* font = LookAndFeel::instance().fontSmall();
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

// NavigateTo widens the rect by half of this fraction of its larger dimension per side
constexpr float kNavigationZoomMargin = 0.1f;

[[nodiscard]] gr::Message toScheduler(const FlowgraphEditor::SchedulerGraphPair& owner, std::string endpoint, gr::property_map data) {
    data.insert_or_assign(std::pmr::string("_targetGraph"), owner.graph);
    return makeMessage(gr::message::Command::Set, owner.scheduler, std::move(endpoint), std::move(data));
}

void navigateWithoutMargin(const ImRect& visible, ImVec2 visibleSize) {
    const float viewMargin = std::max(visibleSize.x, visibleSize.y) * kNavigationZoomMargin / (2.f * (1.f + kNavigationZoomMargin));
    auto*       editor     = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(ax::NodeEditor::GetCurrentEditor());
    editor->NavigateTo(ImRect(visible.Min + ImVec2(viewMargin, viewMargin), visible.Max - ImVec2(viewMargin, viewMargin)), true);
}

} // namespace

FlowgraphEditor::Buttons FlowgraphEditor::drawButtons(const ImVec2& contentScreenTopLeft, const ImVec2& contentSize, Buttons buttons, float horizontalSplitRatio) {
    Buttons result;
    if (!(buttons.openNewBlockDialog || buttons.openNewSubGraphDialog || buttons.openRemoteSignalSelector || buttons.rearrangeBlocks || buttons.exportAllUnusedPorts || buttons.closeWindow)) {
        return result; // an overlay window without items is an ImGui error
    }

    imw::PushCursorPosition _;

    const float padding = LookAndFeel::instance().flowgraph.controlsBarPadding;
    const float height  = LookAndFeel::instance().flowgraph.controlsBarHeight;

    {
        ImGui::SetNextWindowPos({contentScreenTopLeft.x, contentScreenTopLeft.y + contentSize.y - height - padding});
        ImGui::SetNextWindowSize({contentSize.x * (1 - horizontalSplitRatio), height});
        imw::Window overlay("Button Overlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        {
            // the buttons are drawn on top of the editor: blended over the canvas, without transparency, they stay legible
            const ImVec4    canvas = canvasBackgroundColor().value_or(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
            imw::StyleColor buttonStyle(ImGuiCol_Button, overlayTransparentOverOpaque(ImGui::GetStyleColorVec4(ImGuiCol_Button), canvas));
            imw::StyleColor buttonHoveredStyle(ImGuiCol_ButtonHovered, overlayTransparentOverOpaque(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), canvas));
            imw::StyleColor buttonActiveStyle(ImGuiCol_ButtonActive, overlayTransparentOverOpaque(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), canvas));

            ImGui::SetCursorPosX(padding);

            if (buttons.openNewBlockDialog) {
                if (textButton("Add block...")) {
                    result.openNewBlockDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openNewSubGraphDialog) {
                if (textButton("Add sub graph...")) {
                    result.openNewSubGraphDialog = true;
                }
                ImGui::SameLine();
            }

            if (buttons.openRemoteSignalSelector) {
                if (textButton("Add remote signal...")) {
                    result.openRemoteSignalSelector = true;
                }
                ImGui::SameLine();
            }

            auto placeButtonRight = [padding, posFromRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - padding](const char* text) mutable { //
                float width = calcTextButtonSize(text).x;

                ImGui::SetCursorPosX(posFromRight - width);
                posFromRight -= width + padding;

                bool clicked = false;
                if (textButton(text)) {
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

    const auto  lineHeight                   = ImGui::GetFrameHeight();
    const float maxLabelSpaceFromLeft        = LookAndFeel::instance().flowgraph.computeDomainTagMaxOffset;
    const float labelSpaceFromLeftPercentage = LookAndFeel::instance().flowgraph.computeDomainTagOffsetRatio;
    const auto  spacingLeft                  = std::min(availableSpace * labelSpaceFromLeftPercentage, maxLabelSpaceFromLeft);
    const auto  topLeftOfRect                = topLeft + ImVec2{spacingLeft, -(lineHeight / 2.f)};

    const auto schedulerColorU32 = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorder);
    const auto textColorU32      = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphSubgraphBorderText);
    auto*      drawList          = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(topLeftOfRect, topLeftOfRect + textSize + ImVec2{textRectPadding * 2.f, 0.f}, schedulerColorU32, ImGui::GetStyle().FrameRounding);
    drawList->AddText(topLeftOfRect + ImVec2{textRectPadding, 0.f}, textColorU32, tagLabel.data(), tagLabel.data() + tagLabel.size());
}

void FlowgraphEditor::drawBoundingBoxExterior(const BoundingBox& canvasSpacingBoundingBox) {
    const float fadeLengthSeconds = LookAndFeel::instance().flowgraph.exportAreaFadeInSeconds;
    const auto  alphaPercentage   = std::clamp(this->_timeSpentHoldingPin, 0.f, fadeLengthSeconds) / fadeLengthSeconds;

    // while in canvas space, ImGui::GetMousePos() returns the canvas space value and does not need to be converted
    if (!canvasSpacingBoundingBox.contains(ImGui::GetMousePos())) {
        this->_wasHoveringBoundingBoxExteriorThisFrame = true;
        if (this->_timeSpentHoldingPin < fadeLengthSeconds) { // accelerate quickly to the background being fully faded in
            this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime * LookAndFeel::instance().flowgraph.exportAreaFadeInSpeedup;
        }
        ax::NodeEditor::Suspend();
        if (auto tooltip = imw::ToolTip{}) {
            ImGui::TextUnformatted("Release to export this pin...");
        }
        ax::NodeEditor::Resume();
    }

    auto          outlineAlpha       = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutline) * alphaPercentage);
    auto          outerAlpha         = static_cast<std::uint8_t>(LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelection) * alphaPercentage);
    std::uint32_t outlineOpaqueColor = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutline);
    std::uint32_t outerOpaqueColor   = LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelection);
    float         outlineThickness   = LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThickness;
    {
        auto       animPercentage = std::clamp(_timeSpentHoveringBoundingBoxExterior, 0.f, LookAndFeel::instance().flowgraph.exportAreaHoverFadeSeconds) / LookAndFeel::instance().flowgraph.exportAreaHoverFadeSeconds;
        const auto lerpColor      = [animPercentage](std::uint32_t coloru32, std::uint32_t targetu32) { //
            return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(coloru32), ImGui::ColorConvertU32ToFloat4(targetu32), animPercentage));
        };
        const auto castLerp = [animPercentage]<typename T>(T color, T target) { //
            return static_cast<T>(std::lerp(static_cast<float>(color), static_cast<float>(target), animPercentage));
        };

        outlineOpaqueColor = lerpColor(outlineOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outerOpaqueColor   = lerpColor(outerOpaqueColor, LookAndFeel::getColorU32Opaque(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outerAlpha         = castLerp(outerAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionHovered));
        outlineAlpha       = castLerp(outlineAlpha, LookAndFeel::getColorAlphaU8(&Palette::flowgraphBoundingBoxExteriorSelectionOutlineHovered));
        outlineThickness   = castLerp(outlineThickness, LookAndFeel::instance().flowgraph.flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered);
    }

    const auto outlineColor = rgbToImGuiABGR(outlineOpaqueColor, outlineAlpha);
    const auto outerColor   = rgbToImGuiABGR(outerOpaqueColor, outerAlpha);

    auto* const drawlist = ImGui::GetWindowDrawList();

    const auto visibleRegionMin = drawlist->GetClipRectMin();
    const auto visibleRegionMax = drawlist->GetClipRectMax();

    const auto& bb = canvasSpacingBoundingBox;
    drawlist->AddRect({bb.minX, bb.minY}, {bb.maxX, bb.maxY}, outlineColor, 0, ImDrawFlags_None, outlineThickness);
    drawlist->AddRectFilled(visibleRegionMin, {bb.minX, visibleRegionMax.y}, outerColor);
    drawlist->AddRectFilled({bb.maxX, visibleRegionMin.y}, visibleRegionMax, outerColor);
    drawlist->AddRectFilled({bb.minX, visibleRegionMin.y}, {bb.maxX, bb.minY}, outerColor);
    drawlist->AddRectFilled({bb.minX, bb.maxY}, {bb.maxX, visibleRegionMax.y}, outerColor);
}

FlowgraphEditor::NodeDrawResult FlowgraphEditor::drawNode(                       //
    UiGraphBlock&                                          block,                //
    const UiGraphModel::ControlledPropertyMap&             controlledProperties, //
    std::span<const UiGraphPort* const>                    inputPorts,           //
    std::span<const UiGraphPort* const>                    outputPorts,          //
    std::optional<components::PropertyConnectionIndicator> connectionPreview     //
) {
    const auto             blockId        = ax::NodeEditor::NodeId(std::addressof(block));
    const auto             isGroup        = block.isScheduler() || block.isGraph();
    const ImVec4           borderColor    = isGroup ? LookAndFeel::instance().palette().flowgraphSubgraphBorder : ax::NodeEditor::GetStyle().Colors[ax::NodeEditor::StyleColor_NodeBorder];
    const auto             borderWidthVar = imw::node_editor::StyleFloatVar(ax::NodeEditor::StyleVar_NodeBorderWidth, isGroup ? LookAndFeel::instance().flowgraph.subgraphBorderWidth : ax::NodeEditor::GetStyle().NodeBorderWidth);
    const auto             borderColorVar = imw::node_editor::StyleColor(ax::NodeEditor::StyleColor_NodeBorder, borderColor);
    imw::node_editor::Node node(blockId);

    if (isGroup) {
        this->drawComputeDomainTag(block);
    }

    const auto  minimumBlockSize    = LookAndFeel::instance().flowgraph.minimumBlockSize;
    const auto  blockScreenPosition = ImGui::GetCursorScreenPos();
    const float nodeTopScreenY      = blockScreenPosition.y - ax::NodeEditor::GetStyle().NodePadding.y;

    // less padding at the top: the font has empty space above its glyphs
    ImGui::SetCursorScreenPos(blockScreenPosition - ImVec2{0.f, ImGui::GetFontBaked()->FindGlyph('H')->Y0});
    ImGui::TextUnformatted(simplerName(block.blockName).c_str());
    {
        const float menuButtonWidthInsideNode = -components::nodeMenuButtonRect(ImVec2{}).Min.x;
        const float titleRightPadding         = std::max(0.f, menuButtonWidthInsideNode - ax::NodeEditor::GetStyle().NodePadding.z) + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SameLine(0.f, 0.f);
        ImGui::Dummy(ImVec2{titleRightPadding, 0.f});
    }
    auto blockSize = ax::NodeEditor::GetNodeSize(blockId);

    const auto&  nodeEditorStyle = ax::NodeEditor::GetStyle();
    const ImVec2 nodeTopRight    = ImVec2{blockScreenPosition.x - nodeEditorStyle.NodePadding.x + blockSize.x, blockScreenPosition.y - nodeEditorStyle.NodePadding.y};

    if (components::drawMenuButton(std::format("{}.menuButton", block.blockName).c_str(), components::nodeMenuButtonRect(nodeTopRight)).clicked) {
        // defer opening the popup so that we always open it from the same context in the ID stack (same as when doing right click)
        _blockContextMenuRequest = std::addressof(block);
    }

    {
        imw::Font font(LookAndFeel::instance().fontSmall());
        for (const auto& [propertyKey, propertyValue] : block.blockSettings) {
            if (propertyKey == "description" || propertyKey.contains("::")) {
                continue;
            }
            if (controlledProperties.contains(propertyKey)) {
                continue; // drawn below the space used by the ports
            }

            const auto& currentPropertyMetaInformation = block.blockSettingsMetaInformation[std::string(propertyKey)];
            if (!currentPropertyMetaInformation.isVisible) {
                continue;
            }
            std::string value = valueToString(propertyValue);
            ImGui::Text("%s: %s", currentPropertyMetaInformation.description.c_str(), value.c_str());
        }
    }

    // pins are spaced evenly from the node top down to the bottom of the content so far plus the bottom padding
    const auto pinAreaBottomScreenY = [] { return ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y + ax::NodeEditor::GetStyle().NodePadding.w; };
    if (const float missingPinSpace = minimumPinAreaHeight(std::max(inputPorts.size(), outputPorts.size())) - (pinAreaBottomScreenY() - nodeTopScreenY); missingPinSpace > 0.f) {
        ImGui::Dummy(ImVec2{0.f, std::max(0.f, missingPinSpace - ImGui::GetStyle().ItemSpacing.y)});
    }
    const float pinAreaHeight = pinAreaBottomScreenY() - nodeTopScreenY;

    // property connection indicators, the circuit-diagram-like marks, including a preview shown while hovering an interaction about to connect to that property
    const auto                                           notchColor = LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlConnectionIndicator);
    std::vector<components::PropertyConnectionIndicator> connectionIndicators;
    for (const auto& [property, controls] : controlledProperties) {
        for (const UiGraphBlock* control : controls) {
            connectionIndicators.push_back({.propertyName = property, .blockName = control->blockName, .blockUniqueName = control->blockUniqueName, .color = notchColor});
        }
    }
    if (connectionPreview) {
        connectionIndicators.push_back(std::move(*connectionPreview));
    }

    std::optional<std::string> propertyToUnlink; // defer unlinking until drawing is done, since drawing observes block settings
    drawIncomingPropertyConnectionIndicators(
        block, blockScreenPosition, connectionIndicators,                                                                           //
        [this](const components::PropertyConnectionIndicator& indicator) { this->requestFocusOnBlock(indicator.blockUniqueName); }, //
        [&propertyToUnlink](const components::PropertyConnectionIndicator& indicator) { propertyToUnlink = indicator.propertyName; });

    if (propertyToUnlink) {
        _modifyUiConnectionsPopup = components::unlinkUiControlledProperty(block, *propertyToUnlink);
    }

    if (block.isUiControl()) {
        if (drawUiControlWidget) {
            drawUiControlWidget(block, blockSize.x);
        }
        const ImVec2 nodeTopLeft = blockScreenPosition - ImVec2{nodeEditorStyle.NodePadding.x, nodeEditorStyle.NodePadding.y};
        if (const auto heldHandleCenter = components::drawUiControlTargetList(*_graphModel, block, ImRect{nodeTopLeft, nodeTopLeft + blockSize}); heldHandleCenter && !_blockDragConnect) {
            _blockDragConnect.emplace(block.blockUniqueName, *heldHandleCenter);
        }
    }

    if (const float missingHeight = blockScreenPosition.y + minimumBlockSize.y - ImGui::GetCursorScreenPos().y; missingHeight > 0.f) {
        ImGui::Dummy(ImVec2{0.f, missingHeight});
    }

    // without this dummy, anything over the node's edge (the filter button, the ports) would count into its bounding box, as ImGui adds the last item when closing a group, and the node would grow every frame
    ImGui::SetCursorScreenPos(blockScreenPosition);
    imw::StyleVar noSpacing(ImGuiStyleVar_ItemSpacing, ImVec2{});
    ImGui::Dummy(ImVec2{});

    // ports registered with the node editor, drawn later
    auto* exportTarget = exportPortTargetBlock();
    auto  registerPins = [this, exportTarget, &blockSize, pinAreaHeight](auto& ports, auto position, auto pinType) {
        if (pinType == ax::NodeEditor::PinKind::Output) {
            position.x += blockSize.x - ax::NodeEditor::GetStyle().NodePadding.x;
        }

        const float blockY  = position.y - ax::NodeEditor::GetStyle().NodePadding.y;
        const bool  isInput = pinType == ax::NodeEditor::PinKind::Input;

        for (std::size_t i = 0; i < ports.size(); ++i) {
            auto portDisplayName = exportedPortShortenedDisplayName(ports[i], exportTarget);
            auto info            = calculatePinDrawInfo(portDisplayName, i, ports.size(), position.x, blockY, pinAreaHeight, isInput);
            auto pinPos          = isInput ? ImVec2{info.topLeft.x + info.size.x, info.topLeft.y} : info.topLeft;
            _pinPivots[ports[i]] = addPin(ax::NodeEditor::PinId(ports[i]), pinType, pinPos, info.size, portDisplayName ? nullptr : &styleForDataType(ports[i]->portType));
        }
    };

    ImVec2 position = {blockScreenPosition.x - ax::NodeEditor::GetStyle().NodePadding.x, blockScreenPosition.y};
    registerPins(inputPorts, position, ax::NodeEditor::PinKind::Input);
    registerPins(outputPorts, blockScreenPosition, ax::NodeEditor::PinKind::Output);

    return NodeDrawResult{.topLeft = position, .pinAreaHeight = pinAreaHeight};
}

std::optional<components::BlockDragConnectOverlay> FlowgraphEditor::updateUiControlDragConnect(std::span<UiGraphBlock* const> drawnBlocks) {
    if (!_blockDragConnect) {
        return std::nullopt;
    }
    auto overlay = _blockDragConnect->createOverlayDescription();
    if (!_blockDragConnect->update(drawnBlocks)) {
        return overlay;
    }
    if (!_blockDragConnect->cancelled()) {
        if (_blockDragConnect->releasedOnUniqueName().empty()) {
            _uiControlMultiSelectPopup.emplace(std::string(_blockDragConnect->sourceUniqueName()), *_graphModel);
        } else {
            _uiControlPropertyLinkPopup.emplace(std::move(*_blockDragConnect), *_graphModel);
        }
    }
    _blockDragConnect.reset();
    return overlay;
}

void FlowgraphEditor::drawUiControlPopups() {
    if (_uiControlPropertyLinkPopup && _uiControlPropertyLinkPopup->draw(*_graphModel) != components::SelectionPopupState::InProgress) {
        _uiControlPropertyLinkPopup.reset();
    }
    if (_uiControlMultiSelectPopup && _uiControlMultiSelectPopup->draw(*_graphModel) != components::SelectionPopupState::InProgress) {
        _uiControlMultiSelectPopup.reset();
    }
    if (_modifyUiConnectionsPopup && !_modifyUiConnectionsPopup->draw(_graphModel)) {
        _modifyUiConnectionsPopup.reset();
    }
}

void FlowgraphEditor::sendPinsConnectedGraphMessage(ax::NodeEditor::PinId startPinId, ax::NodeEditor::PinId endPinId) {
    auto* startPort = startPinId.AsPointer<UiGraphPort>();
    auto* endPort   = endPinId.AsPointer<UiGraphPort>();

    if (startPort->portDirection == endPort->portDirection) {
        ax::NodeEditor::RejectNewItem();

    } else {
        // the pins arrive in drag order (start, end): ordered here by port direction
        auto* outputPort = startPort->portDirection == gr::PortDirection::OUTPUT ? startPort : endPort;
        auto* inputPort  = startPort->portDirection == gr::PortDirection::INPUT ? startPort : endPort;

        if (ax::NodeEditor::AcceptNewItem()) {
            auto owner = ownersForRoot();
            if (!owner) {
                return;
            }
            gr::Message message = toScheduler(*owner, gr::scheduler::property::kEmplaceEdge, emplaceEdgeData(outputPort->ownerBlock->blockUniqueName, outputPort->portName, inputPort->ownerBlock->blockUniqueName, inputPort->portName));

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

void FlowgraphEditor::updatePinDrag(BoundingBox boundingBox, ImVec4 linkColor) {
    if (auto creation = imw::node_editor::Creation(linkColor, LookAndFeel::instance().flowgraph.newLinkThickness)) {
        // allow the user to drag outside the bounds of a subgraph to export a port
        if (_editorLevel > 0) {
            this->drawBoundingBoxExterior(boundingBox);
        }
        this->_timeSpentHoldingPin += ImGui::GetIO().DeltaTime;

        ax::NodeEditor::PinId startPinId, endPinId;
        if (ax::NodeEditor::QueryNewLink(&startPinId, &endPinId)) {
            // the pins come in drag order (start, end) whatever their direction; the end pin is valid only once the link is over another pin
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
                    .exportedName    = getDefaultExportedName(port),
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

std::optional<components::BlockDragConnectOverlay> FlowgraphEditor::drawGraph(const ImVec2& size) {
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        return std::nullopt;
    }

    const auto* exportTarget = exportPortTargetBlock();

    const std::vector<UiGraphBlock*> graphBlocks               = nodeBlocks(*rootBlock);
    const std::vector<UiGraphBlock*> controlBlocks             = showUiControlBlocks ? uiControlBlocks(*_graphModel) : std::vector<UiGraphBlock*>{};
    const auto&                      graphEdges                = rootBlock->childEdges;
    const auto                       parsedUiControlTargetMaps = _graphModel->parseAllUiControlTargetMaps();

    const std::optional<std::string_view> multiSelectHoveredProperty       = _uiControlMultiSelectPopup ? _uiControlMultiSelectPopup->hoveredProperty() : std::nullopt;
    const UiGraphBlock*                   multiSelectControl               = multiSelectHoveredProperty ? _graphModel->recursiveFindBlockByUniqueName(_uiControlMultiSelectPopup->controlUniqueName()).block : nullptr;
    const UiGraphBlock*                   dragSourceControl                = _blockDragConnect ? _graphModel->recursiveFindBlockByUniqueName(_blockDragConnect->sourceUniqueName()).block : nullptr;
    const auto                            propertiesControlledByDragSource = dragSourceControl ? _graphModel->resolveControlledProperties(*dragSourceControl) : std::unexpected("no UI control is being dragged from");
    const auto                            isCompatibleWithDragSource       = [&propertiesControlledByDragSource, dragSourceControl](const UiGraphBlock& candidate) { //
        assert(bool(dragSourceControl) == bool(propertiesControlledByDragSource));
        return propertiesControlledByDragSource && UiGraphModel::doesBlockHaveACompatibleProperty(*dragSourceControl, *propertiesControlledByDragSource, candidate);
    };

    makeCurrent();
    dropReferencesToDeletedBlocks();
    _pinPivots.clear();

    std::vector<UiGraphBlock*> drawnBlocks = graphBlocks;
    drawnBlocks.insert(drawnBlocks.end(), controlBlocks.cbegin(), controlBlocks.cend());

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
    gr::on_scope_exit capturePositions = [&] {
        for (const auto& block : graphBlocks) {
            const auto position = ax::NodeEditor::GetNodePosition(ax::NodeEditor::NodeId(block));
            block->storedXY     = UiGraphBlock::StoredXY{position.x, position.y};
        }
    };
    const int                nHiddenBorderColours = LookAndFeel::instance().flowgraph.canvasBorder ? 0 : 2;
    gr::on_scope_exit        showBorderAgain      = [nHiddenBorderColours] { ImGui::PopStyleColor(nHiddenBorderColours); };
    imw::node_editor::Editor nodeEditor(_editorName.c_str(), size);
    gr::on_scope_exit        hideBorder = [nHiddenBorderColours] {
        for (const ImGuiCol colour : {ImGuiCol_Border, ImGuiCol_BorderShadow}) {
            if (nHiddenBorderColours > 0) {
                ImGui::PushStyleColor(colour, ImVec4{});
            }
        }
    };
    const UiGraphBlock* dragConnectDropTarget = _blockDragConnect ? components::BlockDragConnectInteraction::blockUnderMouse(drawnBlocks) : nullptr; // canvas space mouse

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

    // the UI connection labels need to be added to the bounding box of the block so that auto-layout takes them into account
    const auto addBlockToBoundingBox = [&addRectangleToBoundingBox](const UiGraphBlock& block, ImVec2 blockPosition, float connectionLabelsWidth) {
        const ImVec2 labelsSize{connectionLabelsWidth, 0.F};
        addRectangleToBoundingBox(blockPosition - labelsSize, ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(&block)) + labelsSize);
    };

    std::vector<ax::NodeEditor::NodeId> filteredOutNodes;

    filteredOutNodes.reserve(_filterBlock ? graphBlocks.size() : 0);

    const auto drawBlock = [&](UiGraphBlock* block, const UiGraphModel::ControlledPropertyMap& controlledProperties) {
        const auto blockId     = ax::NodeEditor::NodeId(block);
        auto       inputPorts  = displayedPorts(*block, block->inputPorts());
        auto       outputPorts = displayedPorts(*block, block->outputPorts());

        const bool filteredOut = _filterBlock && !_graphModel->blockInTree(*block, *_filterBlock);

        float             originalAlpha = std::exchange(ImGui::GetStyle().Alpha, (filteredOut ? LookAndFeel::instance().flowgraph.filteredOutBlockAlpha : ImGui::GetStyle().Alpha));
        gr::on_scope_exit restoreStyle  = [&] { ImGui::GetStyle().Alpha = originalAlpha; };

        if (filteredOut) {
            filteredOutNodes.push_back(blockId);
        }

        const bool pendingMultiSelect          = multiSelectControl && isPendingMultiSelectConnection(*multiSelectControl, *multiSelectHoveredProperty, *block);
        const auto connectionPreview           = pendingMultiSelect ? std::optional(components::PropertyConnectionIndicator{
                                                                          .propertyName    = std::string(*multiSelectHoveredProperty),
                                                                          .blockName       = multiSelectControl->blockName,
                                                                          .blockUniqueName = multiSelectControl->blockUniqueName,
                                                                          .color           = LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlPendingConnection),
                                                                          .isInteractive   = false,
                                                            })
                                                                    : std::nullopt;
        const bool isUiControlConnectionTarget = pendingMultiSelect || isCompatibleWithDragSource(*block);
        const auto blockPosition               = this->drawNode(*block, controlledProperties, inputPorts, outputPorts, connectionPreview);
        const auto blockSize                   = ax::NodeEditor::GetNodeSize(blockId);

        // pins drawn after the node ends: drawn inside, they would grow the node, which places the output pins, so it would grow every frame
        {
            ImGui::SetCursorScreenPos(blockPosition.topLeft);
            auto drawList = ax::NodeEditor::GetNodeBackgroundDrawList(blockId);

            auto drawPorts = [&](auto& ports, float anchorX, bool isInput) {
                const auto& flowgraphLookAndFeel = LookAndFeel::instance().flowgraph;
                const float blockTopY            = blockPosition.topLeft.y - ax::NodeEditor::GetStyle().NodePadding.y;

                for (std::size_t i = 0; i < ports.size(); ++i) {
                    auto portExportedDisplayName = exportedPortShortenedDisplayName(ports[i], exportTarget);
                    auto info                    = calculatePinDrawInfo(portExportedDisplayName, i, ports.size(), anchorX, blockTopY, blockPosition.pinAreaHeight, isInput);

                    if (!portExportedDisplayName) {
                        // the node editor draws these pins, see addPin()
                        if (ImGui::IsMouseHoveringRect(info.topLeft, info.topLeft + info.size)) {
                            // node editor tooltips need a suspended canvas, see imgui-node-editor/examples/widgets-example
                            ax::NodeEditor::Suspend();
                            ImGui::SetTooltip("%s (%s)", ports[i]->portName.c_str(), ports[i]->portType.c_str());
                            ax::NodeEditor::Resume();
                        }
                        continue;
                    }

                    const auto& typeStyle = styleForDataType(ports[i]->portType);
                    const auto  textColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().flowgraphBg);
                    drawList->AddRectFilled(info.topLeft, info.topLeft + info.size, typeStyle.color(), flowgraphLookAndFeel.rectangularPortRounding);
                    drawList->AddRect(info.topLeft, info.topLeft + info.size, darkenOrLighten(typeStyle.color()), flowgraphLookAndFeel.rectangularPortRounding);
                    drawList->AddText(info.font, info.fontSize, ImVec2{info.topLeft.x + flowgraphLookAndFeel.exportedTabPaddingH, info.topLeft.y + flowgraphLookAndFeel.exportedTabPaddingV}, textColor, portExportedDisplayName->c_str());
                }
            };

            drawPorts(inputPorts, blockPosition.topLeft.x, true);
            drawPorts(outputPorts, blockPosition.topLeft.x + blockSize.x, false);
        }

        if (isUiControlConnectionTarget) {
            const auto  nodePosition = ax::NodeEditor::GetNodePosition(blockId);
            const float thickness    = block == dragConnectDropTarget ? LookAndFeel::instance().flowgraph.dropTargetOutlineThickness : LookAndFeel::instance().flowgraph.highlightThickness;
            ax::NodeEditor::GetNodeBackgroundDrawList(blockId)->AddRect(nodePosition, nodePosition + blockSize, LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlPendingConnection), LookAndFeel::instance().flowgraph.nodeRounding, ImDrawFlags_None, thickness);
        }
    };

    // draw blocks before arranging Ui control block region, whose size depends on the space taken up by non-ui-control blocks
    for (auto& block : graphBlocks) {
        const auto controlledProperties = UiGraphModel::uiControlledProperties(*block, parsedUiControlTargetMaps);
        drawBlock(block, controlledProperties);
        if (block->storedXY) {
            addBlockToBoundingBox(*block, ax::NodeEditor::GetNodePosition(ax::NodeEditor::NodeId(block)), components::uiControlConnectionLabelsWidth(controlledProperties));
        }
    }

    for (auto& block : graphBlocks) {
        if (!block->storedXY) {
            const auto   bounds                = boundingBox.value_or(kDefaultBoundingBox);
            const float  connectionLabelsWidth = components::uiControlConnectionLabelsWidth(UiGraphModel::uiControlledProperties(*block, parsedUiControlTargetMaps));
            const ImVec2 position{bounds.minX + connectionLabelsWidth, bounds.maxY + LookAndFeel::instance().flowgraph.unplacedBlockGap};
            ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(block), position);
            addBlockToBoundingBox(*block, position, connectionLabelsWidth);
        }
    }

    const auto graphBounds           = boundingBox.value_or(kDefaultBoundingBox);
    const auto uiControlRegionBounds = components::calculateAndArrangeUiControlBlocksDesignatedRegion(parsedUiControlTargetMaps, controlBlocks, ImRect(ImVec2{graphBounds.minX, graphBounds.minY}, ImVec2{graphBounds.maxX, graphBounds.maxY}));
    if (uiControlRegionBounds) {
        components::drawUiControlRegionBackgroundRect(*uiControlRegionBounds);
    }
    for (UiGraphBlock* control : controlBlocks) {
        drawBlock(control, UiGraphModel::uiControlledProperties(*control, parsedUiControlTargetMaps));
    }

    // the node editor restores the previous view when resized: a fit followed by a resize is repeated
    const ImVec2 canvasSize    = ax::NodeEditor::GetScreenSize();
    const bool   canvasResized = canvasSize.x != _lastCanvasSize.x || canvasSize.y != _lastCanvasSize.y;
    _lastCanvasSize            = canvasSize;
    if (std::exchange(_fitJustApplied, false) && canvasResized) {
        _fitRequested = true;
    }

    // arranged only after drawing has measured every block
    if (std::exchange(_rearrangeRequested, false)) {
        sortNodes(rootBlock);
        _fitRequested = true;
    } else if (_fitRequested) {
        _fitRequested = false;
        fitIntoView(*rootBlock, showEditorControls || closeRequestedCallback ? LookAndFeel::instance().flowgraph.controlsBarHeight + LookAndFeel::instance().flowgraph.controlsBarPadding : 0.f);
        _fitJustApplied = true;
    }

    if (_focusBlockRequest) {
        if (auto* focusBlock = _graphModel->recursiveFindBlockByUniqueName(*_focusBlockRequest).block) {
            const auto focusNodeSize = ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(focusBlock));
            if (focusNodeSize.x > 0.f && focusNodeSize.y > 0.f) {
                focusOnBlockWithDefaultZoomLevel(*focusBlock);
            } else {
                assert(focusNodeSize.x <= 0.f && focusNodeSize.y <= 0.f && "expected UI control blocks to be drawn in every editor or subgraph, so Jump to Control action can always work");
            }
        }
        _focusBlockRequest.reset();
    }

    const auto linkColor = [](const UiGraphPort& sourcePort) {
        const ImU32 typeColor = styleForDataType(sourcePort.portType).color();
        return (typeColor >> IM_COL32_A_SHIFT) == 0 ? ImGui::GetStyle().Colors[ImGuiCol_Text] : ImGui::ColorConvertU32ToFloat4(typeColor);
    };
    for (auto& edge : graphEdges) {
        const auto sourceBlockId      = ax::NodeEditor::NodeId(edge.edgeSourcePort->ownerBlock);
        const auto destinationBlockId = ax::NodeEditor::NodeId(edge.edgeDestinationPort->ownerBlock);
        if (!std::any_of(filteredOutNodes.begin(), filteredOutNodes.end(), [&](const ax::NodeEditor::NodeId& nodeId) { //
                return nodeId == sourceBlockId || nodeId == destinationBlockId;
            })) {
            const bool isEventLink = isEventDataType(edge.edgeSourcePort->portType);
            ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), //
                ax::NodeEditor::PinId(edge.edgeSourcePort),     //
                ax::NodeEditor::PinId(edge.edgeDestinationPort), isEventLink ? ImVec4{} : linkColor(*edge.edgeSourcePort));
            const auto start = _pinPivots.find(edge.edgeSourcePort);
            const auto end   = _pinPivots.find(edge.edgeDestinationPort);
            if (isEventLink && start != _pinPivots.end() && end != _pinPivots.end()) {
                drawDashedLink(ImGui::GetWindowDrawList(), start->second, end->second, ImGui::GetColorU32(linkColor(*edge.edgeSourcePort)));
            }
        }
    }

    auto dragConnectOverlay = this->updateUiControlDragConnect(drawnBlocks);

    // fade out the bounding box effect. though updatePinDrag() may call drawBoundingBoxExterior() and stop this from happening
    this->_wasHoveringBoundingBoxExteriorThisFrame = false;

    this->updatePinDrag(
        [&boundingBox, &uiControlRegionBounds]() {
            constexpr static std::size_t kMarginPixels = 10;
            auto                         out           = boundingBox.value_or(kDefaultBoundingBox);
            if (uiControlRegionBounds) {
                out.addRectangle(uiControlRegionBounds->Min, uiControlRegionBounds->GetSize());
            }
            out.minX -= kMarginPixels;
            out.minY -= kMarginPixels;
            out.maxX += kMarginPixels;
            out.maxY += kMarginPixels;
            return out;
        }(),
        ImGui::GetStyle().Colors[ImGuiCol_Text]);

    if (this->_wasHoveringBoundingBoxExteriorThisFrame) {
        if (this->_timeSpentHoveringBoundingBoxExterior < LookAndFeel::instance().flowgraph.exportAreaHoverFadeSeconds) {
            this->_timeSpentHoveringBoundingBoxExterior += ImGui::GetIO().DeltaTime;
        }
    } else if (this->_timeSpentHoveringBoundingBoxExterior > 0) {
        this->_timeSpentHoveringBoundingBoxExterior -= ImGui::GetIO().DeltaTime;
    }
    return dragConnectOverlay;
}

void FlowgraphEditor::draw(const ImVec2& contentTopLeft, const ImVec2& contentSize) {
    auto* rootBlock = this->rootBlock();
    if (!rootBlock) {
        // the graph may not exist yet after a graph exchange, with a stale root block name of the old graph
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

    imw::PushCursorPosition origCursorPos;

    ImGui::SetCursorPos(contentTopLeft);
    const ImVec2 contentScreenTopLeft = ImGui::GetCursorScreenPos(); // SetNextWindowPos takes screen coordinates

    const bool  horizontalSplit   = contentSize.x > contentSize.y;
    const float splitterWidth     = LookAndFeel::instance().splitterWidth;
    const float halfSplitterWidth = splitterWidth / 2.f;
    const float ratio             = components::splitter(_splitter, contentSize, horizontalSplit, splitterWidth, LookAndFeel::instance().splitterDefaultRatio, !_editPaneContext.selectedBlock());

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
                imw::Disabled _(exportPortTextField.empty());
                if (textButton("OK")) {
                    exportPortRequest->exportedName = exportPortTextField;
                    requestExportPort(*exportPortRequest);

                    exportPortRequest.reset();
                    exportPortTextField.clear();
                    shouldShowExportPortPopup = false;
                }
            }
            ImGui::SameLine();
            if (textButton("Cancel")) {
                exportPortRequest.reset();
                exportPortTextField.clear();
                shouldShowExportPortPopup = false;
            }
            ImGui::EndPopup();
        }
    }

    constexpr static const char* kEdgeConflictPopupId = "##Edge conflict";
    if (unexportPortRequest || exportConflictRequest) {
        ImGui::OpenPopup(kEdgeConflictPopupId);

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

        const auto popupResult = beginYesNoPopup(kEdgeConflictPopupId, popupOptions, ImGuiWindowFlags_AlwaysAutoResize);
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

        if (!ImGui::IsPopupOpen(kEdgeConflictPopupId)) {
            // popup closed, so the user's request is either cancelled or applied
            unexportPortRequest.reset();
            exportConflictRequest.reset();
        }
    }

    // we draw this before we draw the graph, because this may want to request that some nodes be highlighted when the user is hovering certain options
    drawUiControlPopups();

    if (const auto dragConnectOverlay = drawGraph(contentSize)) {
        dragConnectOverlay->draw();
    }

    // don't open properties pane if just clicking on the block menu button
    const bool blockMenuButtonPressed = _blockContextMenuRequest != nullptr;

    const auto mouseDrag         = ImLengthSqr(ImGui::GetMouseDragDelta(ImGuiMouseButton_Right));
    const auto backgroundClicked = ax::NodeEditor::GetBackgroundClickButtonIndex();

    if (!blockMenuButtonPressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouseDrag < 200 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        auto n     = ax::NodeEditor::GetHoveredNode();
        auto block = n.AsPointer<UiGraphBlock>();

        if (!block) {
            _editPaneContext.setSelectedBlock(nullptr, nullptr);
        } else if (auto ownerNames = ownersFor(*block)) {
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
    if (blockMenuButtonPressed) {
        ImGui::OpenPopup("block_ctx_menu");
        _selectedBlock = std::exchange(_blockContextMenuRequest, nullptr);
    }

    if (backgroundClicked == ImGuiMouseButton_Right && mouseDrag < 200) {
        ImGui::OpenPopup("ctx_menu");
        _contextMenuPosition = ax::NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
    }

    if (auto menu = imw::Popup("ctx_menu", 0)) {
        if (ImGui::MenuItem("Refresh graph")) {
            _graphModel->requestFullUpdate();
            _graphModel->requestAvailableBlocksTypesUpdate();
        }

        if (_filterBlock != nullptr && ImGui::MenuItem("Remove filter")) {
            _filterBlock = nullptr;
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

    auto portsSubMenu = imw::Menu{text, /*enabled*/ true};
    if (!portsSubMenu) {
        return;
    }

    for (const UiGraphPort& port : blockPorts) {
        bool exported = port.isExportedTo(exportTarget);

        // only one item in the menu: a checkbox to toggle whether exported
        if (!checkbox(std::format("{}##{}-{}", port.portName, port.ownerBlock->blockUniqueName, port.portName).c_str(), &exported)) {
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
    auto menu = imw::Popup("block_ctx_menu", 0);
    if (!menu) {
        return;
    }

    if (_selectedBlock == nullptr) {
        // the block was deleted while the menu was open
        ImGui::CloseCurrentPopup();
        return;
    }

    const bool isFilter = _filterBlock == _selectedBlock;
    if (ImGui::MenuItem(isFilter ? "Remove filter" : "Filter")) {
        _filterBlock = isFilter ? nullptr : _selectedBlock;
    }

    if (ImGui::MenuItem("Delete this block")) {
        requestBlockDeletion(_selectedBlock->blockUniqueName);
    }

    if (_selectedBlock->isGraph() || _selectedBlock->isScheduler()) {
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
        if (imw::Menu blockTypesMenu("Change type to...", /*enabled*/ true); blockTypesMenu) {
            for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                if (availableParametrization != typeParams.parametrization && ImGui::MenuItem(availableParametrization.c_str())) {
                    if (auto owner = ownersFor(*_selectedBlock)) {
                        _graphModel->sendMessage(toScheduler(*owner, gr::scheduler::property::kReplaceBlock, {{"uniqueName", _selectedBlock->blockUniqueName}, {"type", std::move(typeParams.baseType) + availableParametrization}, {"properties", gr::property_map{{"name", _selectedBlock->blockName}}}}));
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
    const std::vector<UiGraphBlock*> blocks = nodeBlocks(*rootBlock);

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
        nodeSizes.push_back({.width = components::uiControlConnectionLabelsWidth(*_graphModel, block) + std::max(blockSize.x, 80.f), .height = std::max(blockSize.y, 40.f)});
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
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(blocks[i]), ImVec2(positions[i].x + components::uiControlConnectionLabelsWidth(*_graphModel, *blocks[i]), positions[i].y));
    }
}

void FlowgraphEditor::fitIntoView(const UiGraphBlock& rootBlock, float reservedBottomPixels) const {
    const std::vector<UiGraphBlock*> blocks        = nodeBlocks(rootBlock);
    const std::vector<UiGraphBlock*> controlBlocks = showUiControlBlocks ? uiControlBlocks(*_graphModel) : std::vector<UiGraphBlock*>{};
    if (blocks.empty() && controlBlocks.empty()) {
        return;
    }
    ImRect     bounds(ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), ImVec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
    const auto addNode = [this, &bounds](const UiGraphBlock* block, float margin) {
        const auto blockId  = ax::NodeEditor::NodeId(block);
        const auto position = ax::NodeEditor::GetNodePosition(blockId);
        bounds.Add(ImRect(position - ImVec2{margin + components::uiControlConnectionLabelsWidth(*_graphModel, *block), margin}, position + ax::NodeEditor::GetNodeSize(blockId) + ImVec2{margin, margin + components::uiControlTargetListHeight(*block)}));
    };
    std::ranges::for_each(blocks, [&addNode](const UiGraphBlock* block) { addNode(block, 0.f); });
    std::ranges::for_each(controlBlocks, [&addNode](const UiGraphBlock* block) { addNode(block, LookAndFeel::instance().flowgraph.uiControlRegionGap); });

    const ImVec2 viewSize      = ax::NodeEditor::GetScreenSize();
    const float  usableHeight  = std::max(1.f, viewSize.y - reservedBottomPixels);
    const float  contentMargin = std::max(bounds.GetWidth(), bounds.GetHeight()) * kNavigationZoomMargin * 0.5f;
    const float  zoom          = std::min({1.f, viewSize.x / (bounds.GetWidth() + 2.f * contentMargin), usableHeight / (bounds.GetHeight() + 2.f * contentMargin)});

    const ImVec2 visibleSize = viewSize / zoom;
    const float  visibleTop  = bounds.GetCenter().y - 0.5f * usableHeight / zoom;
    const ImRect visible(ImVec2(bounds.GetCenter().x - 0.5f * visibleSize.x, visibleTop), ImVec2(bounds.GetCenter().x + 0.5f * visibleSize.x, visibleTop + visibleSize.y));
    navigateWithoutMargin(visible, visibleSize);
}

void FlowgraphEditor::focusOnBlockWithDefaultZoomLevel(const UiGraphBlock& block) const {
    const auto   blockId  = ax::NodeEditor::NodeId(std::addressof(block));
    const ImVec2 position = ax::NodeEditor::GetNodePosition(blockId);
    const ImVec2 size     = ax::NodeEditor::GetNodeSize(blockId);

    const ImVec2 viewSize = ax::NodeEditor::GetScreenSize();
    const ImVec2 centre   = position + size * 0.5f;
    navigateWithoutMargin(ImRect(centre - viewSize * 0.5f, centre + viewSize * 0.5f), viewSize);
}

void FlowgraphEditor::requestBlockDeletion(const std::string& blockName) {
    const UiGraphBlock* block = _graphModel->recursiveFindBlockByUniqueName(blockName).block;
    if (auto owner = block ? ownersFor(*block) : std::nullopt) {
        _graphModel->sendMessage(toScheduler(*owner, gr::scheduler::property::kRemoveBlock, {{"uniqueName", blockName}}));
    }
}

void FlowgraphEditor::requestBlocksGrouping(std::string graphType, const std::vector<std::string>& uniqueNames) {
    if (auto owner = ownersForRoot()) {
        gr::Tensor<gr::pmt::Value> names(gr::extents_from, {uniqueNames.size()});
        for (std::size_t i = 0; i < uniqueNames.size(); ++i) {
            names[i] = uniqueNames[i];
        }

        _graphModel->sendMessage(toScheduler(*owner, gr::scheduler::property::kGroupBlocks, {{"type", std::move(graphType)}, {"uniqueNames", std::move(names)}}));
    }
}

void FlowgraphEditor::requestBlocksUngrouping(const std::string& uniqueName) {
    const UiGraphBlock* block = _graphModel->recursiveFindBlockByUniqueName(uniqueName).block;
    if (auto owner = block ? ownersFor(*block) : std::nullopt) {
        _graphModel->sendMessage(toScheduler(*owner, gr::scheduler::property::kUngroupBlocks, {{"uniqueName", uniqueName}}));
    }
}

void FlowgraphEditor::dropReferencesToDeletedBlocks() {
    if (_seenBlockDestructionCount == _graphModel->blockDestructionCount) {
        return;
    }
    _seenBlockDestructionCount = _graphModel->blockDestructionCount;

    // any destroyed block clears the selection: a NodeId cannot tell whether its block was freed
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
    // ui control blocks are special and can only exist in the root graph
    std::erase_if(uniqueNames, [this](const std::string& uniqueName) {
        const UiGraphBlock* block = _graphModel->recursiveFindBlockByUniqueName(uniqueName).block;
        return block && block->isUiControl();
    });
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
    graphModel()->send(gr::message::Command::Set, _exportPortTargetBlockUniqueName, gr::graph::property::kSubgraphExportPort, //
        {{"uniqueBlockName", request.uniqueBlockName}, {"portDirection", request.portDirection}, {"portName", request.portName}, {"exportedName", request.exportedName}, {"exportFlag", request.exportFlag}});
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
    auto* sourceBlock = edge.getBlock(UiGraphPort::Role::Source);
    if (!sourceBlock || !sourceBlock->parentBlock) {
        return;
    }

    components::Notification::error("Edge removal behavior is currently unimplemented");
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

namespace {
void sendEmplaceBlockMessage(UiGraphModel& graphModel, const FlowgraphEditor::SchedulerGraphPair& owner, std::string type) { //
    graphModel.sendMessage(toScheduler(owner, gr::scheduler::property::kEmplaceBlock, {{"type", type}, {"properties", gr::property_map{{"name", graphModel.nameRegistry.ensureNameIsUnique(type)}}}}));
}
} // namespace

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

    editor.drawUiControlWidget = [this](const UiGraphBlock& control, float maxWidth) { drawUiControlWidget(control, maxWidth); };

    // owned by the editor: the reference stays valid
    editor.openNewBlockSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownBlockTypes;
            _newBlockSelector.open([graphModel = editor.graphModel(), owner = std::move(*owner)](std::string type) { sendEmplaceBlockMessage(*graphModel, owner, std::move(type)); });
        }
    };

    // owned by the editor: the reference stays valid
    editor.openNewSubGraphSelectorCallback = [this, &editor](UiGraphModel* /*_graphModel*/) {
        if (auto owner = editor.ownersForRoot()) {
            _newBlockSelector.data = editor.graphModel()->knownSchedulerTypes;
            _newBlockSelector.open([graphModel = editor.graphModel(), owner = std::move(*owner)](std::string type) { sendEmplaceBlockMessage(*graphModel, owner, std::move(type)); });
        }
    };

    // owned by the editor: the reference stays valid
    editor.openGroupBlocksSelectorCallback = [this, &editor](std::vector<std::string> uniqueNames) {
        _newBlockSelector.data = editor.graphModel()->knownSchedulerTypes;
        _newBlockSelector.open([&editor, uniqueNames = std::move(uniqueNames)](std::string graphType) { //
            editor.requestBlocksGrouping(std::move(graphType), uniqueNames);
        });
    };

    // remote signals only in the root graph
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

void FlowgraphPage::drawUiControlWidget(const UiGraphBlock& control, float maxWidth) {
    if (!_dashboard) {
        return;
    }
    if (const auto block = findDrawableBlock(_dashboard->session, gr::UICategory::Toolbar, control.blockUniqueName)) {
        imw::Group widget; // sandboxes whatever the block does, e.g. a different behaviour when it ends with SameLine()
        auto       fullConfig = DrawableBlockConfig{.maxWidth = maxWidth, .drawingContext = DrawableBlockConfig::DrawingContext::FlowgraphView};
        if (control.ownerGraph) {
            control.ownerGraph->aggregateAndApplyControlledProperties(fullConfig, control);
        }
        std::ignore = block->draw(fullConfig.toMap().value_or(gr::property_map{}));
    }
}

void FlowgraphPage::drawNodeEditorTab() {
    auto contentSize    = ImGui::GetContentRegionAvail();
    auto contentTopLeft = ImGui::GetCursorPos();

    std::size_t level = 0UZ;
    for (auto& editor : _editors) {
        if (level != 0) {
            const float levelPadding = LookAndFeel::instance().flowgraph.nestedEditorInset;
            contentTopLeft += ImVec2(levelPadding, levelPadding);
            contentSize -= ImVec2(2 * levelPadding, 2 * levelPadding);
        }

        if (&editor == &_editors.back()) {
            editor.draw(contentTopLeft, contentSize);

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
    if (textButton("Reset") || _currentTabIsFlowGraph) {
        _currentTabIsFlowGraph = false;

        if (!_editors.empty()) {
            if (auto owner = _editors.front().ownersForRoot()) {
                _graphModel->send(gr::message::Command::Get, owner->scheduler, gr::scheduler::property::kGraphGRC);
            }
        }
    }

    ImGui::SameLine();
    if (textButton("Apply")) {
        if (auto owner = _editors.front().ownersForRoot()) {
            _graphModel->send(gr::message::Command::Set, owner->scheduler, gr::scheduler::property::kGraphGRC, {{"value", _graphModel->localFlowgraphGrc}});
        }
    }

    ImGui::InputTextMultiline("##grc", &_graphModel->localFlowgraphGrc, ImGui::GetContentRegionAvail());
}

void FlowgraphPage::draw() noexcept {
    if (_graphModel == nullptr) {
        return;
    }
    if (!showEditorControls) {
        drawLocalNodeEditor();
        return;
    }

    imw::TabBar tabBar("maintabbar", 0);

    if (auto item = imw::TabItem("Local", nullptr, 0)) {
        drawLocalNodeEditor();
    }

    if (auto item = imw::TabItem("Local - YAML", nullptr, 0)) {
        drawLocalYamlTab();
    }
}

} // namespace gr::od
