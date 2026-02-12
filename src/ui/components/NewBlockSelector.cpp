#include "NewBlockSelector.hpp"

#include "../App.hpp"

#include <gnuradio-4.0/Scheduler.hpp>

#include "../components/Dialog.hpp"
#include "../components/ListBox.hpp"

#include <misc/cpp/imgui_stdlib.h>

using namespace std::string_literals;

namespace DigitizerUi {
void NewBlockSelector::draw() {
    auto windowSize = ImGui::GetIO().DisplaySize - ImVec2(32, 32);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);
    auto x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + 32);

    if (auto menu = IMW::ModalPopup(m_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        auto listSize = windowSize - ImVec2(64, 64);
        listSize.x /= 2;
        auto ret = components::FilteredListBox("blocks", listSize, data, [index = 0](auto& knownBlockTypesItem) mutable -> std::pair<int, std::string> {
            index++;
            return std::pair{index, knownBlockTypesItem.first};
        });

        // If the type is empty, we treat it as invalid,
        // but some types might not have parametrizations,
        // that is their empty parametrization is valid,
        // so we need std::optional
        std::string                selectedType;
        std::optional<std::string> selectedParametrization;

        if (ret) {
            selectedType = ret.value().second;
        }

        if (!selectedType.empty()) {
            if (auto typeIt = data.find(selectedType); typeIt != data.cend()) {
                ImGui::SameLine();

                if (selectedType != m_previouslySelectedType) {
                    m_previouslySelectedType              = selectedType;
                    m_selectedTypeParametrizationListName = "parametrizations_for_" + selectedType;
                }

                ret = components::FilteredListBox(m_selectedTypeParametrizationListName.c_str(), listSize, //
                    typeIt->second, [index = 0](auto& parametrization) mutable -> std::pair<int, std::string> {
                        index++;
                        return std::pair{index, parametrization};
                    });

                if (ret) {
                    selectedParametrization = ret.value().second;
                }
            }
        }

        if (components::DialogButtons() == components::DialogButton::Ok) {
            if (!selectedType.empty() && selectedParametrization) {
                gr::Message message;
                std::string type    = std::move(selectedType) + selectedParametrization.value_or(std::string());
                message.cmd         = gr::message::Command::Set;
                message.endpoint    = gr::scheduler::property::kEmplaceBlock;
                message.serviceName = m_targetSchedulerUniqueName;
                message.data        = gr::property_map{{"type", std::move(type)}};
                if (!m_targetGraphUniqueName.empty()) {
                    (*message.data)["_targetGraph"] = m_targetGraphUniqueName;
                }
                m_graphModel->sendMessage(message);
            }
        }
    }
}
} // namespace DigitizerUi
