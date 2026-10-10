#include "UiControlInteractionComponents.hpp"

#include "../blocks/TargetMap.hpp"

#include <imgui_node_editor.h>

namespace {

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

} // namespace DigitizerUi::components
