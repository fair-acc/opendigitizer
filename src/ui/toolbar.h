#ifndef DIGITIZER_TOOLBAR_H
#define DIGITIZER_TOOLBAR_H

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#include <imgui.h>

#include "dashboard.h"
#include "flowgraph.h"

#include "app.h"
#include "toolbar_block.h"

namespace DigitizerUi {
namespace detail {
inline bool beginToolbar(const char *id) {
    const ImVec2   size     = ImGui::GetContentRegionAvail();
    const auto     width    = size.x;
    constexpr auto height   = 36;
    auto           ret      = ImGui::BeginChild(id, ImVec2(width, height));
    auto           currentX = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(currentX + 16);

    return ret;
}

inline void endToolbar() {
    const float    width     = ImGui::GetWindowWidth();
    const float    y         = (ImGui::GetWindowPos().y + ImGui::GetWindowHeight()) - 1;
    const float    x         = ImGui::GetWindowPos().x;
    const uint32_t lineColor = DigitizerUi::App::instance().style() == DigitizerUi::Style::Light ? 0x40000000 : 0x40ffffff;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y), ImVec2(width, y), lineColor);

    ImGui::EndChild();
}

} // namespace detail

inline void drawToolbar() {
    const auto &blocks         = App::instance().dashboard->localFlowGraph.blocks();
    bool        hasToolBarItem = false;
    for (const auto &b : blocks) {
        hasToolBarItem |= b->isToolbarBlock();
    }

    if (hasToolBarItem) {
        detail::beginToolbar("##Toolbar");

        for (const auto &b : blocks) {
            if (b->isToolbarBlock()) {
                b->draw();
                ImGui::SameLine();
            }
        }

        detail::endToolbar();
    }
}

} // namespace DigitizerUi

#endif
