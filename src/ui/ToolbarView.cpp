#include "ToolbarView.hpp"

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
constexpr inline float kHeight      = 36.f;
constexpr inline float kLeftPadding = 16.f;

// play, pause and stop for the dashboard's scheduler, enabled by its state; a manual pause or stop holds until play
void drawSchedulerControls(Dashboard& dashboard) {
    using enum gr::lifecycle::State;
    if (!dashboard.scheduler) {
        return;
    }
    const gr::lifecycle::State state  = dashboard.scheduler->state();
    const auto                 button = [](const char* label, const char* tooltip, bool enabled) {
        IMW::Disabled disabled(!enabled);
        bool          pressed = false;
        {
            IMW::Font font(LookAndFeel::instance().fontIconsSolid);
            pressed = ImGui::Button(label);
        }
        ImGui::SetItemTooltip("%s", tooltip);
        ImGui::SameLine();
        return pressed;
    };
    if (button("\uf04b###schedulerPlay", "start or resume the flowgraph", state == STOPPED || state == IDLE || state == PAUSED)) {
        std::ignore = dashboard.scheduler->start();
    }
    if (button("\uf04c###schedulerPause", "pause the flowgraph", state == RUNNING)) {
        std::ignore = dashboard.scheduler->pause();
    }
    if (button("\uf04d###schedulerStop", "stop the flowgraph", state == RUNNING || state == PAUSED)) {
        std::ignore = dashboard.scheduler->stop();
    }
}
} // namespace

void ToolbarView::draw(Dashboard& dashboard) {
    const auto& blocks = _blocks.of(dashboard);
    if (blocks.empty() && !dashboard.schedulerUi) {
        return;
    }

    IMW::Child toolbar("##Toolbar", ImVec2(ImGui::GetContentRegionAvail().x, kHeight), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kLeftPadding);
    if (dashboard.schedulerUi) {
        drawSchedulerControls(dashboard);
    }
    for (const auto& block : blocks) {
        std::ignore = block->draw();
        ImGui::SameLine();
    }

    const ImVec2   pos       = ImGui::GetWindowPos();
    const ImVec2   size      = ImGui::GetWindowSize();
    const uint32_t lineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().toolbarLineColor);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.f), ImVec2(pos.x + size.x, pos.y + size.y - 1.f), lineColor);
}

} // namespace DigitizerUi
