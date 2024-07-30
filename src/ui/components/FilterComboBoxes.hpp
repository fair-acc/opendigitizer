#ifndef OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_FILTER_COMBO_BOXES_HPP_

#include <algorithm>
#include <string>
#include <vector>

#include "../common/ImguiWrap.hpp"

namespace DigitizerUi {

inline auto ColorComboBoxExpectedWidth(const std::string& label) {
    const auto padding  = GImGui->Style.FramePadding.x;
    ImVec2     textSize = ImGui::CalcTextSize(label.c_str(), NULL, true);

    // Space for the main title, plus the arrow and some extra spacetitle
    auto comboWidth = 1.3f * textSize.x + textSize.y + 3 * padding;
    return comboWidth;
}

template<typename ComboboxItem>
inline auto ColorComboBox(char* id, const std::string& label, ImColor color, double comboWidth, std::vector<ComboboxItem>& items) {
    std::optional<ComboboxItem*> result;

    ImGui::SetNextItemWidth(comboWidth);

    const bool somethingIsSelected = std::ranges::any_of(items, &ComboboxItem::isActive);
    if (!somethingIsSelected) {
        color = ImGuiCol_FrameBg;
    }

    IMW::StyleColor frameBg(ImGuiCol_FrameBg, color);
    IMW::StyleColor frameBgHovered(ImGuiCol_FrameBgHovered, color);
    IMW::StyleColor frameBgActive(ImGuiCol_FrameBgActive, color);
    IMW::StyleColor button(ImGuiCol_Button, color);
    IMW::StyleColor buttonHovered(ImGuiCol_ButtonHovered, color);
    IMW::StyleColor buttonActive(ImGuiCol_ButtonActive, color);

    if (auto combo = IMW::Combo(id, label.c_str(), 0)) {
        for (auto& item : items) {
            IMW::StyleColor itemBg(ImGuiCol_Header, color);
            if (ImGui::Selectable(item.title.c_str(), item.isActive)) {
                result = std::addressof(item);
            }
        }
    }

    return result;
}

template<typename ComboboxDefinition>
class FilterComboBoxes {
private:
    std::vector<ComboboxDefinition> m_comboboxes;

public:
    explicit FilterComboBoxes(std::vector<ComboboxDefinition> comboboxes = {}) : m_comboboxes(std::move(comboboxes)) {};

    void setData(std::vector<ComboboxDefinition> comboboxes) { m_comboboxes = std::move(comboboxes); }

    auto draw() {
        std::optional<std::remove_cvref_t<decltype(*std::declval<ComboboxDefinition>().items.begin())>*> result;

        auto contentWidth          = ImGui::GetContentRegionAvail().x;
        auto remainingContentWidth = contentWidth;
        bool first                 = true;

        for (auto& combo : m_comboboxes) {
            auto desiredWidth = ColorComboBoxExpectedWidth(combo.label.c_str());
            if (!first && remainingContentWidth > desiredWidth * 1.2) {
                ImGui::SameLine();
            } else {
                remainingContentWidth = contentWidth;
            }
            remainingContentWidth -= desiredWidth * 1.2;

            auto currentResult = ColorComboBox(combo.id.data(), combo.label, combo.color[LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0 : 1], desiredWidth, combo.items);
            if (currentResult) {
                result = currentResult;
            }
            first = false;
        }

        return result;
    }
};

} // namespace DigitizerUi

#endif
