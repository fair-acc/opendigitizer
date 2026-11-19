#ifndef OPENDIGITIZER_UI_COMPONENTS_SELECTED_LABELS_VIEW_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SELECTED_LABELS_VIEW_HPP_

#include <array>
#include <string>
#include <vector>

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "FilterComboBoxes.hpp"

#include <format>

namespace gr::od {

inline auto xLabelExpectedWidth(const char* label) {
    const auto padding   = GImGui->Style.FramePadding.x;
    ImVec2     labelSize = ImGui::CalcTextSize(label, NULL, true);

    return labelSize.x + labelSize.y + 3 * padding;
}

inline bool xLabel(const char* label, const ImColor& color, const ImVec2& requestedSize = {}) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    ImGuiContext&     g          = *GImGui;
    const ImGuiStyle& style      = g.Style;
    const auto        padding    = style.FramePadding.x;
    const ImGuiID     id         = window->GetID(label);
    ImVec2            labelSize  = ImGui::CalcTextSize(label, NULL, true);
    const auto        textOffset = labelSize.y + padding;
    labelSize.x += textOffset + padding;

    ImVec2 pos  = window->DC.CursorPos;
    ImVec2 size = ImGui::CalcItemSize(requestedSize, labelSize.x + style.FramePadding.x * 2.0f, labelSize.y + style.FramePadding.y * 2.0f);

    ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);

    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, color, true, LookAndFeel::instance().selectedLabelRoundingPercentage * labelSize.y);

    bb.Min.x += padding;
    bb.Max.x -= textOffset + padding;
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label, NULL, &labelSize, style.ButtonTextAlign, &bb);

    {
        imw::Font font(hovered ? LookAndFeel::instance().fontIconsSolid : LookAndFeel::instance().fontIcons);

        const auto* iconX      = "";
        ImVec2      iconSize   = ImGui::CalcTextSize(iconX, NULL, true);
        const auto  iconOffset = (labelSize.y - iconSize.y) / 2;
        bb.Min.x               = bb.Max.x;
        bb.Max.x += textOffset;
        ImGui::RenderText(bb.Min + style.FramePadding + ImVec2{iconOffset, iconOffset}, iconX);
    }

    return pressed;
}

struct SelectedLabelsView {
    struct LabelInfo {
        std::string            display;
        FilterTag*             data  = nullptr;
        std::array<ImColor, 2> color = {};
    };

    std::vector<LabelInfo> labels;

    bool removeLabel(const FilterTag* data) {
        return std::erase_if(labels, [data](const LabelInfo& label) { return label.data == data; }) > 0UZ;
    }

    [[nodiscard]] FilterTag* draw() {
        auto contentWidth          = ImGui::GetContentRegionAvail().x;
        auto remainingContentWidth = contentWidth;
        bool first                 = true;

        FilterTag* toRemove = nullptr;

        for (const auto& label : labels) {
            auto desiredWidth = xLabelExpectedWidth(label.display.c_str());
            if (!first && remainingContentWidth > desiredWidth * 1.2f) {
                ImGui::SameLine();
            } else {
                remainingContentWidth = contentWidth;
            }
            remainingContentWidth -= desiredWidth * 1.2f;

            if (xLabel(label.display.c_str(), label.color[LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0 : 1])) {
                toRemove = label.data;
            }
            first = false;
        }

        if (toRemove) {
            removeLabel(toRemove);
        }

        return toRemove;
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_UI_COMPONENTS_SELECTED_LABELS_VIEW_HPP_
