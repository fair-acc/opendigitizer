#include "ToolbarView.hpp"

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
constexpr inline float kHeight      = 36.f;
constexpr inline float kLeftPadding = 16.f;

// play, pause and stop for the scheduler, enabled by its state; a manual pause or stop holds until play
SchedulerRequest drawSchedulerControls(Scheduler& scheduler) {
    using enum gr::lifecycle::State;
    if (!scheduler) {
        return SchedulerRequest::none;
    }
    const gr::lifecycle::State state  = scheduler->state();
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
    SchedulerRequest request = SchedulerRequest::none;
    if (button("\uf04b###schedulerPlay", "start or resume the flowgraph", state == STOPPED || state == IDLE || state == PAUSED)) {
        std::ignore = scheduler->start();
        request     = SchedulerRequest::play;
    }
    if (button("\uf04c###schedulerPause", "pause the flowgraph", state == RUNNING)) {
        std::ignore = scheduler->pause();
        request     = SchedulerRequest::pause;
    }
    if (button("\uf04d###schedulerStop", "stop the flowgraph", state == RUNNING || state == PAUSED)) {
        std::ignore = scheduler->stop();
        request     = SchedulerRequest::stop;
    }
    return request;
}
} // namespace

SchedulerRequest ToolbarView::draw(Scheduler& scheduler, const UiGraphModel& graphModel, bool schedulerControls) {
    const auto& blocks = _blocks.of(scheduler, graphModel);
    if (blocks.empty() && !schedulerControls) {
        return SchedulerRequest::none;
    }

    IMW::Child toolbar("##Toolbar", ImVec2(ImGui::GetContentRegionAvail().x, kHeight), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kLeftPadding);
    const SchedulerRequest request = schedulerControls ? drawSchedulerControls(scheduler) : SchedulerRequest::none;
    for (const auto& block : blocks) {
        std::ignore = block->draw();
        ImGui::SameLine();
    }

    const ImVec2   pos       = ImGui::GetWindowPos();
    const ImVec2   size      = ImGui::GetWindowSize();
    const uint32_t lineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().toolbarLineColor);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.f), ImVec2(pos.x + size.x, pos.y + size.y - 1.f), lineColor);
    return request;
}

} // namespace DigitizerUi
