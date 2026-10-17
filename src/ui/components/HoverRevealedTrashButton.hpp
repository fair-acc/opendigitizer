#ifndef OPENDIGITIZER_UI_COMPONENTS_HOVER_REVEALED_TRASH_BUTTON_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_HOVER_REVEALED_TRASH_BUTTON_HPP_

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi::components {

/// A trashcan button, visible if @param revealed is true, usually true when hovered
[[nodiscard]] inline bool hoverRevealedTrashButton(bool revealed, ImVec2 size) {
    constexpr const char* kIconTrash = "\u{f2ed}";

    IMW::StyleFloatVar alpha(ImGuiStyleVar_Alpha, revealed ? ImGui::GetStyle().Alpha : 0.F);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, size.y / 2.F);
    const bool pressed = ImGui::Button(kIconTrash, size);
    ImGui::PopFont();
    return pressed;
}

} // namespace DigitizerUi::components

#endif
