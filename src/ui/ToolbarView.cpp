#include "ToolbarView.hpp"

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
constexpr inline float kHeight      = 36.f;
constexpr inline float kLeftPadding = 16.f;
} // namespace

void ToolbarView::draw(Dashboard& dashboard) {
    const auto& blocks = _blocks.of(dashboard);
    if (blocks.empty()) {
        return;
    }

    IMW::Child toolbar("##Toolbar", ImVec2(ImGui::GetContentRegionAvail().x, kHeight), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kLeftPadding);
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
