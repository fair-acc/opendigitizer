#ifndef OPENDIGITIZER_UI_COMPONENTS_YES_NO_POPUP_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_YES_NO_POPUP_HPP_

#include <imgui.h>

#include <optional>

namespace DigitizerUi::components {

enum class YesNoPopupResult {
    PopupNotOpen,
    PopupOpen_NothingPressed,
    PopupOpen_YesPressed,
};

struct YesNoPopupOptions {
    const char* yesText = "Yes";
    const char* noText  = "Cancel";
};

[[nodiscard]] constexpr bool isPopupOpen(YesNoPopupResult result) { return result != YesNoPopupResult::PopupNotOpen; }
[[nodiscard]] constexpr bool isPopupConfirmed(YesNoPopupResult result) { return result == YesNoPopupResult::PopupOpen_YesPressed; }

inline YesNoPopupResult beginYesNoPopup(const char* id, YesNoPopupOptions options) {
    const auto   spacing     = ImGui::GetStyle().ItemSpacing;
    const float  buttonSizeY = (ImGui::GetTextLineHeight() * 3) + spacing.y * 2;
    const ImVec2 popupCenter = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 popupSize{300.f, (buttonSizeY * 3.f) + (spacing.y * 2.f)};
    ImGui::SetNextWindowPos(popupCenter - popupSize / 2.f, ImGuiCond_Appearing, ImVec2{0.5f, 0.5f});
    ImGui::SetNextWindowSize(ImVec2{300, 0}, ImGuiCond_Appearing);

    using enum YesNoPopupResult;
    if (ImGui::BeginPopupModal(id)) {
        bool        pressedYes  = false;
        const float buttonSizeX = ImGui::GetContentRegionAvail().x;

        if (ImGui::Button(options.yesText, {buttonSizeX, buttonSizeY})) {
            pressedYes = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button(options.noText, {buttonSizeX, buttonSizeY})) {
            ImGui::CloseCurrentPopup();
        }
        return pressedYes ? PopupOpen_YesPressed : PopupOpen_NothingPressed;
    }
    return PopupNotOpen;
}
} // namespace DigitizerUi::components

#endif
