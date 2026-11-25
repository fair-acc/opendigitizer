#ifndef OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_DIALOG_HPP_

#include "../common/Imw.hpp"
#include "SheetPopup.hpp"
#include "StyledWidgets.hpp"
#include <imgui.h>

namespace gr::od::components {

enum class DialogButton { None, Ok, Cancel };

inline DialogButton dialogButtons(bool okEnabled = true) {
    // sheet popups will already put the last item at the bottom
    if (!isCurrentPopupASheet()) {
        float y = ImGui::GetContentRegionAvail().y;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - ImGui::GetFrameHeight());
    }
    ImGui::Separator();

    {
        imw::Disabled disabled(!okEnabled);
        if (textButton("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
            closeCurrentPopup();
            return DialogButton::Ok;
        }
    }

    ImGui::SameLine();
    if (textButton("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        closeCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

} // namespace gr::od::components

#endif
