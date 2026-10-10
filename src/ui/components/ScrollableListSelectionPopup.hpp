#ifndef OPENDIGITIZER_UI_COMPONENTS_SCROLLABLELISTSELECTIONPOPUP_HPP
#define OPENDIGITIZER_UI_COMPONENTS_SCROLLABLELISTSELECTIONPOPUP_HPP

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi::components {

enum class SelectionPopupState { InProgress, Confirmed, Cancelled };

struct ScrollableListSelectionPopupResult {
    // this is a view into the ScrollableListSelectionPopup, it becomes invalid
    // when that is destroyed or draw() is called on it again
    std::span<const std::size_t> enabledItems;
    SelectionPopupState          currentState = SelectionPopupState::InProgress;
    std::optional<std::size_t>   hoveredItem;
};

// example type that can be given as a parameter to
// ScrollableListSelectionPopup. this one just has a line of text and a
// checkbox to enable or disable each line
struct CheckboxListItem {
    std::string name;
    bool        selected = false;

    bool draw() {
        ImGui::Checkbox(name.c_str(), &selected);
        return selected;
    }
};

/// This class stores the state needed for a drawing a popup containing a
/// scrollable list of items, each of which is selectable, and then handling
/// the user pressing a "Done" button at the bottom of the popup to confirm
/// their selection
template<typename T>
class ScrollableListSelectionPopup {
    std::string              _name;
    std::string              _emptyListMessage;
    std::vector<T>           _items;
    std::vector<std::size_t> _enabledItems;
    bool                     _opened = false;

public:
    ScrollableListSelectionPopup(std::string name, std::vector<T> items, std::string emptyListMessage = {}) : _name(std::move(name)), _emptyListMessage(std::move(emptyListMessage)), _items(std::move(items)) {}

    [[nodiscard]] std::span<const T> items() const { return _items; }

    [[nodiscard]] ScrollableListSelectionPopupResult draw() {
        if (!std::exchange(_opened, true)) {
            ImGui::OpenPopup(_name.c_str());
        }
        _enabledItems.clear();
        std::optional<std::size_t> hoveredItem;
        bool                       confirmed = false;

        if (auto popup = IMW::Popup(_name.c_str(), 0)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextUnformatted(_name.c_str());
            ImGui::Separator();
            {
                const float dpi = LookAndFeel::dpiScale();
                IMW::Child  list("##items", ImVec2(280.F * dpi, 180.F * dpi), true, 0);
                for (std::size_t index = 0UZ; index < _items.size(); ++index) {
                    if (_items[index].draw()) {
                        _enabledItems.push_back(index);
                    }
                    if (ImGui::IsItemHovered()) {
                        hoveredItem = index;
                    }
                }
                if (_items.empty() && !_emptyListMessage.empty()) {
                    IMW::StyleColor textColor(ImGuiCol_Text, LookAndFeel::instance().palette().errorColor);
                    ImGui::TextWrapped("%s", _emptyListMessage.c_str());
                }
            }

            const float doneWidth = 100.F * LookAndFeel::dpiScale();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (ImGui::GetContentRegionAvail().x - doneWidth) / 2.F));
            if (ImGui::Button("Done", ImVec2(doneWidth, 0.F))) {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }
        }

        const SelectionPopupState state = confirmed ? SelectionPopupState::Confirmed : (ImGui::IsPopupOpen(_name.c_str()) ? SelectionPopupState::InProgress : SelectionPopupState::Cancelled);
        return {.enabledItems = _enabledItems, .currentState = state, .hoveredItem = hoveredItem};
    }
};

} // namespace DigitizerUi::components

#endif
