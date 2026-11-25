#include "SheetPopup.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include "StyledWidgets.hpp"
#include "SwipeGesture.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace gr::od {

namespace {
struct SheetStorageIds {
    ImGuiID pageVisibleFraction;
    ImGuiID isClosing;
    ImGuiID headerButton;
};

[[nodiscard]] SheetStorageIds sheetStorageIds(ImGuiID popupId) {
    imw::OverrideId popupScope(popupId);
    return SheetStorageIds{
        .pageVisibleFraction = ImGui::GetID("pageVisibleFraction"),
        .isClosing           = ImGui::GetID("isClosing"),
        .headerButton        = ImGui::GetID("##sheetHeader"),
    };
}

constexpr PageSwipeState kClosedSheetSwipeState{.pageVisibleFraction = 0.f, .isClosing = false};

// outside of the [0, 1] range of PageSwipeState::pageVisibleFraction
constexpr float kAbsentPageVisibleFraction = -1.f;

[[nodiscard]] PageSwipeState readSheetSwipeStorage(const ImGuiStorage& storage, const SheetStorageIds& storageIds) {
    return PageSwipeState{
        .pageVisibleFraction = storage.GetFloat(storageIds.pageVisibleFraction, kClosedSheetSwipeState.pageVisibleFraction),
        .isClosing           = storage.GetBool(storageIds.isClosing, kClosedSheetSwipeState.isClosing),
    };
}

void writeSheetSwipeStorage(ImGuiStorage& storage, const SheetStorageIds& storageIds, PageSwipeState swipe) {
    storage.SetFloat(storageIds.pageVisibleFraction, swipe.pageVisibleFraction);
    storage.SetBool(storageIds.isClosing, swipe.isClosing);
}

[[nodiscard]] float lengthAlongEdgeNormal(ImVec2 size, SheetEdge edge) { return edge == SheetEdge::Bottom ? size.y : size.x; }

[[nodiscard]] float headerThickness(SheetEdge edge) {
    const ImVec2 padding = ImGui::GetStyle().WindowPadding;
    return ImGui::GetFrameHeight() + 2.f * (edge == SheetEdge::Bottom ? padding.y : padding.x);
}

void drawSheet(const ImRect& sheetRect, SheetEdge edge) {
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    const bool         isBottom    = edge == SheetEdge::Bottom;
    ImGui::GetWindowDrawList()->AddRectFilled(sheetRect.Min, sheetRect.Max, ImGui::GetColorU32(ImGuiCol_PopupBg), lookAndFeel.sheetHeaderRoundingEms * ImGui::GetFontSize(), isBottom ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersLeft);
}

void drawSheetHandlePill(const ImRect& headerRect, SheetEdge edge) {
    const LookAndFeel& lookAndFeel  = LookAndFeel::instance();
    const float        fontSize     = ImGui::GetFontSize();
    const bool         isBottom     = edge == SheetEdge::Bottom;
    const float        pillLength   = lookAndFeel.sheetHandlePillLengthEms * fontSize;
    const float        pillWidth    = lookAndFeel.sheetHandlePillThicknessEms * fontSize;
    const ImVec2       pillHalfSize = (isBottom ? ImVec2{pillLength, pillWidth} : ImVec2{pillWidth, pillLength}) * .5f;
    const ImVec2       center       = headerRect.GetCenter();
    ImGui::GetWindowDrawList()->AddRectFilled(center - pillHalfSize, center + pillHalfSize, ImGui::GetColorU32(ImGuiCol_Text, lookAndFeel.sheetHandlePillAlpha), .5f * pillWidth);
}

[[nodiscard]] bool headerIconButton(const char* label, ImVec2 position) {
    const float size = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(position);
    imw::StyleFloatVar rounding(ImGuiStyleVar_FrameRounding, .5f * size);
    imw::Font          iconFont(LookAndFeel::instance().fontIconsSolid);
    return button(label, ImVec2{size, size});
}

/// Return true if a click has occurred outside the bounds of the current sheet popup. If there are nested popups
/// (another one open above this one) then the click is ignored and assumed to just close that nested popup
[[nodiscard]] bool isClickOutsideOfCurrentSheet() {
    const bool isSheetHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isSheetHovered && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
}

/// Returns nullptr if the popup was just opened this frame and has not been created yet
[[nodiscard]] const ImGuiWindow* getImGuiWindowOfOpenPopup(ImGuiID popupId) {
    for (const ImGuiPopupData& popup : ImGui::GetCurrentContext()->OpenPopupStack) {
        if (popup.PopupId == popupId) {
            return popup.Window;
        }
    }
    return nullptr;
}

/// Get the ImGuiWindow* given the ImGuiWindow* of the root sheet popup window and the ID of the child window.
/// Returns nullptr if either the sheet window or the page window does not exist
[[nodiscard]] const ImGuiWindow* getImGuiChildWindowOfSheetPopupPage(const ImGuiWindow* sheetWindow, ImGuiID pageId) {
    if (sheetWindow == nullptr) {
        return nullptr;
    }
    // This is done in an attempt to use the public API as much as possible, though imgui_internal things might do this better
    for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if (window->RootWindow == sheetWindow && window->ChildId == pageId) {
            return window;
        }
    }
    return nullptr;
}

/// Get the ideal length of a popup along the opening axis, measured from the content drawn in it on the previous frame
[[nodiscard]] float sheetPopupChildWindowPageLength(ImGuiID popupId, ImGuiID sheetPageId, SheetEdge edge) {
    const float        minLength  = lengthAlongEdgeNormal(LookAndFeel::instance().sheetMinContentSizeEms * ImGui::GetFontSize(), edge);
    const ImGuiWindow* pageWindow = getImGuiChildWindowOfSheetPopupPage(getImGuiWindowOfOpenPopup(popupId), sheetPageId);
    if (pageWindow == nullptr) {
        return minLength;
    }
    return std::max(minLength, lengthAlongEdgeNormal(pageWindow->ContentSizeIdeal + ImGui::GetStyle().WindowPadding * 2.f, edge));
}

/// This must be closed with ImGui::EndPopup() in the same manner as ImGui::OpenPopup().
/// @param backButtonPressed if this is not nullptr, the sheet will draw a back button and set the bool to whether it was pressed
[[nodiscard]] bool beginSheetPopup(ImGuiID popupId, float contentLength, bool* backButtonPressed, PopupFlags flags) {
    // the sheet places and sizes itself. ImGui::BeginPopupEx() also discards this data when the popup is not open
    ImGui::GetCurrentContext()->NextWindowData.ClearFlags();
    const bool            isModal       = hasFlag(flags, PopupFlags::Modal);
    ImGuiStorage* const   callerStorage = ImGui::GetStateStorage();
    const SheetStorageIds storageIds    = sheetStorageIds(popupId);
    PageSwipeState        swipeState    = readSheetSwipeStorage(*callerStorage, storageIds);
    swipeState.isClosing                = detail::takePopupCloseRequest(popupId) || swipeState.isClosing;

    if (!ImGui::IsPopupOpen(popupId, ImGuiPopupFlags_None)) {
        // closed, or closed without the closing animation by ImGui::CloseCurrentPopup() or by another popup that replaced it
        writeSheetSwipeStorage(*callerStorage, storageIds, kClosedSheetSwipeState);
        return false;
    }

    const LookAndFeel&         lookAndFeel   = LookAndFeel::instance();
    const SheetEdge            edge          = lookAndFeel.sheetEdge();
    const bool                 isBottom      = edge == SheetEdge::Bottom;
    const ImGuiViewport* const viewport      = ImGui::GetMainViewport();
    const float                header        = headerThickness(edge);
    const float                displayLength = lengthAlongEdgeNormal(viewport->Size, edge);
    const float                maxLength     = lookAndFeel.sheetMaxLengthFractionOfDisplay * displayLength;
    const float                sheetLength   = hasFlag(flags, PopupFlags::WantsMaxSize) ? maxLength : std::min(header + std::max(contentLength, 0.f), maxLength);
    const float                visibleLength = swipeState.pageVisibleFraction * sheetLength;
    const ImVec2               sheetPosition = isBottom ? ImVec2{viewport->Pos.x, viewport->Pos.y + viewport->Size.y - visibleLength} : ImVec2{viewport->Pos.x + viewport->Size.x - visibleLength, viewport->Pos.y};
    const ImVec2               sheetSize     = isBottom ? ImVec2{viewport->Size.x, sheetLength} : ImVec2{sheetLength, viewport->Size.y};

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    if (swipeState.isClosing) {
        windowFlags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::SetNextWindowPos(sheetPosition);
    ImGui::SetNextWindowSize(sheetSize);
    {
        imw::StyleVar      noPadding(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
        imw::StyleFloatVar noBorder(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImVec4             dimColor = ImGui::GetStyleColorVec4(ImGuiCol_ModalWindowDimBg);
        dimColor.w *= hasFlag(flags, PopupFlags::WantsExclusiveFocus) ? swipeState.pageVisibleFraction : 0.f; // fading animation
        imw::StyleColor dimBehindSheet(ImGuiCol_ModalWindowDimBg, dimColor);
        // we use a modal popup since we can recreate the non-modal behavior from modal, but not the other way around
        if (!ImGui::BeginPopupEx(popupId, windowFlags | ImGuiWindowFlags_Modal)) {
            return false;
        }
    }
    if (ImGui::IsWindowAppearing()) {
        swipeState.isClosing = false; // just opened, or opened again with ImGui::OpenPopup() while it was closing
    } else if (!isModal && isClickOutsideOfCurrentSheet()) {
        swipeState.isClosing = true;
    }

    const ImVec2 scrolledSheetPosition = ImGui::GetCursorScreenPos();
    const ImRect sheetRect             = {scrolledSheetPosition, scrolledSheetPosition + sheetSize};
    const ImRect headerRect            = {scrolledSheetPosition, scrolledSheetPosition + (isBottom ? ImVec2{sheetSize.x, header} : ImVec2{header, sheetSize.y})};
    drawSheet(sheetRect, edge);
    if (!isModal) {
        drawSheetHandlePill(headerRect, edge);
    }

    const ImVec2 padding    = ImGui::GetStyle().WindowPadding;
    const float  buttonSize = ImGui::GetFrameHeight();
    if (backButtonPressed != nullptr) {
        *backButtonPressed = headerIconButton(detail::kSheetBackButtonLabel, headerRect.Min + padding);
    }
    const ImVec2 closeButtonPosition = isBottom ? ImVec2{headerRect.Max.x - padding.x - buttonSize, headerRect.Min.y + padding.y} : ImVec2{headerRect.Min.x + padding.x, headerRect.Max.y - padding.y - buttonSize};
    if (headerIconButton(detail::kSheetCloseButtonLabel, closeButtonPosition)) {
        swipeState.isClosing = true;
    }

    if (isModal) {
        swipeState = updatePageSwipeState(swipeState, std::nullopt, false, sheetLength);
    } else {
        ImGui::SetCursorScreenPos(headerRect.Min);
        {
            imw::OverrideId popupScope(popupId);
            ImGui::InvisibleButton("##sheetHeader", headerRect.GetSize());
        }
        const float headerDrag = lengthAlongEdgeNormal(ImGui::GetIO().MouseDelta, edge);
        swipeState             = updatePageSwipeState(swipeState, ImGui::IsItemActive() ? std::optional{headerDrag} : std::nullopt, ImGui::IsItemDeactivated(), sheetLength);
    }

    if (isPageSwipeClosed(swipeState)) {
        ImGui::CloseCurrentPopup();
        swipeState = kClosedSheetSwipeState;
    }
    writeSheetSwipeStorage(*callerStorage, storageIds, swipeState);

    ImGui::SetCursorScreenPos(isBottom ? ImVec2{sheetRect.Min.x, headerRect.Max.y} : ImVec2{headerRect.Max.x, sheetRect.Min.y});
    return true;
}

struct CurrentSheet {
    ImGuiID         popupId;
    ImGuiWindow*    window;
    ImGuiStorage*   callerStorage;
    SheetStorageIds storageIds;
};

/// Returns the innermost popup whose contents are being drawn if it exists and is a sheet
[[nodiscard]] std::optional<CurrentSheet> currentSheet() {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    if (!LookAndFeel::instance().usesSheetPopups() || context.BeginPopupStack.empty()) {
        return std::nullopt;
    }
    const ImGuiPopupData& popup = context.BeginPopupStack.back();
    if (popup.Window == nullptr || popup.Window->ParentWindow == nullptr) {
        return std::nullopt;
    }
    ImGuiStorage* const   callerStorage = popup.Window->ParentWindow->DC.StateStorage;
    const SheetStorageIds storageIds    = sheetStorageIds(popup.PopupId);
    if (callerStorage->GetFloat(storageIds.pageVisibleFraction, kAbsentPageVisibleFraction) == kAbsentPageVisibleFraction) {
        return std::nullopt;
    }
    return CurrentSheet{.popupId = popup.PopupId, .window = popup.Window, .callerStorage = callerStorage, .storageIds = storageIds};
}

[[nodiscard]] ImGuiID createSheetStorageId(ImGuiID popupId, const char* name) {
    imw::OverrideId popupScope(popupId);
    return ImGui::GetID(name);
}

/// The storage ID for the ID of sheet menu (a page opened by beginPopupSheetMenu()) at the given depth
[[nodiscard]] ImGuiID pageMenuIdStorageId(ImGuiID popupId, std::size_t depth) {
    imw::OverrideId popupScope(popupId);
    imw::ChangeId   depthScope(static_cast<int>(depth));
    return ImGui::GetID("pageMenuId");
}

/// Returns a list of IDs from the root page into the most nested open page, like a file path
[[nodiscard]] std::vector<ImGuiID> getPathToCurrentlyOpenPage(const ImGuiStorage& storage, ImGuiID popupId) {
    std::vector<ImGuiID> path;
    for (std::size_t depth = 1UZ;; ++depth) {
        const auto menuId = static_cast<ImGuiID>(storage.GetInt(pageMenuIdStorageId(popupId, depth), 0));
        if (menuId == 0) {
            return path;
        }
        path.push_back(menuId);
    }
}

/// Closes all pages more deeply nested than the given depth. Inclusive, so this will keep the first page if you pass zero for depth
void truncatePagePath(ImGuiStorage& storage, ImGuiID popupId, std::size_t depth) {
    for (std::size_t closedDepth = depth + 1UZ; storage.GetInt(pageMenuIdStorageId(popupId, closedDepth), 0) != 0; ++closedDepth) {
        storage.SetInt(pageMenuIdStorageId(popupId, closedDepth), 0);
    }
}

/// Get the ID of the nested menu child window inside a popup given the IDs of all the nested menus up to that one
[[nodiscard]] ImGuiID pageChildId(ImGuiID popupId, std::span<const ImGuiID> menuIds) {
    imw::OverrideId popupScope(popupId);
    for (const ImGuiID menuId : menuIds) {
        ImGui::PushID(static_cast<int>(menuId));
    }
    const ImGuiID id = ImGui::GetID("##page");
    for (std::size_t i = 0UZ; i < menuIds.size(); ++i) {
        ImGui::PopID();
    }
    return id;
}

/// Given a page's ChildWindow and the root popup ID, and the IDs of the currently open pages, try to get its depth in
/// the nested menu hierarchy, assuming it is the currently open page or an ancestor of it
[[nodiscard]] std::optional<std::size_t> pageDepth(const ImGuiWindow& window, ImGuiID popupId, std::span<const ImGuiID> path) {
    for (std::size_t depth = 0UZ; depth <= path.size(); ++depth) {
        if (window.ChildId == pageChildId(popupId, path.first(depth))) {
            return depth;
        }
    }
    return std::nullopt;
}

constexpr PageSwipeState kOpenPageSwipeState{.pageVisibleFraction = 1.f, .isClosing = false};

/// The top page of a sheet slides to the right to show its parent page. Its state is kept in the storage of the window
/// that draws the sheet
[[nodiscard]] PageSwipeState readTopPageSwipeState(const ImGuiStorage& storage, ImGuiID popupId) {
    return PageSwipeState{
        .pageVisibleFraction = storage.GetFloat(createSheetStorageId(popupId, "topPageVisibleFraction"), kOpenPageSwipeState.pageVisibleFraction),
        .isClosing           = storage.GetBool(createSheetStorageId(popupId, "isTopPageClosing"), kOpenPageSwipeState.isClosing),
    };
}

void writeTopPageSwipeState(ImGuiStorage& storage, ImGuiID popupId, PageSwipeState swipeState) {
    storage.SetFloat(createSheetStorageId(popupId, "topPageVisibleFraction"), swipeState.pageVisibleFraction);
    storage.SetBool(createSheetStorageId(popupId, "isTopPageClosing"), swipeState.isClosing);
}

/// Moves the contents of the current window `offset` to the right, as if it were scrolled horizontally by -offset
/// the windows itself and its clip rect stay in the same place, this should just be a visual effect
void offsetWindowContentsHorizontally(float offset) {
    ImGuiWindow& window = *ImGui::GetCurrentWindow();
    // (ImGui::SetScrollX() doesn't allow this to happen so just directly modify internal state)
    for (ImRect* rect : {&window.WorkRect, &window.ParentWorkRect, &window.ContentRegionRect}) {
        rect->Min.x += offset;
        rect->Max.x += offset;
    }
    for (ImVec2* position : {&window.DC.CursorStartPos, &window.DC.CursorPos, &window.DC.CursorPosPrevLine, &window.DC.CursorMaxPos, &window.DC.IdealMaxPos}) {
        position->x += offset;
    }
    window.DC.Indent.x += offset;
}

/// This struct is used to save and restore imgui state relevant for laying out and measuring the contents of a child
/// window. Saving and restoring this state can enable child windows to draw and layout independently of their parents
/// which is useful for implementing stacking and swiping of pages
struct ParentPageLayout {
    ImVec2            cursorPos;
    ImVec2            cursorPosPrevLine;
    ImVec2            cursorMaxPos;
    ImVec2            idealMaxPos;
    ImVec2            currLineSize;
    ImVec2            prevLineSize;
    float             currLineTextBaseOffset;
    float             prevLineTextBaseOffset;
    bool              isSameLine;
    bool              isSetPos;
    ImGuiLastItemData lastItemData;
};

[[nodiscard]] ParentPageLayout readCurrentWindowLayout() {
    const ImGuiWindowTempData& layout = ImGui::GetCurrentWindow()->DC;
    return ParentPageLayout{
        .cursorPos              = layout.CursorPos,
        .cursorPosPrevLine      = layout.CursorPosPrevLine,
        .cursorMaxPos           = layout.CursorMaxPos,
        .idealMaxPos            = layout.IdealMaxPos,
        .currLineSize           = layout.CurrLineSize,
        .prevLineSize           = layout.PrevLineSize,
        .currLineTextBaseOffset = layout.CurrLineTextBaseOffset,
        .prevLineTextBaseOffset = layout.PrevLineTextBaseOffset,
        .isSameLine             = layout.IsSameLine,
        .isSetPos               = layout.IsSetPos,
        .lastItemData           = ImGui::GetCurrentContext()->LastItemData,
    };
}

void restoreCurrentWindowLayout(const ParentPageLayout& saved) {
    ImGuiWindowTempData& layout              = ImGui::GetCurrentWindow()->DC;
    layout.CursorPos                         = saved.cursorPos;
    layout.CursorPosPrevLine                 = saved.cursorPosPrevLine;
    layout.CursorMaxPos                      = saved.cursorMaxPos;
    layout.IdealMaxPos                       = saved.idealMaxPos;
    layout.CurrLineSize                      = saved.currLineSize;
    layout.PrevLineSize                      = saved.prevLineSize;
    layout.CurrLineTextBaseOffset            = saved.currLineTextBaseOffset;
    layout.PrevLineTextBaseOffset            = saved.prevLineTextBaseOffset;
    layout.IsSameLine                        = saved.isSameLine;
    layout.IsSetPos                          = saved.isSetPos;
    ImGui::GetCurrentContext()->LastItemData = saved.lastItemData;
}

/// Get the singleton stack of saved/restored imgui state for the nested popups
[[nodiscard]] std::vector<ParentPageLayout>& parentPageLayouts() {
    static std::vector<ParentPageLayout> layouts;
    return layouts;
}

/// Storage ID for the bool whether the current window's page is hidden
[[nodiscard]] ImGuiID isPageHiddenStorageId() { return ImHashStr("##isPageHidden"); }

/// Cause everything inside of the current window to be clipped
void pushHiddenPageClipRect() {
    const ImVec2 windowPosition = ImGui::GetWindowPos();
    // we could hide the window but that would hide its children as well
    ImGui::PushClipRect(windowPosition, windowPosition, false);
}

/// Begin the ChildWindow of the page specified by @param depth. Note that only the deepest page in the hierarchy will
/// end up having its contents shown (and maybe the parent of it, if a sliding animation is happening)
void beginPage(const CurrentSheet& sheet, std::size_t depth, ImGuiID pageId, ImVec2 position, ImVec2 size) {
    const std::size_t    topDepth = getPathToCurrentlyOpenPage(*sheet.callerStorage, sheet.popupId).size();
    const PageSwipeState topSwipe = readTopPageSwipeState(*sheet.callerStorage, sheet.popupId);
    ImGui::SetNextWindowPos(position);
    ImGui::BeginChild(pageId, size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::IsWindowAppearing()) {
        // resetting the scroll position on open seems expected for the user, and it makes tests a little more deterministic.
        // if we were to remove this, tests would need to reset the scroll of the pages themselves, between test cases
        ImGui::SetScrollY(0.f);
    }
    const float width    = ImGui::GetWindowWidth();
    const bool  isHidden = depth != topDepth && (depth + 1UZ != topDepth || topSwipe.pageVisibleFraction >= 1.f);
    ImGui::GetStateStorage()->SetBool(isPageHiddenStorageId(), isHidden);
    if (isHidden) {
        pushHiddenPageClipRect();
    } else if (depth == topDepth) {
        offsetWindowContentsHorizontally((1.f - topSwipe.pageVisibleFraction) * width);
    } else {
        offsetWindowContentsHorizontally(-topSwipe.pageVisibleFraction * width);
    }
}

/// Scrolls the top page with a vertical drag, and slides it with a horizontal drag. Does nothing for the other pages,
/// and for the top page after the first time in a frame, because closing the top page makes its parent page the top
/// page while the parent page is still being drawn
void endPage(const CurrentSheet& sheet, std::size_t depth) {
    if (ImGui::GetStateStorage()->GetBool(isPageHiddenStorageId(), false)) {
        ImGui::PopClipRect();
    }
    ImGuiStorage&     storage        = *sheet.callerStorage;
    const std::size_t topDepth       = getPathToCurrentlyOpenPage(storage, sheet.popupId).size();
    const ImGuiID     handledFrameId = createSheetStorageId(sheet.popupId, "topPageHandledFrame");
    if (depth != topDepth || storage.GetInt(handledFrameId, -1) == ImGui::GetFrameCount()) {
        return;
    }
    storage.SetInt(handledFrameId, ImGui::GetFrameCount());

    const std::optional<WindowDrag> drag = claimWindowDrag(createSheetStorageId(sheet.popupId, "##pageScroll"), createSheetStorageId(sheet.popupId, "##pageSwipe"));
    if (drag && drag->axis == DragAxis::Vertical) {
        ImGui::SetScrollY(ImGui::GetScrollY() - drag->delta.y);
    }
    if (depth == 0UZ) {
        return; // the root page does not slide
    }
    const bool     isSwipe  = drag && drag->axis == DragAxis::Horizontal;
    PageSwipeState topSwipe = updatePageSwipeState(readTopPageSwipeState(storage, sheet.popupId), isSwipe ? std::optional{drag->delta.x} : std::nullopt, isSwipe && drag->releasedThisFrame, ImGui::GetWindowWidth());
    if (isPageSwipeClosed(topSwipe)) {
        truncatePagePath(storage, sheet.popupId, depth - 1UZ);
        topSwipe = kOpenPageSwipeState;
    }
    writeTopPageSwipeState(storage, sheet.popupId, topSwipe);
}

/// Closes the open pages from the first one whose beginPopupSheetMenu() was not called in this frame, for example because the
/// contents of the sheet changed
void closePagesNotDrawnThisFrame(const CurrentSheet& sheet) {
    const std::vector<ImGuiID> path = getPathToCurrentlyOpenPage(*sheet.callerStorage, sheet.popupId);
    for (std::size_t depth = 1UZ; depth <= path.size(); ++depth) {
        const ImGuiWindow* page = getImGuiChildWindowOfSheetPopupPage(sheet.window, pageChildId(sheet.popupId, std::span(path).first(depth)));
        if (page == nullptr || page->LastFrameActive != ImGui::GetFrameCount()) {
            truncatePagePath(*sheet.callerStorage, sheet.popupId, depth - 1UZ);
            writeTopPageSwipeState(*sheet.callerStorage, sheet.popupId, kOpenPageSwipeState);
            return;
        }
    }
}

inline constexpr const char* kChevronRightIcon = "\u{f054}";

/// draw a row like "Submenu    >" which opens a subpage
/// @returns true if the row was clicked, false otherwise
[[nodiscard]] bool drawSheetMenuRow(const char* label, const char* icon, bool isEnabled) {
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    const float        rowHeight   = ImGui::GetFrameHeight() * lookAndFeel.sheetRowHeightFrameHeightRatio;
    const float        spacing     = ImGui::GetStyle().ItemInnerSpacing.x;
    imw::Disabled      disabled(!isEnabled);
    imw::StyleVar      verticallyCentredLabel(ImGuiStyleVar_SelectableTextAlign, ImVec2{0.f, .5f});

    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    if (icon != nullptr) {
        imw::Font    iconFont(lookAndFeel.fontIconsSolid);
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::GetWindowDrawList()->AddText(rowStart + ImVec2{0.f, .5f * (rowHeight - iconSize.y)}, ImGui::GetColorU32(ImGuiCol_Text), icon);
        ImGui::SetCursorScreenPos(rowStart + ImVec2{iconSize.x + spacing, 0.f});
    }
    const bool   isClicked = selectable(label, false, ImGuiSelectableFlags_NoAutoClosePopups, ImVec2{0.f, rowHeight});
    const ImVec2 rowMin    = ImGui::GetItemRectMin();
    const ImVec2 rowMax    = ImGui::GetItemRectMax();

    imw::Font    iconFont(lookAndFeel.fontIconsSolid);
    const ImVec2 chevronSize = ImGui::CalcTextSize(kChevronRightIcon);
    ImGui::GetWindowDrawList()->AddText(ImVec2{rowMax.x - chevronSize.x, rowMin.y + .5f * (rowMax.y - rowMin.y - chevronSize.y)}, ImGui::GetColorU32(ImGuiCol_Text), kChevronRightIcon);
    // selectables go to the right side of the page but only add their labels to the measured contents of the page
    ImGui::SameLine(0.f, spacing);
    ImGui::Dummy(ImVec2{chevronSize.x, 0.f});
    return isClicked;
}

} // namespace

bool beginPopupSheet(const char* id, PopupFlags flags) { return beginPopupSheetEx(ImGui::GetID(id), flags); }

bool beginPopupSheetEx(ImGuiID popupId, PopupFlags flags) {
    ImGuiStorage&              storage       = *ImGui::GetStateStorage();
    const std::vector<ImGuiID> path          = getPathToCurrentlyOpenPage(storage, popupId);
    const std::span            pathSpan      = path;
    PageSwipeState             topSwipe      = readTopPageSwipeState(storage, popupId);
    const SheetEdge            edge          = LookAndFeel::instance().sheetEdge();
    const float                topLength     = sheetPopupChildWindowPageLength(popupId, pageChildId(popupId, pathSpan), edge);
    const float                contentLength = path.empty() ? topLength : std::lerp(sheetPopupChildWindowPageLength(popupId, pageChildId(popupId, pathSpan.first(path.size() - 1UZ)), edge), topLength, topSwipe.pageVisibleFraction);

    bool backButtonPressed = false;
    if (!beginSheetPopup(popupId, contentLength, path.empty() ? nullptr : &backButtonPressed, flags)) {
        truncatePagePath(storage, popupId, 0UZ);
        writeTopPageSwipeState(storage, popupId, kOpenPageSwipeState);
        return false;
    }
    if (backButtonPressed) {
        topSwipe.isClosing = true;
        writeTopPageSwipeState(storage, popupId, topSwipe);
    }
    const std::optional<CurrentSheet> sheet = currentSheet();
    assert(sheet.has_value());
    beginPage(*sheet, 0UZ, pageChildId(popupId, {}), ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail());
    return true;
}

void endPopupSheet() {
    const std::optional<CurrentSheet> sheet = currentSheet();
    assert(sheet.has_value());
    endPage(*sheet, 0UZ);
    ImGui::EndChild();
    closePagesNotDrawnThisFrame(*sheet);
    ImGui::EndPopup();
}

bool beginPopupSheetMenu(const char* label, const char* icon, bool isEnabled) {
    const std::optional<CurrentSheet> sheet = currentSheet();
    assert(sheet.has_value());
    std::vector<ImGuiID>             path  = getPathToCurrentlyOpenPage(*sheet->callerStorage, sheet->popupId);
    const std::optional<std::size_t> depth = pageDepth(*ImGui::GetCurrentWindow(), sheet->popupId, path);
    assert(depth.has_value() && "beginPopupSheetMenu() must be called directly inside a sheet popup");

    const ImGuiID menuId = ImGui::GetID(label);
    if (drawSheetMenuRow(label, icon, isEnabled)) {
        truncatePagePath(*sheet->callerStorage, sheet->popupId, *depth);
        sheet->callerStorage->SetInt(pageMenuIdStorageId(sheet->popupId, *depth + 1UZ), static_cast<int>(menuId));
        writeTopPageSwipeState(*sheet->callerStorage, sheet->popupId, PageSwipeState{.pageVisibleFraction = 0.f, .isClosing = false});
        path = getPathToCurrentlyOpenPage(*sheet->callerStorage, sheet->popupId);
    }
    if (!isEnabled || *depth >= path.size() || path[*depth] != menuId) {
        return false;
    }
    const ImGuiWindow& parentPage = *ImGui::GetCurrentWindow();
    parentPageLayouts().push_back(readCurrentWindowLayout());
    if (ImGui::GetStateStorage()->GetBool(isPageHiddenStorageId(), false)) {
        ImGui::PopClipRect(); // a child window is clipped by the clip rectangle of its parent window
    }
    beginPage(*sheet, *depth + 1UZ, pageChildId(sheet->popupId, std::span(path).first(*depth + 1UZ)), parentPage.Pos, parentPage.Size);
    return true;
}

void endPopupSheetMenu() {
    const std::optional<CurrentSheet> sheet = currentSheet();
    assert(sheet.has_value());
    const std::optional<std::size_t> depth = pageDepth(*ImGui::GetCurrentWindow(), sheet->popupId, getPathToCurrentlyOpenPage(*sheet->callerStorage, sheet->popupId));
    assert(depth.has_value());
    endPage(*sheet, *depth);
    ImGui::EndChild();
    restoreCurrentWindowLayout(parentPageLayouts().back());
    parentPageLayouts().pop_back();
    if (ImGui::GetStateStorage()->GetBool(isPageHiddenStorageId(), false)) {
        pushHiddenPageClipRect();
    }
}

bool isCurrentPopupASheet() { return currentSheet().has_value(); }

bool isCurrentWindowSheetContents() {
    const std::optional<CurrentSheet> sheet = currentSheet();
    return sheet.has_value() && pageDepth(*ImGui::GetCurrentWindow(), sheet->popupId, getPathToCurrentlyOpenPage(*sheet->callerStorage, sheet->popupId)).has_value();
}

void closeCurrentPopupSheet() {
    const std::optional<CurrentSheet> sheet = currentSheet();
    assert(sheet.has_value());
    sheet->callerStorage->SetBool(sheet->storageIds.isClosing, true);
}

namespace {
/// @param callerStorage the storage of the window that draws the sheet
[[nodiscard]] bool isSheetOrItsTopPageAnimating(ImGuiID popupId, const ImGuiStorage& callerStorage) {
    const SheetStorageIds storageIds         = sheetStorageIds(popupId);
    const ImGuiID         activeId           = ImGui::GetActiveID();
    const bool            isSheetAnimating   = isPageSwipeAnimating(readSheetSwipeStorage(callerStorage, storageIds), activeId == storageIds.headerButton);
    const bool            isTopPageAnimating = isPageSwipeAnimating(readTopPageSwipeState(callerStorage, popupId), activeId == createSheetStorageId(popupId, "##pageSwipe"));
    return isSheetAnimating || isTopPageAnimating;
}
} // namespace

namespace detail {
bool isPopupSheetAnimating(const char* id) { return ImGui::IsPopupOpen(id) && isSheetOrItsTopPageAnimating(ImGui::GetID(id), *ImGui::GetStateStorage()); }

bool isAnyPopupSheetAnimating() {
    for (const ImGuiPopupData& popup : ImGui::GetCurrentContext()->OpenPopupStack) {
        if (popup.Window == nullptr || popup.Window->ParentWindow == nullptr) {
            continue; // opened in this frame, and not begun yet
        }
        const ImGuiStorage& callerStorage = *popup.Window->ParentWindow->DC.StateStorage;
        const bool          isSheet       = callerStorage.GetFloat(sheetStorageIds(popup.PopupId).pageVisibleFraction, kAbsentPageVisibleFraction) != kAbsentPageVisibleFraction;
        if (isSheet && isSheetOrItsTopPageAnimating(popup.PopupId, callerStorage)) {
            return true;
        }
    }
    return false;
}
} // namespace detail
} // namespace gr::od
