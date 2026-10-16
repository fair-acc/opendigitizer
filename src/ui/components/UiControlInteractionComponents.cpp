#include "UiControlInteractionComponents.hpp"

#include <cassert>
#include <cfloat>
#include <cstdint>
#include <format>
#include <ranges>

#include "../FlowgraphLayout.hpp"
#include "../blocks/TargetMap.hpp"
#include "HoverRevealedTrashButton.hpp"
#include "MapUtils.hpp"
#include "OverlayButton.hpp"

#include <imgui_node_editor.h>

namespace {

constexpr float kUiControlNotchLengthInwards  = 5.F;
constexpr float kUiControlNotchLengthOutwards = 5.F;

[[nodiscard]] static ImVec2 uiControlConnectionLabelSize(std::string_view label) {
    using namespace DigitizerUi;
    auto* font = LookAndFeel::instance().fontTiny[LookAndFeel::instance().prototypeMode];
    return font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.F, label.data(), label.data() + label.size());
}

/// Distance between the left edge of a block and the right edge of the connection source block label
/// Must be called while the editor is active
[[nodiscard]] static float uiControlConnectionLabelGap() {
    // the part of the node padding not covered by the notch, plus some extra since imgui seems to underestimate text size
    return ax::NodeEditor::GetStyle().NodePadding.x - kUiControlNotchLengthInwards + ImGui::GetStyle().ItemInnerSpacing.x;
}

/// Draws the text @param outsideLabel to the left of the block, and a notch indicating the connection
/// from that to whatever text is inside the block at the current cursor position.
/// This does not move the cursor. Pass 0 to @param rowHeight to use the height of the drawn text instead.
/// Pass @param interactionId to make connection interactable for the Unlink and Jump to Control actions
DigitizerUi::components::UiControlConnectionNotchResult drawUiControlConnectionNotch(std::string_view outsideLabel, ImVec2 blockStartCursorPosition, float rowHeight, ImU32 color, const char* interactionId) {
    using namespace DigitizerUi;
    using namespace DigitizerUi::components;

    const ImVec4 nodePadding = ax::NodeEditor::GetStyle().NodePadding;
    const auto   blockLeft   = blockStartCursorPosition.x - nodePadding.x;

    assert(kUiControlNotchLengthInwards < nodePadding.x && "notch is going to draw on top of text");
    auto*      fgDrawList = ImGui::GetWindowDrawList();
    auto*      font       = LookAndFeel::instance().fontTiny[LookAndFeel::instance().prototypeMode];
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
        const ImRect notchRect{ImVec2{blockLeft - kUiControlNotchLengthOutwards, centerY - (textSize.y / 2.F)}, ImVec2{blockLeft + kUiControlNotchLengthInwards, centerY + (textSize.y / 2.F)}};
        notch               = components::overlayButtonBehavior(std::format("{}.unlink", interactionId).c_str(), notchRect);
        result.notchClicked = notch.clicked;
    }

    if (notch.hovered || notch.held) {
        using namespace std::string_view_literals;
        static constexpr auto unlinkIcon = "\u{f00d}"sv;

        ImFont*      iconFont = LookAndFeel::instance().fontIconsSolid;
        const float  iconFontSize{textSize.y};
        const ImVec2 iconSize = [iconFont, iconFontSize] {
            IMW::FontWithSize fontChange(iconFont, iconFontSize);
            return ImGui::CalcTextSize(unlinkIcon.data(), unlinkIcon.data() + unlinkIcon.size());
        }();
        const auto iconColor = LookAndFeel::getColorU32ImGui(notch.held ? &Palette::flowgraphUiControlUnlinkPressed : &Palette::flowgraphUiControlUnlink);
        fgDrawList->AddText(iconFont, iconFontSize, ImVec2{blockLeft, centerY} - (iconSize / 2.F), iconColor, unlinkIcon.data(), unlinkIcon.data() + unlinkIcon.size());
    } else {
        fgDrawList->AddLine(ImVec2{blockLeft - kUiControlNotchLengthOutwards, centerY}, ImVec2{blockLeft + kUiControlNotchLengthInwards, centerY}, color);
    }
    fgDrawList->AddText(font, font->LegacySize, labelPosition, color, outsideLabel.data(), outsideLabel.data() + outsideLabel.size());
    return result;
}

/// Must be called while the node editor is active so that coordinates (including mouse coordinates) are in canvas space
[[nodiscard]] const DigitizerUi::UiGraphBlock* blockUnderMouse(std::span<DigitizerUi::UiGraphBlock* const> drawnBlocks) {
    const ImVec2                     mousePosition = ImGui::GetMousePos();
    const DigitizerUi::UiGraphBlock* found         = nullptr;
    for (const DigitizerUi::UiGraphBlock* candidate : drawnBlocks) {
        const auto   nodeId   = ax::NodeEditor::NodeId(candidate);
        const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
        if (ImRect(position, position + ax::NodeEditor::GetNodeSize(nodeId)).Contains(mousePosition)) {
            found = candidate;
        }
    }
    return found;
}
} // namespace

namespace DigitizerUi::components {

void BlockDragConnectOverlay::draw() const {
    const auto  lineColor = LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlPendingConnection);
    const float thickness = 2.F;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(clipRect.Min, clipRect.Max, true);
    drawList->AddLine(lineStart, lineEnd, lineColor, thickness);
    if (dropTargetOutline) {
        drawList->AddRect(dropTargetOutline->Min, dropTargetOutline->Max, lineColor, 0, ImDrawFlags_None, 2.F * thickness);
    }
    drawList->PopClipRect();
}

BlockDragConnectOverlay BlockDragConnectInteraction::createOverlayDescription(std::span<UiGraphBlock* const> drawnBlocks, const std::function<bool(const UiGraphBlock&)>& isCompatibleDropTarget) const {
    const ImDrawList*     canvasDrawList = ImGui::GetWindowDrawList();
    const UiGraphBlock*   dropTarget     = blockUnderMouse(drawnBlocks);
    std::optional<ImRect> dropTargetOutline;
    if (dropTarget && isCompatibleDropTarget(*dropTarget)) {
        const auto   nodeId   = ax::NodeEditor::NodeId(dropTarget);
        const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
        dropTargetOutline     = ImRect(ax::NodeEditor::CanvasToScreen(position), ax::NodeEditor::CanvasToScreen(position + ax::NodeEditor::GetNodeSize(nodeId)));
    }
    return BlockDragConnectOverlay{
        .clipRect          = ImRect(ax::NodeEditor::CanvasToScreen(canvasDrawList->GetClipRectMin()), ax::NodeEditor::CanvasToScreen(canvasDrawList->GetClipRectMax())),
        .lineStart         = ax::NodeEditor::CanvasToScreen(_startPosition),
        .lineEnd           = ax::NodeEditor::CanvasToScreen(ImGui::GetMousePos()),
        .dropTargetOutline = dropTargetOutline,
    };
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

[[nodiscard]] float uiControlConnectionLabelsWidth(UiGraphModel& graphModel, const UiGraphBlock& block) {
    const auto controlledProperties = graphModel.uiControlledProperties(block);
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

bool drawUiControlDragDropHandle(const char* label, float availableWidth, ImVec2 blockStartCursorPosition) {
    const ImVec4 nodePadding  = ax::NodeEditor::GetStyle().NodePadding;
    const auto   blockTopLeft = blockStartCursorPosition - ImVec2{nodePadding.x, nodePadding.y};
    const ImVec2 handleSize{std::max(availableWidth - (nodePadding.x * 2.F), ImGui::GetFrameHeight()), ImGui::GetFrameHeight()};

    using namespace std::string_view_literals;
    static constexpr auto dragDropIcon = "\u{f58d}"sv;

    // horizontally center drag drop handle. recalculate offset from left, so it is easier to adjust width without messing up the drawing
    const ImVec2 cursorBefore = ImGui::GetCursorPos();
    const ImVec2 min          = ImVec2{blockTopLeft.x, ImGui::GetCursorScreenPos().y} + ImVec2{(availableWidth - handleSize.x) / 2.f, ImGui::GetStyle().FramePadding.y};
    const ImVec2 max          = min + handleSize;

    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(label, handleSize);
    const bool held    = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();

    // change color based on interaction. ImGui::GetColorU32 applies the global style alpha, so the
    // handle is greyed out along with the rest of a node that is filtered out
    ImVec4 fill = LookAndFeel::instance().palette().flowgraphUiControlFill;
    if (hovered || held) {
        fill.w = held ? 1.F : std::min(fill.w * 2.F, 1.F);
    }
    const auto fillColor      = ImGui::GetColorU32(fill);
    const auto highlightColor = ImGui::GetColorU32(LookAndFeel::instance().palette().flowgraphUiControlHighlight);

    // calculate positioning for icon to center it in the button
    const float  fontSize = 12.f;
    ImFont*      font     = LookAndFeel::instance().fontIconsSolidLarge;
    const ImVec2 iconSize = [font, fontSize] {
        IMW::FontWithSize fontChange(font, fontSize);
        return ImGui::CalcTextSize(dragDropIcon.data(), dragDropIcon.data() + dragDropIcon.size());
    }();
    const ImVec2 iconOffset = ((max - min) / 2.F) - iconSize / 2.F;

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, fillColor);
    drawList->AddRect(min, max, highlightColor, 0, ImDrawFlags_None, 3.F);
    drawList->AddText(font, fontSize, min + iconOffset, highlightColor, dragDropIcon.data(), dragDropIcon.data() + dragDropIcon.size());

    ImGui::SetCursorPos({cursorBefore.x, cursorBefore.y + handleSize.y + (ImGui::GetStyle().FramePadding.y * 2.F)});
    return held;
}

void drawUiControlTargetList(UiGraphModel& graphModel, const UiGraphBlock& control) {
    const auto targets = control.uiControlTargetMap();
    if (!targets || targets->entries().empty()) {
        return;
    }
    const auto rowTexts = targets->entries() | std::views::transform([](const TargetEntry& entry) { return std::format("{}:{}", entry.isGlob() ? "*" : entry.blockTarget, entry.propertyName); }) | std::ranges::to<std::vector>();

    ImGui::Spacing();
    IMW::Font   font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    const float spacing      = ImGui::GetStyle().ItemInnerSpacing.x;
    const float rowHeight    = ImGui::GetTextLineHeight();
    const float maxTextWidth = std::ranges::max(rowTexts | std::views::transform([](const std::string& text) { return ImGui::CalcTextSize(text.c_str()).x; }));

    for (const auto& [entry, text] : std::views::zip(targets->entries(), rowTexts)) {
        const ImVec2 rowMin     = ImGui::GetCursorScreenPos();
        const bool   rowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMin + ImVec2{maxTextWidth + spacing + rowHeight, rowHeight});

        ImGui::TextUnformatted(text.c_str());
        ImGui::SameLine(0.F, spacing + maxTextWidth - ImGui::GetItemRectSize().x);
        IMW::ChangeStrId rowId(std::format("{}.target.{}", control.blockName, text).c_str());
        if (components::hoverRevealedTrashButton(rowHovered, ImVec2{rowHeight, rowHeight})) {
            graphModel.removeUiControlConnection(control.blockUniqueName, entry.isGlob() ? std::string_view("*") : std::string_view(entry.blockTarget), entry.propertyName);
        }
    }
    ImGui::Spacing();
}

constexpr std::size_t kUiControlRegionMinimumNodesOnFirstRow = 3UZ;

std::optional<ImRect> calculateAndArrangeUiControlBlocksDesignatedRegion(UiGraphModel& graphModel, std::span<UiGraphBlock* const> controlBlocks, const ImRect& graphBounds) {
    if (controlBlocks.empty()) {
        return std::nullopt;
    }
    const auto nodeSizes = controlBlocks | std::views::transform([&graphModel](const UiGraphBlock* block) {
        const ImVec2 size = ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(block));
        return flowgraph_layout::Size{.width = uiControlConnectionLabelsWidth(graphModel, *block) + size.x, .height = size.y};
    }) | std::ranges::to<std::vector>();
    const auto rows      = flowgraph_layout::computeFixedBlockWrappingWithGap(nodeSizes, graphBounds.GetWidth(), kUiControlRegionGap, kUiControlRegionMinimumNodesOnFirstRow);

    // imgui node editor floors node positions internally
    const ImVec2 topLeft = ImFloor(ImVec2{graphBounds.Min.x, graphBounds.Min.y - kUiControlRegionGap - rows.containerSize.height});
    for (std::size_t i = 0UZ; i < controlBlocks.size(); ++i) {
        ax::NodeEditor::SetNodePosition(ax::NodeEditor::NodeId(controlBlocks[i]), ImFloor(topLeft + ImVec2{rows.offsets[i].x + uiControlConnectionLabelsWidth(graphModel, *controlBlocks[i]), rows.offsets[i].y}));
    }
    return ImRect(topLeft, topLeft + ImVec2{rows.containerSize.width, rows.containerSize.height});
}

// imgui node editor composites this draw channel index between the grid and
// the blocks of the node editor. see c_BackgroundChannel_SelectionRect
constexpr int kNodeEditorBackgroundDrawChannel = 5;

void drawUiControlRegionBackgroundRect(const ImRect& region) {
    constexpr std::uint8_t fillAlpha = 0x33;
    const std::uint32_t    opaque    = LookAndFeel::getColorU32Opaque(&Palette::flowgraphUiControlPendingConnection);
    auto* const            drawList  = ImGui::GetWindowDrawList();

    const int blockDrawChannel = drawList->_Splitter._Current;
    drawList->ChannelsSetCurrent(kNodeEditorBackgroundDrawChannel);
    drawList->AddRectFilled(region.Min, region.Max, rgbToImGuiABGR(opaque, fillAlpha));
    drawList->AddRect(region.Min, region.Max, rgbToImGuiABGR(opaque), 0, ImDrawFlags_None, 2.F);
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
    IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);

    // iterate blockSettings rather than indicators so that we draw in the same order the properties would be drawn in if they were not being indicated as connected
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

        // just draw the RHS property name (the target of the connection) only once, the rest are more entries on the left, beneath this first entry
        const auto&       currentPropertyMetaInformation = block.blockSettingsMetaInformation[std::string(propertyKey)];
        const std::string value                          = valueToString(propertyValue);
        const std::string displayName                    = currentPropertyMetaInformation.description.empty() ? std::string(propertyKey) : currentPropertyMetaInformation.description;
        ImGui::Text("%s: %s", displayName.c_str(), value.c_str());

        for (const auto& indicator : indicatorsForProperty | std::views::drop(1)) {
            ImGui::Dummy(ImVec2{0.F, drawLeftHandSideNotchAndBlockName(indicator, 0.F)});
        }
    }
}

} // namespace DigitizerUi::components
