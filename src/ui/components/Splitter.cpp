#include "Splitter.hpp"

#include "StyledWidgets.hpp"

namespace gr::od::components {

float splitter(SplitterState& state, ImVec2 space, bool vertical, float size, float defaultRatio, bool reset) {
    imw::PushCursorPosition _;

    float startRatio = state.startRatio;

    state.move(defaultRatio, !reset);
    if (state.isHidden()) {
        return 0.0f;
    }

    float s = vertical ? space.x : space.y;
    auto  w = s * state.ratio;
    if (vertical) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + s - w - size / 2.f);
    } else {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s - w - size / 2.f);
    }

    {
        imw::Child child("##c", ImVec2(0, 0), 0, 0);
        button("##sep", vertical ? ImVec2{size, space.y} : ImVec2{space.x, size});

        const auto cursor = vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(cursor);
        }

        if (ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(cursor);
            const auto delta = ImGui::GetMouseDragDelta();
            state.ratio      = startRatio - (vertical ? delta.x : delta.y) / s;
        } else {
            state.startRatio = state.ratio;
        }
    }

    return state.ratio;
}

} // namespace gr::od::components
