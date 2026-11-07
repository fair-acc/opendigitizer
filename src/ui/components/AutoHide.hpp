#ifndef OPENDIGITIZER_UI_COMPONENTS_AUTOHIDE_HPP
#define OPENDIGITIZER_UI_COMPONENTS_AUTOHIDE_HPP

#include "../common/FramePacer.hpp"
#include "../common/LookAndFeel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace DigitizerUi::components {

/// controls that slide out of sight once the pointer has stayed away from them for LookAndFeel::autoHideDelaySeconds and
/// slide back when it comes within autoHideRevealDistance of where they belong (a tap there on touch screens)
struct AutoHide {
    float awaySeconds = 0.f;
    float hidden      = 0.f; // 0: in place, 1: out of sight

    float advance(bool engaged, float dt) noexcept {
        const auto& look   = LookAndFeel::instance();
        awaySeconds        = engaged ? 0.f : awaySeconds + dt;
        const float target = awaySeconds >= look.autoHideDelaySeconds ? 1.f : 0.f;
        const float step   = dt / std::max(look.autoHideSlideSeconds, 1e-3f);
        hidden             = target > hidden ? std::min(target, hidden + step) : std::max(target, hidden - step);
        return hidden;
    }

    /// `home`: the screen rectangle of the controls in place (empty: not drawn, unchanged)
    float update(const ImRect& home) {
        if (home.GetWidth() <= 0.f || home.GetHeight() <= 0.f) {
            return hidden;
        }
        ImRect zone = home;
        zone.Expand(LookAndFeel::instance().autoHideRevealDistance);
        const float result           = advance(zone.Contains(ImGui::GetIO().MousePos), ImGui::GetIO().DeltaTime);
        const bool  countdownOrSlide = hidden < 1.f && (hidden > 0.f || awaySeconds > 0.f);
        if (countdownOrSlide) {
            globalFramePacer().requestFrame();
        }
        return result;
    }
};

} // namespace DigitizerUi::components

#endif // OPENDIGITIZER_UI_COMPONENTS_AUTOHIDE_HPP
