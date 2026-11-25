#include "UiControlInteractionComponents.hpp"

#include <cassert>
#include <cfloat>
#include <cstdint>
#include <format>
#include <ranges>

#include "../FlowgraphLayout.hpp"
#include "../blocks/TargetMap.hpp"
#include "MapUtils.hpp"
#include "OverlayButton.hpp"

#include <gnuradio-4.0/meta/utils.hpp>

#include <imgui_node_editor.h>

using namespace gr::od;

namespace {

struct UiControlConnectionNotchResult {
    float labelHeight  = 0.F;
    bool  labelClicked = false;
    bool  notchClicked = false;
};

[[nodiscard]] ImVec2 uiControlConnectionLabelSize(std::string_view label) {
    auto* font = LookAndFeel::instance().fontTiny();
    return font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.F, label.data(), label.data() + label.size());
}

/// from the left edge of a block to the right edge of the connection source block label; only while the editor is active
[[nodiscard]] float uiControlConnectionLabelGap() {
    // the part of the node padding not covered by the notch, plus some extra since imgui seems to underestimate text size
    return ax::NodeEditor::GetStyle().NodePadding.x - LookAndFeel::instance().flowgraph.uiControlNotchLengthInwards + ImGui::GetStyle().ItemInnerSpacing.x;
}

[[nodiscard]] ImVec4 Palette::*unlinkPaletteColor(bool pressed) { return pressed ? &Palette::flowgraphUiControlUnlinkPressed : &Palette::flowgraphUiControlUnlink; }

void drawUnlinkIcon(ImDrawList* drawList, ImVec2 center, float iconFontSize, bool pressed) {
    using namespace std::string_view_literals;
    static constexpr auto kUnlinkIcon = "\u{f00d}"sv;

    ImFont*      iconFont = LookAndFeel::instance().fontIconsSolid;
    const ImVec2 iconSize = [iconFont, iconFontSize] {
        imw::FontWithSize fontChange(iconFont, iconFontSize);
        return ImGui::CalcTextSize(kUnlinkIcon.data(), kUnlinkIcon.data() + kUnlinkIcon.size());
    }();
    const auto iconColor = LookAndFeel::getColorU32ImGui(unlinkPaletteColor(pressed));
    drawList->AddText(iconFont, iconFontSize, center - (iconSize / 2.F), iconColor, kUnlinkIcon.data(), kUnlinkIcon.data() + kUnlinkIcon.size());
}

[[nodiscard]] float uiControlTargetRowHeight() { return LookAndFeel::instance().fontTiny()->LegacySize; }

[[nodiscard]] float uiControlTargetRowPitch() { return uiControlTargetRowHeight() + LookAndFeel::instance().flowgraph.uiControlTargetRowSpacing; }

[[nodiscard]] std::size_t uiControlTargetEntryCount(const UiGraphBlock& control) {
    const auto targets = control.uiControlTargetMap();
    return targets ? targets->entries().size() : 0UZ;
}

[[nodiscard]] std::size_t uiControlTargetRowCount(std::size_t entryCount) {
    // the drag-connect handle always has a notch
    return std::max(entryCount, 1UZ);
}

[[nodiscard]] float uiControlOutgoingConnectionRowCenterOffsetFromBlockBottom(std::size_t index) { //
    return ImGui::GetStyle().ItemSpacing.y + (uiControlTargetRowHeight() / 2.F) + (static_cast<float>(index) * uiControlTargetRowPitch());
}

[[nodiscard]] ImVec2 uiControlDragHandleHitboxSize() {
    const auto& flowgraphLookAndFeel = LookAndFeel::instance().flowgraph;
    return ImVec2{flowgraphLookAndFeel.pinWidth, flowgraphLookAndFeel.pinHeight};
}

/// `outsideLabel` left of the block and a notch connecting it to the text inside the block at the current cursor position, which does not move;
/// `rowHeight` 0: the height of the drawn text; `interactionId` makes the connection interactable for the Unlink and Jump to Control actions
UiControlConnectionNotchResult drawUiControlConnectionNotch(std::string_view outsideLabel, ImVec2 blockStartCursorPosition, float rowHeight, ImU32 color, const char* interactionId) {
    using namespace components;

    const ImVec4 nodePadding = ax::NodeEditor::GetStyle().NodePadding;
    const auto   blockLeft   = blockStartCursorPosition.x - nodePadding.x;

    assert(LookAndFeel::instance().flowgraph.uiControlNotchLengthInwards < nodePadding.x && "notch is going to draw on top of text");
    auto*      fgDrawList = ImGui::GetWindowDrawList();
    auto*      font       = LookAndFeel::instance().fontTiny();
    const auto textSize   = uiControlConnectionLabelSize(outsideLabel);
    const auto centerY    = ImGui::GetCursorScreenPos().y + ((rowHeight > 0.F ? rowHeight : textSize.y) / 2.F);

    const ImVec2 labelPosition{blockLeft - uiControlConnectionLabelGap() - textSize.x, centerY - (textSize.y / 2.F)};

    UiControlConnectionNotchResult result{.labelHeight = textSize.y};
    if (interactionId) {
        const auto label    = components::overlayButtonBehavior(std::format("{}.label", interactionId).c_str(), ImRect{labelPosition, labelPosition + textSize});
        result.labelClicked = label.clicked;
        if (label.hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            fgDrawList->AddLine(labelPosition + ImVec2{0.F, textSize.y}, labelPosition + textSize, color);
        }
    }

    components::OverlayButtonState notch;
    if (interactionId) {
        const ImRect notchRect{ImVec2{blockLeft - LookAndFeel::instance().flowgraph.uiControlNotchLengthOutwards, centerY - (textSize.y / 2.F)}, ImVec2{blockLeft + LookAndFeel::instance().flowgraph.uiControlNotchLengthInwards, centerY + (textSize.y / 2.F)}};
        notch               = components::overlayButtonBehavior(std::format("{}.unlink", interactionId).c_str(), notchRect);
        result.notchClicked = notch.clicked;
    }

    if (notch.hovered || notch.held) {
        drawUnlinkIcon(fgDrawList, ImVec2{blockLeft, centerY}, textSize.y, notch.held);
    } else {
        // the outside and inside label texts may differ in colour: a gradient between them on the notch makes that less noticeable
        const ImU32 insideColor = ImGui::GetColorU32(ImGuiCol_Text);
        fgDrawList->AddRectFilledMultiColor(ImVec2{blockLeft - LookAndFeel::instance().flowgraph.uiControlNotchLengthOutwards, centerY - .5F}, ImVec2{blockLeft + LookAndFeel::instance().flowgraph.uiControlNotchLengthInwards, centerY + .5F}, color, insideColor, insideColor, color);
    }
    const ImU32 labelColor = notch.hovered || notch.held ? LookAndFeel::getColorU32ImGui(unlinkPaletteColor(notch.held)) : color;
    fgDrawList->AddText(font, font->LegacySize, labelPosition, labelColor, outsideLabel.data(), outsideLabel.data() + outsideLabel.size());
    return result;
}

} // namespace

namespace gr::od::components {

void BlockDragConnectOverlay::draw() const {
    const auto  lineColor = LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlPendingConnection);
    const float thickness = LookAndFeel::instance().flowgraph.highlightThickness;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(clipRect.Min, clipRect.Max, true);
    drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
    drawList->PopClipRect();
}

BlockDragConnectOverlay BlockDragConnectInteraction::createOverlayDescription() const {
    const ImDrawList* canvasDrawList = ImGui::GetWindowDrawList();
    return BlockDragConnectOverlay{
        .clipRect  = ImRect(ax::NodeEditor::CanvasToScreen(canvasDrawList->GetClipRectMin()), ax::NodeEditor::CanvasToScreen(canvasDrawList->GetClipRectMax())),
        .lineStart = ax::NodeEditor::CanvasToScreen(_startPosition),
        .lineEnd   = ax::NodeEditor::CanvasToScreen(ImGui::GetMousePos()),
    };
}

const UiGraphBlock* BlockDragConnectInteraction::blockUnderMouse(std::span<UiGraphBlock* const> drawnBlocks) {
    const ImVec2        mousePosition = ImGui::GetMousePos();
    const UiGraphBlock* found         = nullptr;
    for (const UiGraphBlock* candidate : drawnBlocks) {
        const auto   nodeId   = ax::NodeEditor::NodeId(candidate);
        const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
        if (ImRect(position, position + ax::NodeEditor::GetNodeSize(nodeId)).Contains(mousePosition)) {
            found = candidate;
        }
    }
    return found;
}

bool BlockDragConnectInteraction::update(std::span<UiGraphBlock* const> drawnBlocks) {
    if (_released) {
        return true;
    }

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return false;
    }
    _released = true;
    if (const UiGraphBlock* dropTarget = blockUnderMouse(drawnBlocks)) {
        _releasedOnUniqueName = dropTarget->blockUniqueName;
    }
    _cancelled = _releasedOnUniqueName == _sourceUniqueName;
    return true;
}

ScrollableListSelectionPopupResult ScrollableListSelectionPopup::draw(const std::function<bool(std::size_t)>& isItemEnabled) {
    if (!std::exchange(_opened, true)) {
        ImGui::OpenPopup(_name.c_str());
    }
    std::optional<std::size_t> hoveredItem;
    bool                       confirmed = false;

    if (auto popup = Popup(_name.c_str(), PopupOptions{})) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closeCurrentPopup();
        }
        ImGui::TextUnformatted(_name.c_str());
        ImGui::Separator();
        {
            imw::Child list("##items", LookAndFeel::instance().selectionPopupListSize, true, 0);
            for (std::size_t index = 0UZ; index < _items.size(); ++index) {
                imw::Disabled incompatible(isItemEnabled && !isItemEnabled(index));
                _items[index].draw();
                if (ImGui::IsItemHovered()) {
                    hoveredItem = index;
                }
            }
            if (_items.empty() && !_emptyListMessage.empty()) {
                imw::StyleColor textColor(ImGuiCol_Text, LookAndFeel::instance().palette().errorColor);
                ImGui::TextWrapped("%s", _emptyListMessage.c_str());
            }
        }

        const float doneWidth = LookAndFeel::instance().selectionPopupDoneWidth;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (ImGui::GetContentRegionAvail().x - doneWidth) / 2.F));
        if (textButton("Done", ImVec2(doneWidth, 0.F))) {
            confirmed = true;
            closeCurrentPopup();
        }
    }

    const SelectionPopupState unconfirmedState = ImGui::IsPopupOpen(_name.c_str()) ? SelectionPopupState::InProgress : SelectionPopupState::Cancelled;
    const SelectionPopupState state            = confirmed ? SelectionPopupState::Confirmed : unconfirmedState;
    return {.currentState = state, .hoveredItem = hoveredItem};
}

std::optional<std::string_view> UiControlMultiSelectPopup::hoveredProperty() const {
    if (!_hoveredItem.has_value()) {
        return std::nullopt;
    }
    return std::string_view(_popup.items()[*_hoveredItem].name);
}

SelectionPopupState UiControlMultiSelectPopup::draw(UiGraphModel& model) {
    const ScrollableListSelectionPopupResult result = _popup.draw();
    _hoveredItem                                    = result.hoveredItem;
    if (result.currentState == SelectionPopupState::Confirmed) {
        applySelection(model);
    }
    return result.currentState;
}

std::vector<CheckboxListItem> UiControlMultiSelectPopup::connectableProperties(std::string_view controlUniqueName, UiGraphModel& model) {
    UiGraphBlock* control = model.recursiveFindBlockByUniqueName(controlUniqueName).block;
    if (!control || !control->isUiControl() || !control->usesTargetMap()) {
        return {};
    }
    const auto                    targets = control->uiControlTargetMap();
    std::vector<CheckboxListItem> items;
    for (auto&& property : model.globConnectableProperties(*control)) {
        const bool connected = targets && targets->contains(TargetEntry{.blockTarget = "*", .propertyName = property});
        items.push_back(CheckboxListItem{.name = std::move(property), .selected = connected});
    }
    return items;
}

void UiControlMultiSelectPopup::applySelection(UiGraphModel& model) {
    UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_controlUniqueName).block;
    if (!control) {
        return;
    }
    auto targets = control->uiControlTargetMap();
    if (!targets) {
        return;
    }

    for (const CheckboxListItem& property : _popup.items()) {
        const TargetEntry entry{.blockTarget = "*", .propertyName = property.name};
        if (property.selected) {
            targets->addTarget(entry);
        } else {
            targets->removeTarget(entry);
        }
    }
    control->setSetting("target_map", targets->toString());
}

SelectionPopupState UiControlPropertyLinkPopup::draw(UiGraphModel& model) {
    const UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_interaction.sourceUniqueName()).block;
    const UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(_interaction.releasedOnUniqueName()).block;
    // the checked items are the same for every item, so the properties controlled with them applied are resolved once
    const auto controlledWithSelection = [&]() -> std::optional<std::vector<UiGraphModel::BlockPointerPropertyNamePair>> {
        if (!control || !target) {
            return std::nullopt;
        }
        const auto targets = control->uiControlTargetMap();
        if (!targets) {
            return std::nullopt;
        }
        return model.resolveControlledProperties(*control, withSelectionApplied(*targets, *target));
    }();
    const auto isItemEnabled = [&](std::size_t index) {
        const CheckboxListItem& item = _popup.items()[index];
        return item.selected || (controlledWithSelection && UiGraphModel::isCompatibleWithControlledProperties(*control, *controlledWithSelection, {target, item.name}));
    };
    const ScrollableListSelectionPopupResult result = _popup.draw(isItemEnabled);
    if (result.currentState == SelectionPopupState::Confirmed) {
        applySelection(model);
    }
    return result.currentState;
}

std::vector<CheckboxListItem> UiControlPropertyLinkPopup::connectableProperties(const BlockDragConnectInteraction& interaction, UiGraphModel& model) {
    UiGraphBlock* control = model.recursiveFindBlockByUniqueName(interaction.sourceUniqueName()).block;
    UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(interaction.releasedOnUniqueName()).block;
    if (!control || !target) {
        return {};
    }
    const auto targets = control->uiControlTargetMap();
    if (!targets) {
        return {};
    }
    const auto                    controlled = model.resolveControlledProperties(*control, *targets);
    std::vector<CheckboxListItem> items;
    for (auto&& property : target->connectableProperties(control->uiControlCompatibleTypeMap())) {
        const bool connected = targets->relationshipToTarget(target->blockName, property) == TargetRelationship::TargetingSpecifically;
        if (connected || UiGraphModel::isCompatibleWithControlledProperties(*control, controlled, {target, property})) {
            items.push_back(CheckboxListItem{.name = std::move(property), .selected = connected});
        }
    }
    return items;
}

void UiControlPropertyLinkPopup::applySelection(UiGraphModel& model) {
    UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_interaction.sourceUniqueName()).block;
    UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(_interaction.releasedOnUniqueName()).block;
    if (!control || !target) {
        return;
    }
    const auto targets = control->uiControlTargetMap();
    if (!targets) {
        return;
    }
    control->setSetting("target_map", withSelectionApplied(*targets, *target).toString());
}

TargetMap UiControlPropertyLinkPopup::withSelectionApplied(TargetMap targets, const UiGraphBlock& target) const {
    for (const CheckboxListItem& property : _popup.items()) {
        const TargetEntry entry{.blockTarget = target.blockName, .propertyName = property.name};
        if (property.selected) {
            targets.addTarget(entry);
        } else {
            targets.removeTarget(entry);
        }
    }
    return targets;
}

[[nodiscard]] float uiControlConnectionLabelsWidth(const UiGraphModel::ControlledPropertyMap& controlledProperties) {
    if (controlledProperties.empty()) {
        return 0.F;
    }
    float widestLabel = 0.F;
    for (const auto& controls : controlledProperties | std::views::values) {
        for (const UiGraphBlock* control : controls) {
            widestLabel = std::max(widestLabel, uiControlConnectionLabelSize(control->blockName).x);
        }
    }
    return uiControlConnectionLabelGap() + widestLabel;
}

std::optional<ImVec2> drawUiControlTargetList(UiGraphModel& graphModel, const UiGraphBlock& control, const ImRect& node) {
    if (!control.usesTargetMap()) {
        return std::nullopt;
    }
    const auto                         targets = control.uiControlTargetMap();
    const std::span<const TargetEntry> entries = targets ? targets->entries() : std::span<const TargetEntry>{};

    const auto&  flowgraphLookAndFeel = LookAndFeel::instance().flowgraph;
    const float  rowHeight            = uiControlTargetRowHeight();
    const float  lineX                = node.Min.x + ax::NodeEditor::GetStyle().NodePadding.x;
    const ImVec4 color                = LookAndFeel::instance().palette().flowgraphUiControlConnectionIndicator;
    const auto   calculateRowCenter   = [&](std::size_t index) { return ImVec2{lineX, node.Max.y + uiControlOutgoingConnectionRowCenterOffsetFromBlockBottom(index)}; };
    const auto   lastRowIndex         = uiControlTargetRowCount(entries.size()) - 1UZ;
    const ImVec2 handleHitboxSize     = uiControlDragHandleHitboxSize();
    const auto   lineSegmentAboveRow  = [&](std::size_t index) {
        const float top = index == 0UZ ? node.Max.y : calculateRowCenter(index - 1UZ).y;
        return ImRect{ImVec2{lineX - (handleHitboxSize.x / 2.F), top}, ImVec2{lineX + (handleHitboxSize.x / 2.F), calculateRowCenter(index).y}};
    };
    const float labelLeft = lineX + (handleHitboxSize.x / 2.F) + ImGui::GetStyle().ItemInnerSpacing.x;

    const ImVec2 dragHandleTopCenter   = calculateRowCenter(lastRowIndex);
    const auto   dragHandleInteraction = components::overlayButtonBehavior( //
        std::format("{}.uiDragHandle", control.blockName).c_str(),        //
        ImRect{dragHandleTopCenter - ImVec2{handleHitboxSize.x / 2.F, 0.F}, dragHandleTopCenter + ImVec2{handleHitboxSize.x / 2.F, handleHitboxSize.y}});
    const bool   isDragHandleActive    = dragHandleInteraction.hovered || dragHandleInteraction.held;
    if (isDragHandleActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    // GetColorU32 applies the style alpha, which greys out a filtered node
    const ImU32 lineColor = ImGui::GetColorU32(isDragHandleActive ? LookAndFeel::instance().palette().flowgraphUiControlHighlight : color);

    auto*             drawList = ImGui::GetWindowDrawList();
    imw::FontWithSize font(LookAndFeel::instance().fontTiny(), rowHeight);
    imw::StyleColor   textColor(ImGuiCol_Text, color);
    // AddLine offsets its endpoints by half a pixel but AddCircleFilled does not: needed to centre a circle on the line
    const float lineHalfThickness = flowgraphLookAndFeel.uiControlTargetLineThickness / 2.F;
    const auto  drawLineSegment   = [&](const ImRect& segment) { drawList->AddRectFilled(ImVec2{lineX - lineHalfThickness, segment.Min.y}, ImVec2{lineX + lineHalfThickness, segment.Max.y}, lineColor); };

    if (entries.empty()) {
        drawLineSegment(lineSegmentAboveRow(0UZ));
    }

    std::size_t index = 0;
    for (const auto& entry : entries) {
        gr::on_scope_exit increment = [&index] { ++index; };

        const std::string text      = std::format("{}:{}", entry.isGlob() ? "*" : entry.blockTarget, entry.propertyName);
        const ImVec2      dotCenter = calculateRowCenter(index);
        const ImRect      segment   = lineSegmentAboveRow(index);

        const auto unlinkInteractionState = components::overlayButtonBehavior(std::format("{}.target.{}.unlink", control.blockName, text).c_str(), segment);
        if (unlinkInteractionState.hovered || unlinkInteractionState.held) {
            drawUnlinkIcon(drawList, segment.GetCenter(), rowHeight, unlinkInteractionState.held);
        } else {
            drawLineSegment(segment);
        }
        if (index != lastRowIndex) {
            drawList->AddCircleFilled(dotCenter, flowgraphLookAndFeel.uiControlTargetDotRadius, lineColor);
        }

        imw::StyleColor labelColor(ImGuiCol_Text, unlinkInteractionState.hovered || unlinkInteractionState.held ? LookAndFeel::instance().palette().*unlinkPaletteColor(unlinkInteractionState.held) : color);
        ImGui::RenderTextEllipsis(drawList, ImVec2{labelLeft, dotCenter.y - (rowHeight / 2.F)}, ImVec2{node.Max.x, dotCenter.y + (rowHeight / 2.F)}, node.Max.x, text.c_str(), nullptr, nullptr);

        if (unlinkInteractionState.clicked) {
            graphModel.removeUiControlConnection(control.blockUniqueName, entry.isGlob() ? std::string_view("*") : std::string_view(entry.blockTarget), entry.propertyName);
        }
    }

    const float  dragHandleRadius = isDragHandleActive ? flowgraphLookAndFeel.uiControlDragHandleHoveredRadius : flowgraphLookAndFeel.uiControlDragHandleRadius;
    const ImVec2 dragHandleCenter = dragHandleTopCenter + ImVec2{0.F, dragHandleRadius};
    drawList->AddCircleFilled(dragHandleCenter, dragHandleRadius, lineColor);
    if (!dragHandleInteraction.held) {
        return std::nullopt;
    }
    return dragHandleCenter;
}

[[nodiscard]] float uiControlTargetListHeight(const UiGraphBlock& block) {
    if (!block.isUiControl() || !block.usesTargetMap()) {
        return 0.F;
    }
    const float lastRowCenterOffset = uiControlOutgoingConnectionRowCenterOffsetFromBlockBottom(uiControlTargetRowCount(uiControlTargetEntryCount(block)) - 1UZ);
    return lastRowCenterOffset + std::max(uiControlTargetRowHeight() / 2.F, uiControlDragHandleHitboxSize().y);
}

constexpr std::size_t kUiControlRegionMinimumNodesOnFirstRow = 3UZ;

std::optional<ImRect> calculateAndArrangeUiControlBlocksDesignatedRegion(std::span<const UiGraphModel::ParsedUiControlTargetMap> parsedUiControlTargetMaps, std::span<UiGraphBlock* const> controlBlocks, const ImRect& graphBounds) {
    if (controlBlocks.empty()) {
        return std::nullopt;
    }
    const auto connectionLabelsWidth = [parsedUiControlTargetMaps](const UiGraphBlock& block) { return uiControlConnectionLabelsWidth(UiGraphModel::uiControlledProperties(block, parsedUiControlTargetMaps)); };
    const auto nodeSizes             = controlBlocks | std::views::transform([&connectionLabelsWidth](const UiGraphBlock* block) {
        const ImVec2 size = ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(block));
        return flowgraph_layout::Size{.width = connectionLabelsWidth(*block) + size.x, .height = size.y + uiControlTargetListHeight(*block)};
    }) | std::ranges::to<std::vector>();
    const auto rows                  = flowgraph_layout::computeFixedBlockWrappingWithGap(nodeSizes, graphBounds.GetWidth(), LookAndFeel::instance().flowgraph.uiControlRegionGap, kUiControlRegionMinimumNodesOnFirstRow);

    // imgui node editor floors node positions internally
    const ImVec2 topLeft = ImFloor(ImVec2{graphBounds.Min.x, graphBounds.Min.y - LookAndFeel::instance().flowgraph.uiControlRegionGap - rows.containerSize.height});
    for (std::size_t i = 0UZ; i < controlBlocks.size(); ++i) {
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(controlBlocks[i]), ImFloor(topLeft + ImVec2{rows.offsets[i].x + connectionLabelsWidth(*controlBlocks[i]), rows.offsets[i].y}));
    }
    return ImRect(topLeft, topLeft + ImVec2{rows.containerSize.width, rows.containerSize.height});
}

// imgui node editor composites this draw channel index between the grid and
// the blocks of the node editor. see c_BackgroundChannel_SelectionRect
constexpr int kNodeEditorBackgroundDrawChannel = 5;

void drawUiControlRegionBackgroundRect(const ImRect& region) {
    constexpr std::uint8_t kFillAlpha = 0x33;
    const std::uint32_t    opaque     = LookAndFeel::getColorU32Opaque(&Palette::flowgraphUiControlPendingConnection);
    auto* const            drawList   = ImGui::GetWindowDrawList();

    const int blockDrawChannel = drawList->_Splitter._Current;
    drawList->ChannelsSetCurrent(kNodeEditorBackgroundDrawChannel);
    drawList->AddRectFilled(region.Min, region.Max, rgbToImGuiABGR(opaque, kFillAlpha), LookAndFeel::instance().flowgraph.nodeRounding);
    drawList->AddRect(region.Min, region.Max, rgbToImGuiABGR(opaque), LookAndFeel::instance().flowgraph.nodeRounding, ImDrawFlags_None, LookAndFeel::instance().flowgraph.uiControlRegionBorderThickness);
    drawList->ChannelsSetCurrent(blockDrawChannel);
}

void drawIncomingPropertyConnectionIndicators(                                                      //
    UiGraphBlock&                                                              block,               //
    ImVec2                                                                     blockScreenPosition, //
    std::span<const components::PropertyConnectionIndicator>                   indicators,          //
    const std::function<void(const components::PropertyConnectionIndicator&)>& onLabelClicked,      //
    const std::function<void(const components::PropertyConnectionIndicator&)>& onNotchClicked       //
) {
    if (indicators.empty()) {
        return;
    }
    imw::Font font(LookAndFeel::instance().fontSmall());

    // blockSettings order, not indicator order: as the properties are drawn when not connected
    for (const auto& [propertyKey, propertyValue] : block.blockSettings) {
        auto indicatorsForProperty = indicators | std::views::filter([&propertyKey](const auto& indicator) { return indicator.propertyName == propertyKey; });
        if (std::ranges::empty(indicatorsForProperty)) {
            continue;
        }

        const auto drawLeftHandSideNotchAndBlockName = [&](const components::PropertyConnectionIndicator& indicator, float rowHeight) {
            const std::string interactionId = std::format("{}.{}.{}", block.blockName, propertyKey, indicator.blockName);
            const auto        notch         = drawUiControlConnectionNotch(indicator.blockName, blockScreenPosition, rowHeight, indicator.color, indicator.isInteractive ? interactionId.c_str() : nullptr);
            if (notch.labelClicked) {
                onLabelClicked(indicator);
            }
            if (notch.notchClicked) {
                onNotchClicked(indicator);
            }
            return notch.labelHeight;
        };

        drawLeftHandSideNotchAndBlockName(*indicatorsForProperty.begin(), ImGui::GetTextLineHeight());

        // the target property name only once; further connections are entries on the left, beneath the first
        const auto&       currentPropertyMetaInformation = block.blockSettingsMetaInformation[std::string(propertyKey)];
        const std::string value                          = valueToString(propertyValue);
        const std::string displayName                    = currentPropertyMetaInformation.description.empty() ? std::string(propertyKey) : currentPropertyMetaInformation.description;
        ImGui::Text("%s: %s", displayName.c_str(), value.c_str());

        for (const auto& indicator : indicatorsForProperty | std::views::drop(1)) {
            ImGui::Dummy(ImVec2{0.F, drawLeftHandSideNotchAndBlockName(indicator, 0.F)});
        }
    }
}

} // namespace gr::od::components
