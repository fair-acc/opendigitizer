#ifndef OPENDIGITIZER_UI_COMPONENTS_UICONTROLPROPERTYLINKPOPUP_HPP
#define OPENDIGITIZER_UI_COMPONENTS_UICONTROLPROPERTYLINKPOPUP_HPP

#include <string>
#include <utility>
#include <vector>

#include "../GraphModel.hpp"
#include "../blocks/TargetMap.hpp"
#include "BlockDragConnectInteraction.hpp"
#include "ScrollableListSelectionPopup.hpp"

namespace DigitizerUi::components {

/// Connects properties of the block a drag ended on to the UI control it started from.
class UiControlPropertyLinkPopup {
    BlockDragConnectInteraction                    _interaction;
    ScrollableListSelectionPopup<CheckboxListItem> _popup;

public:
    UiControlPropertyLinkPopup(BlockDragConnectInteraction interaction, UiGraphModel& model) //
        : _interaction(std::move(interaction)), _popup("Select Block Properties", connectableProperties(_interaction, model), "This block has no compatible properties to control.") {}

    SelectionPopupState draw(UiGraphModel& model) {
        const ScrollableListSelectionPopupResult result = _popup.draw();
        if (result.currentState == SelectionPopupState::Confirmed) {
            applySelection(model);
        }
        return result.currentState;
    }

private:
    static std::vector<CheckboxListItem> connectableProperties(const BlockDragConnectInteraction& interaction, UiGraphModel& model) {
        UiGraphBlock* control = model.recursiveFindBlockByUniqueName(interaction.sourceUniqueName()).block;
        UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(interaction.releasedOnUniqueName()).block;
        if (!control || !target) {
            return {};
        }
        const auto                    targets = TargetMap::fromString(control->blockSettings.value_or<std::string>("target_map", std::string{}));
        std::vector<CheckboxListItem> items;
        for (auto&& property : target->connectableProperties(control->uiControlValue())) {
            const bool connected = targets && targets->relationshipToTarget(target->blockName, property) == TargetRelationship::TargetingSpecifically;
            items.push_back(CheckboxListItem{.name = std::move(property), .selected = connected});
        }
        return items;
    }

    void applySelection(UiGraphModel& model) {
        UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_interaction.sourceUniqueName()).block;
        UiGraphBlock* target  = model.recursiveFindBlockByUniqueName(_interaction.releasedOnUniqueName()).block;
        if (!control || !target) {
            return;
        }
        auto targets = TargetMap::fromString(control->blockSettings.value_or<std::string>("target_map", std::string{}));
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
};

} // namespace DigitizerUi::components

#endif
