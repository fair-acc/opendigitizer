#ifndef OPENDIGITIZER_UI_COMPONENTS_UICONTROLMULTISELECTPOPUP_HPP
#define OPENDIGITIZER_UI_COMPONENTS_UICONTROLMULTISELECTPOPUP_HPP

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../GraphModel.hpp"
#include "../blocks/TargetMap.hpp"
#include "ScrollableListSelectionPopup.hpp"

namespace DigitizerUi::components {

/// Connects a UI control to a property of every block which has one, through the '*' selector.
class UiControlMultiSelectPopup {
    std::string                                    _controlUniqueName;
    ScrollableListSelectionPopup<CheckboxListItem> _popup;
    std::optional<std::size_t>                     _hoveredItem;

public:
    UiControlMultiSelectPopup(std::string controlUniqueName, UiGraphModel& model) //
        : _controlUniqueName(std::move(controlUniqueName)), _popup("Multi-Select", connectableProperties(_controlUniqueName, model), "No block has a property this control could set.") {}

    [[nodiscard]] std::string_view controlUniqueName() const { return _controlUniqueName; }

    /// the property hovered during the last draw(), so the blocks it would connect to can be highlighted
    [[nodiscard]] std::optional<std::string_view> hoveredProperty() const {
        if (!_hoveredItem) {
            return std::nullopt;
        }
        return std::string_view(_popup.items()[*_hoveredItem].name);
    }

    SelectionPopupState draw(UiGraphModel& model) {
        const ScrollableListSelectionPopupResult result = _popup.draw();
        _hoveredItem                                    = result.hoveredItem;
        if (result.currentState == SelectionPopupState::Confirmed) {
            applySelection(model);
        }
        return result.currentState;
    }

private:
    static std::vector<CheckboxListItem> connectableProperties(std::string_view controlUniqueName, UiGraphModel& model) {
        UiGraphBlock* control = model.recursiveFindBlockByUniqueName(controlUniqueName).block;
        if (!control) {
            return {};
        }
        const auto                    targets = TargetMap::fromString(control->blockSettings.value_or<std::string>("target_map", std::string{}));
        std::vector<CheckboxListItem> items;
        for (auto&& property : model.globConnectableProperties(*control)) {
            const bool connected = targets && targets->contains(TargetEntry{.blockTarget = "*", .propertyName = property});
            items.push_back(CheckboxListItem{.name = std::move(property), .selected = connected});
        }
        return items;
    }

    void applySelection(UiGraphModel& model) {
        UiGraphBlock* control = model.recursiveFindBlockByUniqueName(_controlUniqueName).block;
        if (!control) {
            return;
        }
        auto targets = TargetMap::fromString(control->blockSettings.value_or<std::string>("target_map", std::string{}));
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
};

} // namespace DigitizerUi::components

#endif
