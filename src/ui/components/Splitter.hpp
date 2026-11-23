#ifndef OPENDIGITIZER_UI_COMPONENTS_SPLITTER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SPLITTER_HPP_

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

namespace gr::od::components {

struct SplitterState {
    enum class State { Hidden, AnimatedForward, AnimatedBackward, Shown } animState = State::Hidden;

    float startRatio = 0.0f;
    float ratio      = 0.0f;
    float speed      = LookAndFeel::instance().splitterRatioDeltaPerFrame;

    void move(float max, bool forward = true) noexcept {
        if (forward) {
            moveForward(max);
        } else {
            move_backward();
        }
    }

    void moveForward(float max) noexcept {
        if (animState == State::Shown) {
            return;
        }

        animState = State::AnimatedForward;
        if (ratio / max >= 0.7f) {
            speed = LookAndFeel::instance().splitterRatioDeltaPerFrameSlow;
        }

        ratio += speed;
        if (ratio >= max) {
            ratio     = max;
            animState = State::Shown;
            speed     = LookAndFeel::instance().splitterRatioDeltaPerFrame;
        }
    }
    void move_backward() noexcept {
        if (animState == State::Hidden) {
            return;
        }

        animState = State::AnimatedBackward;
        ratio -= speed;
        if (ratio <= 0.0f) {
            reset();
        }
    }

    void reset() noexcept {
        animState  = State::Hidden;
        startRatio = 0.0f;
        ratio      = 0.0f;
    }
    [[nodiscard]] bool isHidden() const noexcept { return animState == State::Hidden; }
};

float splitter(SplitterState& state, ImVec2 space, bool vertical, float size, float defaultRatio, bool reset);

} // namespace gr::od::components

#endif
