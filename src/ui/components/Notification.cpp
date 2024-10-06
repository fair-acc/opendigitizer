#include "Notification.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include "notification/ImGuiNotify.hpp"

void DigitizerUi::components::Notification::success(Notification&& notification) { ImGui::InsertNotification({ImGuiToastType::Success, static_cast<int>(notification.dismissTime.count()), notification.text.c_str()}); }

void DigitizerUi::components::Notification::success(const std::string& text, std::chrono::milliseconds dismissTime) { ImGui::InsertNotification({ImGuiToastType::Success, static_cast<int>(dismissTime.count()), text.c_str()}); }

void DigitizerUi::components::Notification::warning(Notification&& notification) { ImGui::InsertNotification({ImGuiToastType::Warning, static_cast<int>(notification.dismissTime.count()), notification.text.c_str()}); }

void DigitizerUi::components::Notification::warning(const std::string& text, std::chrono::milliseconds dismissTime) { ImGui::InsertNotification({ImGuiToastType::Warning, static_cast<int>(dismissTime.count()), text.c_str()}); }

void DigitizerUi::components::Notification::error(Notification&& notification) { ImGui::InsertNotification({ImGuiToastType::Error, static_cast<int>(notification.dismissTime.count()), notification.text.c_str()}); }

void DigitizerUi::components::Notification::error(const std::string& text, std::chrono::milliseconds dismissTime) { ImGui::InsertNotification({ImGuiToastType::Error, static_cast<int>(dismissTime.count()), text.c_str()}); }

void DigitizerUi::components::Notification::info(Notification&& notification) { ImGui::InsertNotification({ImGuiToastType::Info, static_cast<int>(notification.dismissTime.count()), notification.text.c_str()}); }

void DigitizerUi::components::Notification::info(const std::string& text, std::chrono::milliseconds dismissTime) { ImGui::InsertNotification({ImGuiToastType::Info, static_cast<int>(dismissTime.count()), text.c_str()}); }

void DigitizerUi::components::Notification::render() {
    // Notifications style setup
    IMW::StyleFloatVar _(ImGuiStyleVar_WindowRounding, 0.5f);  // round borders
    IMW::StyleFloatVar _(ImGuiStyleVar_WindowBorderSize, 1.f); // visible borders

    // Notifications color setup
    if (LookAndFeel::instance().style == LookAndFeel::Style::Dark) {
        IMW::StyleColor _(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f)); // Background color
    } else {
        IMW::StyleColor _(ImGuiCol_WindowBg, ImVec4(1.00f, 1.00f, 1.00f, 1.00f)); // White background
    }

    // Main rendering function
    ImGui::RenderNotifications();
}
