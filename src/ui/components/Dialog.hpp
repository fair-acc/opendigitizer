#ifndef OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_

#include "../common/ImguiWrap.hpp"
#include "StyledStandardWidgets.hpp"
#include <imgui.h>

namespace DigitizerUi::components {

enum class DialogButton { None, Ok, Cancel };

inline DialogButton DialogButtons(bool okEnabled = true) {
    float y = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - ImGui::GetFrameHeight());
    ImGui::Separator();

    {
        IMW::Disabled disabled(!okEnabled);
        if (DigitizerUi::TextButton("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            ImGui::CloseCurrentPopup();
            return DialogButton::Ok;
        }
    }

    ImGui::SameLine();
    if (DigitizerUi::TextButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

} // namespace DigitizerUi::components

#endif
