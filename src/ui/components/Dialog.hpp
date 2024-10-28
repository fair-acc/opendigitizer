#ifndef OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_

namespace DigitizerUi::components {

enum class DialogButton { None, Ok, Cancel };

inline DialogButton DialogButtons(bool okEnabled = true) {
    float y = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - 20);
    ImGui::Separator();

    {
        IMW::Disabled disabled(!okEnabled);
        if (ImGui::Button("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            ImGui::CloseCurrentPopup();
            return DialogButton::Ok;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

} // namespace DigitizerUi::components

#endif
