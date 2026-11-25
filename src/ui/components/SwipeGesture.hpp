#ifndef OPENDIGITIZER_UI_COMPONENTS_SWIPE_GESTURE_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SWIPE_GESTURE_HPP_

#include <imgui.h>

#include <optional>

namespace gr::od {

struct PageSwipeState {
    float pageVisibleFraction; // 1 is open, 0 is closed
    bool  isClosing;           // which direction the page is animating
};

/// Animates the opening/closing of a page, updating its position, unless there is @param dragPixels, in which case it moves the popup with that drag
/// Note that @param dragPixels being positive meaning going towards closed. If it passes a certain threshold of closed-ness and the drag is released,
/// then the popup will automatically finish closing
[[nodiscard]] PageSwipeState updatePageSwipeState(PageSwipeState state, std::optional<float> dragPixels, bool isDragReleasedThisFrame, float lengthPixels);

[[nodiscard]] constexpr bool isPageSwipeAnimating(PageSwipeState state, bool isDragHeld) { //
    return !isDragHeld && state.pageVisibleFraction != (state.isClosing ? 0.f : 1.f);
}

[[nodiscard]] constexpr bool isPageSwipeClosed(PageSwipeState state) { return state.isClosing && state.pageVisibleFraction <= 0.f; }

enum class DragAxis { Horizontal, Vertical };

struct WindowDrag {
    // which axis the initial movement of the drag was primarily on, x or y
    DragAxis axis;
    // note that even on a Horizontal drag, the y component may contain some delta, potentially even more than the x component.
    // this is also true for a Vertical drag and the x component. These can be ignored for a 1D drag or use them for 2D drag
    ImVec2 delta             = {0.f, 0.f};
    bool   releasedThisFrame = false;
};

/// Detects clicking and dragging (on touchscreen this would be swiping) within the current window. A drag that starts on
/// an item takes the item's active ID, unless suppressSwipeGesture() was called in this frame from a window with the
/// same root window.
/// @returns if a drag has occurred, the amount of movement and the axis on which it occurred
std::optional<WindowDrag> claimWindowDrag(ImGuiID verticalDragId, ImGuiID horizontalDragId);

/// Keeps claimWindowDrag() from taking the mouse drag event for this frame, in every window with the same root window as the
/// current window. It should be called after an item which uses drag input, like so:
/// 
/// ImGui::SliderScalar(...);
/// if (ImGui::IsItemActive()) {
///		suppressSwipeGesture();
/// }
void suppressSwipeGesture();

} // namespace gr::od

#endif
