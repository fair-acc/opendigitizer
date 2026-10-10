#include "ModifyUiConnectionsPopup.hpp"

#include "../GraphModel.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace {
std::size_t countOtherBlocksWithProperty(DigitizerUi::UiGraphModel& model, const DigitizerUi::UiGraphBlock& block, const std::string& property) {
    std::size_t count = 0UZ;
    forEachBlockRecursive(model.rootBlock, [&](DigitizerUi::UiGraphBlock& candidate) {
        if (std::addressof(candidate) != std::addressof(block) && candidate.blockSettings.contains(property)) {
            ++count;
        }
    });
    return count;
}
} // namespace

namespace DigitizerUi::components {

bool ModifyUiConnectionsPopup::draw(UiGraphModel* graphModel) {
    UiGraphBlock* block = graphModel ? graphModel->recursiveFindBlockByUniqueName(this->blockUniqueName).block : nullptr;
    if (!block) {
        return false;
    }

    constexpr const char* dialogName = "Modify Connections";
    if (!std::exchange(this->opened, true)) {
        ImGui::OpenPopup(dialogName);
    }

    enum class Action { none, apply, cancel };
    Action action = Action::none;

    if (auto popup = IMW::ModalPopup(dialogName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            action = Action::cancel;
            ImGui::CloseCurrentPopup();
        }
        {
            const float dpi = LookAndFeel::dpiScale();
            IMW::Child  list("##connections", ImVec2(380.F * dpi, 160.F * dpi), true, 0);
            int         index = 0;
            for (auto& connection : this->connections) {
                ImGui::Checkbox(std::format("##connected{}", index++).c_str(), &connection.keep);
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(connection.controlName.c_str());
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                {
                    IMW::Font icon(LookAndFeel::instance().fontIconsSolid);
                    ImGui::TextUnformatted("\uf061"); // arrow pointing from the control to its selector
                }
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::Text("\"%s:%s\"", connection.allBlocks ? "*" : block->blockName.c_str(), this->property.c_str());
                if (!connection.keep && connection.allBlocks && graphModel) {
                    ImGui::TextDisabled("Disconnecting this connection will also disconnect %zu other controls", countOtherBlocksWithProperty(*graphModel, *block, this->property));
                }
            }
        }
        const float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.F;
        if (ImGui::Button("Done", ImVec2(halfWidth, 0.F))) {
            action = Action::apply;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(halfWidth, 0.F))) {
            action = Action::cancel;
            ImGui::CloseCurrentPopup();
        }
    }

    if (action == Action::apply && graphModel) {
        for (const auto& connection : this->connections) {
            if (!connection.keep) {
                graphModel->removeUiControlConnection(connection.controlUniqueName, connection.allBlocks ? std::string_view("*") : std::string_view(block->blockName), this->property);
            }
        }
    }
    if (action != Action::none || (this->opened && !ImGui::IsPopupOpen(dialogName))) {
        return false;
    }
    return true;
}

} // namespace DigitizerUi::components
