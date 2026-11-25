#ifndef OPENDIGITIZER_UI_COMPONENTS_YES_NO_POPUP_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_YES_NO_POPUP_HPP_

#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace gr::od::components {

enum class YesNoPopupResult {
    PopupNotOpen,
    PopupOpenNothingPressed,
    PopupOpenYesPressed,
};

struct YesNoPopupOptions {
    const char* yesText   = "Yes";
    const char* noText    = "Cancel";
    const char* titleText = nullptr;
};

[[nodiscard]] constexpr bool isPopupOpen(YesNoPopupResult result) { return result != YesNoPopupResult::PopupNotOpen; }
[[nodiscard]] constexpr bool isPopupConfirmed(YesNoPopupResult result) { return result == YesNoPopupResult::PopupOpenYesPressed; }

/// if (isPopupOpen(beginYesNoPopup(...))) {
///
///     endPopup();
/// }
inline YesNoPopupResult beginYesNoPopup(const char* id, YesNoPopupOptions options, ImGuiWindowFlags windowFlags = ImGuiWindowFlags_None) {
    const auto  spacing     = ImGui::GetStyle().ItemSpacing;
    const float buttonSizeY = (ImGui::GetTextLineHeight() * 3) + spacing.y * 2;
    const float textHeight  = options.titleText ? ImGui::GetTextLineHeightWithSpacing() : 0.f;
    // beginPopup() will draw some centered title text, we should make sure we are at least wide enough to accommodate it
    // TODO: might be nicer to measure from the previous frame
    const float  headerWidth = std::max(ImGui::CalcTextSize(id, nullptr, true).x, options.titleText ? ImGui::CalcTextSize(options.titleText).x : 0.f);
    const float  popupWidth  = std::max(LookAndFeel::instance().yesNoPopupWidth, headerWidth + 2.f * ImGui::GetStyle().WindowPadding.x);
    const ImVec2 popupCenter = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 popupSize{popupWidth, (buttonSizeY * 3.f) + (spacing.y * 2.f) + textHeight};
    ImGui::SetNextWindowPos(popupCenter - popupSize / 2.f, ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2{popupWidth, 0.f}, ImGuiCond_Appearing);

    // store the result of yes/no on the popup so that we can play any closing animations and then return after that is done
    ImGuiStorage& storage      = *ImGui::GetStateStorage();
    const ImGuiID popupId      = ImGui::GetID(id);
    const ImGuiID yesPressedId = ImHashStr("##yesPressed", 0, popupId);

    using enum YesNoPopupResult;
    if (!beginPopup(id, PopupOptions{.flags = PopupFlags::WantsExclusiveFocus | PopupFlags::Modal, .desktopWindowFlags = windowFlags})) {
        storage.SetBool(yesPressedId, false);
        return PopupNotOpen;
    }
    if (options.titleText) {
        ImGui::TextUnformatted(options.titleText);
    }

    const float buttonSizeX = ImGui::GetContentRegionAvail().x;
    if (textButton(options.yesText, {buttonSizeX, buttonSizeY})) {
        storage.SetBool(yesPressedId, true);
        closeCurrentPopup();
    }
    if (textButton(options.noText, {buttonSizeX, buttonSizeY})) {
        closeCurrentPopup();
    }
    const bool isConfirmed = storage.GetBool(yesPressedId, false) && !ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_AnyPopupLevel);
    if (isConfirmed) {
        storage.SetBool(yesPressedId, false);
    }
    return isConfirmed ? PopupOpenYesPressed : PopupOpenNothingPressed;
}
} // namespace gr::od::components

#endif
