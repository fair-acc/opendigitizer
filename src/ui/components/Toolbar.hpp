#ifndef DIGITIZER_TOOLBAR_H
#define DIGITIZER_TOOLBAR_H

#include "../common/ImguiWrap.hpp"

#include "../blocks/ToolbarBlock.hpp"

namespace DigitizerUi::components {
namespace detail {
struct ToolbarRAII {
    bool valid = false;
    inline ToolbarRAII(const char *id) {
        const ImVec2   size   = ImGui::GetContentRegionAvail();
        const auto     width  = size.x;
        constexpr auto height = 36;
        valid                 = ImGui::BeginChild(id, ImVec2(width, height));
        auto currentX         = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(currentX + 16);
    }

    inline ~ToolbarRAII() {
        if (valid) {
            const float    width     = ImGui::GetWindowWidth();
            const float    y         = (ImGui::GetWindowPos().y + ImGui::GetWindowHeight()) - 1;
            const float    x         = ImGui::GetWindowPos().x;
            const uint32_t lineColor = LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0x40000000 : 0x40ffffff;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y), ImVec2(width, y), lineColor);

            ImGui::EndChild();
        }
    }
};

} // namespace detail

inline void Toolbar(std::vector<gr::BlockModel *> blocks) {
    if (blocks.empty()) {
        return;
    }

    detail::ToolbarRAII toolbar("##Toolbar");
    if (toolbar.valid) {
        for (const auto &b : blocks) {
            // TODO: if (b->isToolbarBlock()) {
            b->draw();
            ImGui::SameLine();
        }
    }
}

} // namespace DigitizerUi::components

#endif
