#ifndef OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <string>
#include <vector>

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

namespace gr::od {

inline auto colorComboBoxExpectedWidth(const std::string& label) {
    const auto padding  = GImGui->Style.FramePadding.x;
    ImVec2     textSize = ImGui::CalcTextSize(label.c_str(), NULL, true);

    auto comboWidth = LookAndFeel::instance().filterComboTextWidthScale * textSize.x + textSize.y + 3 * padding;
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

inline FilterTag* colorComboBox(char* id, const std::string& label, ImColor color, float comboWidth, std::vector<FilterTag>& items) {
    FilterTag* result = nullptr;

    ImGui::SetNextItemWidth(comboWidth);

    const bool somethingIsSelected = std::ranges::any_of(items, &FilterTag::isActive);
    if (!somethingIsSelected) {
        color = ImGuiCol_FrameBg;
    }

    imw::StyleColor frameBg(ImGuiCol_FrameBg, color);
    imw::StyleColor frameBgHovered(ImGuiCol_FrameBgHovered, color);
    imw::StyleColor frameBgActive(ImGuiCol_FrameBgActive, color);
    imw::StyleColor button(ImGuiCol_Button, color);
    imw::StyleColor buttonHovered(ImGuiCol_ButtonHovered, color);
    imw::StyleColor buttonActive(ImGuiCol_ButtonActive, color);

    if (auto combo = ComboBox(id, label.c_str(), 0)) {
        for (auto& item : items) {
            imw::StyleColor itemBg(ImGuiCol_Header, color);
            if (selectable(item.title.c_str(), item.isActive)) {
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
            float desiredWidth = colorComboBoxExpectedWidth(combo.label.c_str());
            if (!first && remainingContentWidth > desiredWidth * LookAndFeel::instance().filterComboWrapScale) {
                ImGui::SameLine();
            } else {
                remainingContentWidth = contentWidth;
            }
            remainingContentWidth -= desiredWidth * LookAndFeel::instance().filterComboWrapScale;

            if (FilterTag* currentResult = colorComboBox(combo.id.data(), combo.label, combo.color[LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0 : 1], desiredWidth, combo.items)) {
                result = currentResult;
            }
            first = false;
        }

        return result;
    }
};

} // namespace gr::od

#endif
