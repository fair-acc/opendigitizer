#ifndef OPENDIGITIZER_UI_COMPONENTS_SPLITTER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SPLITTER_HPP_

#include "../common/ImguiWrap.hpp"

namespace DigitizerUi::components {

struct SplitterState {
    enum class State { Hidden, AnimatedForward, AnimatedBackward, Shown } anim_state = State::Hidden;

    float start_ratio = 0.0f;
    float ratio       = 0.0f;
    float speed       = 0.02f;

    void move(float max, bool forward = true) noexcept {
        if (forward) {
            move_forward(max);
        } else {
            move_backward();
        }
    }

    void move_forward(float max) noexcept {
        if (anim_state == State::Shown) {
            return;
        }

        anim_state = State::AnimatedForward;
        if (ratio / max >= 0.7f) {
            speed = 0.01f;
        }

        ratio += speed;
        if (ratio >= max) {
            ratio      = max;
            anim_state = State::Shown;
            speed      = 0.02f;
        }
    }
    void move_backward() noexcept {
        if (anim_state == State::Hidden) {
            return;
        }

        anim_state = State::AnimatedBackward;
        ratio -= speed;
        if (ratio <= 0.0f) {
            reset();
        }
    }

    void reset() noexcept {
        anim_state  = State::Hidden;
        start_ratio = 0.0f;
        ratio       = 0.0f;
    }
    [[nodiscard]] bool is_hidden() const noexcept { return anim_state == State::Hidden; }
};

float Splitter(SplitterState& state, ImVec2 space, bool vertical, float size, float defaultRatio, bool reset);

} // namespace DigitizerUi::components

#endif
