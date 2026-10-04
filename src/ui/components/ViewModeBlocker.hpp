#ifndef OPENDIGITIZER_UI_VIEWMODEBLOCKER_HPP
#define OPENDIGITIZER_UI_VIEWMODEBLOCKER_HPP

#include <imgui.h>
#include <imgui_internal.h>

#include "../common/ImguiWrap.hpp"
#include "YesNoPopup.hpp"

namespace DigitizerUi::components {

[[nodiscard]] inline bool drawViewModeBlocker(const ImRect& area) noexcept {
    ImGui::SetNextWindowSize(area.GetSize());
    ImGui::SetNextWindowPos(area.GetTL());
    IMW::Window window("coveringWindow", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar);
    const auto  unlockPopupID = "Return dashboard to interactive mode?##lockModeDisableInputBlockerPopup";
    ImGui::SetCursorScreenPos(area.GetTL());
    if (ImGui::InvisibleButton("inputBlocker", area.GetSize()) || (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        ImGui::OpenPopup(unlockPopupID);
    }

    bool exitRequested = false;
    if (const auto popup = beginYesNoPopup(unlockPopupID, {.yesText = "Make interactive"}); isPopupOpen(popup)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        if (isPopupConfirmed(popup)) {
            exitRequested = true;
        }
        ImGui::EndPopup();
    }
    return exitRequested;
}

} // namespace DigitizerUi::components

#endif
