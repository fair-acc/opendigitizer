#ifndef OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <string>
#include <vector>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledStandardWidgets.hpp"

namespace DigitizerUi {

inline auto ColorComboBoxExpectedWidth(const std::string& label) {
    const auto padding  = GImGui->Style.FramePadding.x;
    ImVec2     textSize = ImGui::CalcTextSize(label.c_str(), NULL, true);

    // Space for the main title, plus the arrow and some extra spacetitle
    auto comboWidth = 1.3f * textSize.x + textSize.y + 3 * padding;
    return comboWidth;
}

struct FilterTag {
    std::size_t category = 0UZ;
    std::string title;
    bool        isActive = false;

    auto operator<=>(const FilterTag&) const = default;
};

struct FilterCombo {
    std::string            id;
    std::string            label;
    std::array<ImColor, 2> color;
    std::vector<FilterTag> items;
};

inline FilterTag* ColorComboBox(char* id, const std::string& label, ImColor color, float comboWidth, std::vector<FilterTag>& items) {
    FilterTag* result = nullptr;

    ImGui::SetNextItemWidth(comboWidth);

    const bool somethingIsSelected = std::ranges::any_of(items, &FilterTag::isActive);
    if (!somethingIsSelected) {
        color = ImGuiCol_FrameBg;
    }

    IMW::StyleColor frameBg(ImGuiCol_FrameBg, color);
    IMW::StyleColor frameBgHovered(ImGuiCol_FrameBgHovered, color);
    IMW::StyleColor frameBgActive(ImGuiCol_FrameBgActive, color);
    IMW::StyleColor button(ImGuiCol_Button, color);
    IMW::StyleColor buttonHovered(ImGuiCol_ButtonHovered, color);
    IMW::StyleColor buttonActive(ImGuiCol_ButtonActive, color);

    if (auto combo = DigitizerUi::ComboBox(id, label.c_str(), 0)) {
        for (auto& item : items) {
            IMW::StyleColor itemBg(ImGuiCol_Header, color);
            if (DigitizerUi::Selectable(item.title.c_str(), item.isActive)) {
                result = std::addressof(item);
            }
        }
    }

    return result;
}

struct FilterComboBoxes {
    std::vector<FilterCombo> combos;

    [[nodiscard]] FilterTag* draw() {
        FilterTag* result = nullptr;

        auto contentWidth          = ImGui::GetContentRegionAvail().x;
        auto remainingContentWidth = contentWidth;
        bool first                 = true;

        for (FilterCombo& combo : combos) {
            float desiredWidth = ColorComboBoxExpectedWidth(combo.label.c_str());
            if (!first && remainingContentWidth > desiredWidth * 1.2f) {
                ImGui::SameLine();
            } else {
                remainingContentWidth = contentWidth;
            }
            remainingContentWidth -= desiredWidth * 1.2f;

            if (FilterTag* currentResult = ColorComboBox(combo.id.data(), combo.label, combo.color[LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0 : 1], desiredWidth, combo.items)) {
                result = currentResult;
            }
            first = false;
        }

        return result;
    }
};

} // namespace DigitizerUi

#endif
