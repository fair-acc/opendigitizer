#include "UiControlInteractionComponents.hpp"

#include <cassert>
#include <cfloat>
#include <format>
#include <ranges>

#include "../blocks/TargetMap.hpp"
#include "HoverRevealedTrashButton.hpp"
#include "MapUtils.hpp"

#include <imgui_node_editor.h>

namespace {

/// Draws the text @param outsideLabel to the left of the block, and a notch indicating the connection
/// from that to whatever text is inside the block at the current cursor position.
/// This does not move the cursor. Pass 0 to @param rowHeight to use the height of the drawn text instead.
/// Returns the height of the label.
float drawUiControlConnectionNotch(std::string_view outsideLabel, ImVec2 blockStartCursorPosition, float rowHeight, ImU32 color) {
    using namespace DigitizerUi;

    const ImVec4 nodePadding = ax::NodeEditor::GetStyle().NodePadding;
    const auto   blockLeft   = blockStartCursorPosition.x - nodePadding.x;

    const float notchLengthInwards  = 5.F;
    const float notchLengthOutwards = 5.F;

    assert(notchLengthInwards < nodePadding.x && "notch is going to draw on top of text");
    auto*      fgDrawList = ImGui::GetWindowDrawList();
    auto*      font       = LookAndFeel::instance().fontTiny[LookAndFeel::instance().prototypeMode];
    const auto textSize   = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.F, outsideLabel.data(), outsideLabel.data() + outsideLabel.size());
    // outerPadding is whatever inner padding is, plus some extra since imgui seems to underestimate text size
    const auto outerPadding = blockStartCursorPosition.x - (blockLeft + notchLengthInwards) + ImGui::GetStyle().ItemInnerSpacing.x;
    const auto centerY      = ImGui::GetCursorScreenPos().y + ((rowHeight > 0.F ? rowHeight : textSize.y) / 2.F);

    fgDrawList->AddLine(ImVec2{blockLeft - notchLengthOutwards, centerY}, ImVec2{blockLeft + notchLengthInwards, centerY}, color);
    fgDrawList->AddText(font, font->LegacySize, ImVec2{blockLeft - outerPadding - textSize.x, centerY - (textSize.y / 2.F)}, color, outsideLabel.data(), outsideLabel.data() + outsideLabel.size());
    return textSize.y;
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
    const ScrollableListSelectionPopupResult result = _popup.draw();
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
    const auto                    targets = control->uiControlTargetMap();
    std::vector<CheckboxListItem> items;
    for (auto&& property : target->connectableProperties(control->uiControlCompatibleTypeMap())) {
        const bool connected = targets && targets->relationshipToTarget(target->blockName, property) == TargetRelationship::TargetingSpecifically;
        items.push_back(CheckboxListItem{.name = std::move(property), .selected = connected});
    }
    return items;
}

void UiControlPropertyLinkPopup::applySelection(UiGraphModel& model) {
    UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_interaction.sourceUniqueName()).block;
    UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(_interaction.releasedOnUniqueName()).block;
    if (!control || !target) {
        return;
    }
    auto targets = control->uiControlTargetMap();
    if (!targets) {
        return;
    }

    for (const CheckboxListItem& property : _popup.items()) {
        const TargetEntry entry{.blockTarget = target->blockName, .propertyName = property.name};
        if (property.selected) {
            targets->addTarget(entry);
        } else {
            targets->removeTarget(entry);
        }
    }
    control->setSetting("target_map", targets->toString());
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

void drawIncomingPropertyConnectionIndicators(                       //
    UiGraphBlock&                                block,               //
    ImVec2                                       blockScreenPosition, //
    std::span<const PropertyConnectionIndicator> indicators           //
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

        const auto drawLeftHandSideNotchAndBlockName = [&](const components::PropertyConnectionIndicator& indicator, float rowHeight) { //
            return drawUiControlConnectionNotch(indicator.blockName, blockScreenPosition, rowHeight, indicator.color);
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
