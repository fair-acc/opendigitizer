#include "ToolbarView.hpp"

#include <string>

#include <gnuradio-4.0/Logger.hpp>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

namespace DigitizerUi {

namespace {
constexpr inline std::string_view kToolkit     = "Dear ImGui";
constexpr inline float            kHeight      = 36.f;
constexpr inline float            kLeftPadding = 16.f;

std::string toolkitOf(const gr::BlockModel& block) {
    const gr::property_map& metaInformation = block.metaInformation();
    const auto              drawable        = metaInformation.find_value(std::string("Drawable"), std::pmr::get_default_resource());
    if (!drawable) {
        return {};
    }
    const auto drawableInfo = drawable->get_if<gr::property_map>();
    if (!drawableInfo) {
        return {};
    }
    return drawableInfo->find_value(std::string("Toolkit"), std::pmr::get_default_resource()).value_or(gr::pmt::Value{}).value_or(std::string());
}
} // namespace

void ToolbarView::refreshBlocks(Dashboard& dashboard) {
    const void*         scheduler          = dashboard.scheduler ? static_cast<const void*>(dashboard.scheduler.operator->()) : nullptr;
    const std::uint64_t topologyGeneration = dashboard.graphModel.topologyGeneration;
    if (_dashboard == std::addressof(dashboard) && _scheduler == scheduler && _topologyGeneration == topologyGeneration) {
        return;
    }
    _dashboard          = std::addressof(dashboard);
    _scheduler          = scheduler;
    _topologyGeneration = topologyGeneration;
    _blocks.clear();
    if (!scheduler) {
        return;
    }

    for (const auto& block : dashboard.scheduler->graph().blocks()) {
        if (block->uiCategory() != gr::UICategory::Toolbar) {
            continue;
        }
        if (const std::string toolkit = toolkitOf(*block); toolkit != kToolkit) {
            gr::log::warning("toolbar block '{}' is drawn with toolkit '{}', not '{}': not shown", block->uniqueName(), toolkit, kToolkit);
            continue;
        }
        _blocks.push_back(block);
    }
}

void ToolbarView::draw(Dashboard& dashboard) {
    refreshBlocks(dashboard);
    if (_blocks.empty()) {
        return;
    }

    IMW::Child toolbar("##Toolbar", ImVec2(ImGui::GetContentRegionAvail().x, kHeight), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kLeftPadding);
    for (const auto& block : _blocks) {
        std::ignore = block->draw();
        ImGui::SameLine();
    }

    const ImVec2   pos       = ImGui::GetWindowPos();
    const ImVec2   size      = ImGui::GetWindowSize();
    const uint32_t lineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().toolbarLineColor);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + size.y - 1.f), ImVec2(pos.x + size.x, pos.y + size.y - 1.f), lineColor);
}

} // namespace DigitizerUi
