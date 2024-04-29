#include "Splitter.hpp"

namespace DigitizerUi::components {

struct SplitterState {
    enum class State {
        Hidden,
        AnimatedForward,
        AnimatedBackward,
        Shown
    } anim_state
            = State::Hidden;
    float start_ratio = 0.0f;
    float ratio       = 0.0f;
    float speed       = 0.02f;

    void  move(float max, bool forward = true) noexcept {
        if (forward)
            move_forward(max);
        else
            move_backward();
    }

    void move_forward(float max) noexcept {
        if (anim_state == State::Shown)
            return;

        anim_state = State::AnimatedForward;
        if (ratio / max >= 0.7f)
            speed = 0.01f;

        ratio += speed;
        if (ratio >= max) {
            ratio      = max;
            anim_state = State::Shown;
            speed      = 0.02f;
        }
    }
    void move_backward() noexcept {
        if (anim_state == State::Hidden)
            return;

        anim_state = State::AnimatedBackward;
        ratio -= speed;
        if (ratio <= 0.0f)
            reset();
    }

    void reset() noexcept {
        anim_state  = State::Hidden;
        start_ratio = 0.0f;
        ratio       = 0.0f;
    }
    [[nodiscard]] bool is_hidden() const noexcept {
        return anim_state == State::Hidden;
    }
} splitter_state;

float Splitter(ImVec2 space, bool vertical, float size, float defaultRatio, bool reset) {
    float startRatio = splitter_state.start_ratio;

    splitter_state.move(defaultRatio, !reset);
    if (splitter_state.is_hidden())
        return 0.0f;

    float s = vertical ? space.x : space.y;
    auto  w = s * splitter_state.ratio;
    if (vertical) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + s - w - size / 2.f);
    } else {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s - w - size / 2.f);
    }

    {
        IMW::Child child("##c", ImVec2(0, 0), 0, 0);
        ImGui::Button("##sep", vertical ? ImVec2{ size, space.y } : ImVec2{ space.x, size });

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
