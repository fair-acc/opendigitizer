#include "imguiutils.h"

namespace ImGuiUtils {

DialogButton drawDialogButtons(bool okEnabled) {
    int y = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - 20);
    ImGui::Separator();

    if (!okEnabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Ok;
    }
    if (!okEnabled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

} // namespace ImGuiUtils
