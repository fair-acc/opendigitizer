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

/// Returns the area relative to @param nodeTopRight that the block menu button covers. can be used to draw and to calculate outline locations
inline ImRect nodeMenuButtonRect(ImVec2 nodeTopRight) {
    const float  sideLength = ImGui::GetTextLineHeight();
    const float  inset      = LookAndFeel::instance().flowgraph.menuButtonInset * sideLength;
    const ImVec2 topRight{nodeTopRight.x - inset, nodeTopRight.y + inset};
    return ImRect{ImVec2{topRight.x - sideLength, topRight.y}, ImVec2{topRight.x, topRight.y + sideLength}};
}

/// Uses OverlayButton to draw a button with a three vertical dots icon covering @param rect
inline OverlayButtonState drawMenuButton(const char* strId, const ImRect& rect) {
    using namespace std::string_view_literals;
    static constexpr auto kVerticalEllipsisIcon = "\u{f142}"sv;

    const OverlayButtonState state = overlayButtonBehavior(strId, rect);

    const float fontSize = rect.GetHeight() * 0.6F;
    ImFont*     font     = LookAndFeel::instance().fontIconsSolid;

    const Palette& palette  = LookAndFeel::instance().palette();
    auto*          drawList = ImGui::GetWindowDrawList();
    if (state.hovered || state.held) {
        const ImVec4 backgroundColor = state.held ? palette.flowgraphNodeMenuButtonActive : palette.flowgraphNodeMenuButtonHovered;
        drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(backgroundColor), LookAndFeel::instance().flowgraph.menuButtonRounding);
    }
    const ImVec4 iconColor = palette.mainWindowButtonIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    drawList->AddText(font, fontSize, rect.GetCenter() - IMW::calcGlyphBounds(font, fontSize, kVerticalEllipsisIcon).GetCenter(), ImGui::GetColorU32(iconColor), kVerticalEllipsisIcon.data(), kVerticalEllipsisIcon.data() + kVerticalEllipsisIcon.size());
    return state;
}
} // namespace DigitizerUi::components

#endif
