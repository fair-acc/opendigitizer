#include "Splitter.hpp"

namespace DigitizerUi::components {

float Splitter(SplitterState& splitter_state, ImVec2 space, bool vertical, float size, float defaultRatio, bool reset) {
    IMW::PushCursorPosition _;

    float startRatio = splitter_state.start_ratio;

    splitter_state.move(defaultRatio, !reset);
    if (splitter_state.is_hidden()) {
        return 0.0f;
    }

    float s = vertical ? space.x : space.y;
    auto  w = s * splitter_state.ratio;
    if (vertical) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + s - w - size / 2.f);
    } else {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s - w - size / 2.f);
    }

    {
        IMW::Child child("##c", ImVec2(0, 0), 0, 0);
        ImGui::Button("##sep", vertical ? ImVec2{size, space.y} : ImVec2{space.x, size});

        const auto cursor = vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(cursor);
        }

        if (ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(cursor);
            const auto delta     = ImGui::GetMouseDragDelta();
            splitter_state.ratio = startRatio - (vertical ? delta.x : delta.y) / s;
        } else {
            splitter_state.start_ratio = splitter_state.ratio;
        }
    }

    return splitter_state.ratio;
}

} // namespace DigitizerUi::components
