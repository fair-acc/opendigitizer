#ifndef OPENDIGITIZER_UI_COMPONENTS_OVERLAY_BUTTON_HPP
#define OPENDIGITIZER_UI_COMPONENTS_OVERLAY_BUTTON_HPP

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <string_view>

namespace DigitizerUi::components {

struct OverlayButtonState {
    bool hovered = false;
    bool held    = false;
    bool clicked = false;
};

/// A button which does not affect layout. Unlike ImGui::InvisibleButton, it can be placed outside of a node, or on top of
/// other content of a node, without changing the size of the node. This function does not draw the button, only does the
/// behavior. Unlike ImGui::ButtonBehavior, it does declare a clipping area test engine info for addressability in tests
inline OverlayButtonState overlayButtonBehavior(const char* strId, const ImRect& rect) {
    ImGuiContext&      g  = *GImGui;
    const ImGuiID      id = ImGui::GetID(strId);
    OverlayButtonState state;
    if (!ImGui::ItemAdd(rect, id, nullptr, ImGuiItemFlags_NoNav)) {
        return state;
    }
    state.clicked = ImGui::ButtonBehavior(rect, id, &state.hovered, &state.held);
    IMGUI_TEST_ENGINE_ITEM_INFO(id, strId, g.LastItemData.StatusFlags);
    return state;
}

/// Returns the area relative to @param nodeTopRight that the filter button covers. can be used to draw and to calculate outline locations
inline ImRect nodeFilterButtonRect(ImVec2 nodeTopRight) {
    const float  sideLength = ImGui::GetTextLineHeight();
    const float  overhang   = LookAndFeel::instance().flowgraph.filterButtonOverhang * sideLength;
    const ImVec2 topRight{nodeTopRight.x + overhang, nodeTopRight.y - overhang};
    return ImRect{ImVec2{topRight.x - sideLength, topRight.y}, ImVec2{topRight.x, topRight.y + sideLength}};
}

/// Uses OverlayButton to draw a button with a filter icon covering @param rect
inline OverlayButtonState drawFilterToggleButton(const char* strId, const ImRect& rect, bool toggledOn, ImVec4 fillColor, ImVec4 outlineColor, float outlineThickness) {
    using namespace std::string_view_literals;
    static constexpr auto filterIcon = "\u{f0b0}"sv;

    const OverlayButtonState state = overlayButtonBehavior(strId, rect);

    const float  fontSize = rect.GetHeight() * 0.6F;
    ImFont*      font     = LookAndFeel::instance().fontIconsSolid;
    const ImVec2 iconSize = [font, fontSize] {
        IMW::FontWithSize fontChange(font, fontSize);
        return ImGui::CalcTextSize(filterIcon.data(), filterIcon.data() + filterIcon.size());
    }();

    auto*       drawList = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fillColor), rounding);
    if (toggledOn || state.hovered || state.held) {
        drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(toggledOn || state.held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), rounding);
    }
    drawList->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(outlineColor), rounding, ImDrawFlags_None, outlineThickness);
    drawList->AddText(font, fontSize, rect.GetCenter() - (iconSize / 2.F), ImGui::GetColorU32(toggledOn || state.hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), filterIcon.data(), filterIcon.data() + filterIcon.size());
    return state;
}
} // namespace DigitizerUi::components

#endif
