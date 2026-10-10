#include "JumpToControlPopup.hpp"

#include "../GraphModel.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp" // prevent recompilation when changing colors

namespace DigitizerUi::components {

bool JumpToControlPopup::draw(UiGraphModel* graphModel, std::function<void(const std::string&)> focusBlockCallback) {
    UiGraphBlock* block = graphModel ? graphModel->recursiveFindBlockByUniqueName(this->blockUniqueName).block : nullptr;
    if (!block) {
        return false;
    }

    constexpr const char* popupName = "Jump to Control";
    if (!std::exchange(this->opened, true)) {
        ImGui::OpenPopup(popupName);
    }

    if (auto popup = IMW::Popup(popupName, 0)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextUnformatted(popupName);
        ImGui::Separator();
        {
            const float dpi = LookAndFeel::dpiScale();
            IMW::Child  list("##controls", ImVec2(240.F * dpi, 140.F * dpi), true, 0);
            const auto  controlled    = graphModel->uiControlledProperties(*block);
            const auto  controllersIt = controlled.find(this->property);
            if (controllersIt != controlled.end()) {
                for (UiGraphBlock* control : controllersIt->second) {
                    // using a button instead of a selectable will prevent the selection from closing the popup
                    if (ImGui::Button(control->blockName.c_str(), ImVec2{0.f, ImGui::GetFrameHeightWithSpacing()}) && focusBlockCallback) {
                        focusBlockCallback(control->blockUniqueName);
                    }
                }
            }
        }
        const float doneWidth = 100.F * LookAndFeel::dpiScale();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (ImGui::GetContentRegionAvail().x - doneWidth) / 2.F));
        if (ImGui::Button("Done", ImVec2(doneWidth, 0.F))) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (this->opened && !ImGui::IsPopupOpen(popupName)) {
        return false;
    }
    return true;
}
} // namespace DigitizerUi::components
