#include "SwipeGesture.hpp"

#include "../common/FramePacer.hpp"
#include "../common/LookAndFeel.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace gr::od {

namespace {
/// The frame in which suppressSwipeGesture() was last called is kept in the storage of the root window, so items in child
/// windows suppress the drag of the windows they are drawn in
[[nodiscard]] ImGuiStorage& rootWindowStorage() { return ImGui::GetCurrentWindow()->RootWindow->StateStorage; }

[[nodiscard]] ImGuiID swipeGestureSuppressedFrameId() { return ImHashStr("##swipeGestureSuppressedFrame"); }

[[nodiscard]] bool isSwipeGestureSuppressed() { return rootWindowStorage().GetInt(swipeGestureSuppressedFrameId(), -1) == ImGui::GetFrameCount(); }

[[nodiscard]] float animatePageVisibleFractionSinceLastFrame(PageSwipeState state) {
    const float target = state.isClosing ? 0.f : 1.f;
    if (state.pageVisibleFraction == target) {
        return target;
    }
    globalFramePacer().requestFrame();
    const float step = ImGui::GetIO().DeltaTime / LookAndFeel::instance().sheetAnimationDurationSeconds;
    return state.isClosing ? std::max(target, state.pageVisibleFraction - step) : std::min(target, state.pageVisibleFraction + step);
}
} // namespace

PageSwipeState updatePageSwipeState(PageSwipeState state, std::optional<float> dragPixels, bool isDragReleasedThisFrame, float lengthPixels) {
    if (dragPixels.has_value() && lengthPixels > 0.f) {
        state.pageVisibleFraction = std::clamp(state.pageVisibleFraction - *dragPixels / lengthPixels, 0.f, 1.f);
    }
    if (isDragReleasedThisFrame && 1.f - state.pageVisibleFraction > LookAndFeel::instance().sheetDismissThresholdFraction) {
        state.isClosing = true;
    }
    if (!dragPixels.has_value() || isDragReleasedThisFrame) {
        state.pageVisibleFraction = animatePageVisibleFractionSinceLastFrame(state);
    }
    return state;
}

std::optional<WindowDrag> claimWindowDrag(ImGuiID verticalDragId, ImGuiID horizontalDragId) {
    ImGui::KeepAliveID(verticalDragId);
    ImGui::KeepAliveID(horizontalDragId);
    const ImGuiIO& io       = ImGui::GetIO();
    const ImGuiID  activeId = ImGui::GetActiveID();

    if (activeId == verticalDragId || activeId == horizontalDragId) {
        const bool isReleased = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (isReleased) {
            ImGui::ClearActiveID();
        }
        return WindowDrag{
            .axis              = activeId == verticalDragId ? DragAxis::Vertical : DragAxis::Horizontal,
            .delta             = io.MouseDelta,
            .releasedThisFrame = isReleased,
        };
    }

    const ImVec2 pressPosition       = io.MouseClickedPos[ImGuiMouseButton_Left];
    const ImRect windowRect          = {ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize()};
    const bool   isPressOnThisWindow = activeId != 0 ? ImGui::GetCurrentContext()->ActiveIdWindow == ImGui::GetCurrentWindow() : ImGui::IsWindowHovered();
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left) || !windowRect.Contains(pressPosition) || !isPressOnThisWindow || isSwipeGestureSuppressed()) {
        return std::nullopt;
    }

    const ImVec2 movementSincePress = io.MousePos - pressPosition;
    const bool   isVertical         = std::abs(movementSincePress.y) >= std::abs(movementSincePress.x);
    ImGui::SetActiveID(isVertical ? verticalDragId : horizontalDragId, ImGui::GetCurrentWindow());
    return WindowDrag{
        .axis              = isVertical ? DragAxis::Vertical : DragAxis::Horizontal,
        .delta             = movementSincePress,
        .releasedThisFrame = false,
    };
}

void suppressSwipeGesture() { rootWindowStorage().SetInt(swipeGestureSuppressedFrameId(), ImGui::GetFrameCount()); }

} // namespace gr::od
