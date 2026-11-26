#include "RadialMenu.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace gr::od {
namespace {

constexpr const char* kMissingIconIcon = "\u{f128}"; // question

/// Where the items of the menu, or of one of its open submenus, are. Item `index` is in the direction of the angle
/// firstItemAngle + index * angleBetweenItems, expansion * spokeLength away from the center
struct RadialMenuLayout {
    ImVec2      center;
    float       spokeLength;
    float       expansion; // grows from 0 to 1 while a submenu opens
    float       spokeStartRadius;
    float       firstItemAngle;
    float       angleBetweenItems;
    std::size_t itemCount; // number of items drawn in the previous frame. zero indicates that the menu or submenu just opened
};

struct RadialMenuLevel {
    RadialMenuLayout layout;
    std::size_t      drawnItemCount = 0UZ;
};

/// The radial menu whose items are being drawn in this frame
struct RadialMenuInProgress {
    ImGuiStorage*                callerStorage;
    ImGuiID                      popupId;
    std::vector<RadialMenuLevel> levels; // the menu, then each submenu whose items are being drawn, from the outermost to the innermost
    std::size_t                  deepestOpenSubmenuDepth = 0UZ;
    bool                         isMouseOverMenu         = false; // over the square around the menu, or the rectangle around an open submenu
    bool                         isAnyItemHovered        = false;
};

[[nodiscard]] std::optional<RadialMenuInProgress>& radialMenuInProgress() {
    static std::optional<RadialMenuInProgress> menu;
    return menu;
}

[[nodiscard]] RadialMenuInProgress& currentRadialMenu() {
    assert(radialMenuInProgress().has_value() && "items must be drawn between beginRadialMenu() and endRadialMenu()");
    return *radialMenuInProgress();
}

/// An ID in the ID space of the popup, so it is the same from the window that draws the menu and from inside the menu.
/// `depth` is 0 for the menu, 1 for the submenu opened from it, 2 for the submenu opened from that submenu, and so on
[[nodiscard]] ImGuiID radialMenuStateId(ImGuiID popupId, const char* name, std::size_t depth = 0UZ) {
    imw::OverrideId  popupScope(popupId);
    imw::ChangeStrId nameScope(name);
    return ImGui::GetID(static_cast<int>(depth));
}

[[nodiscard]] ImGuiID itemCountKey(ImGuiID popupId, std::size_t depth) { return radialMenuStateId(popupId, "itemCount", depth); }
[[nodiscard]] ImGuiID fadeInProgressKey(ImGuiID popupId) { return radialMenuStateId(popupId, "fadeInProgress"); }
[[nodiscard]] ImGuiID openSubmenuKey(ImGuiID popupId, std::size_t depth) { return radialMenuStateId(popupId, "openSubmenu", depth); }
[[nodiscard]] ImGuiID submenuExpansionKey(ImGuiID popupId, std::size_t depth) { return radialMenuStateId(popupId, "submenuExpansion", depth); }

/// The ID of the item of the submenu that is open at `depth`, or 0 if no submenu is open there
[[nodiscard]] ImGuiID openSubmenuId(const ImGuiStorage& storage, ImGuiID popupId, std::size_t depth) { return static_cast<ImGuiID>(storage.GetInt(openSubmenuKey(popupId, depth), 0)); }

/// Closes the submenu that is open at `depth`, and the submenus that are open inside of it
void closeSubmenusFrom(ImGuiStorage& storage, ImGuiID popupId, std::size_t depth) {
    for (; openSubmenuId(storage, popupId, depth) != 0U; ++depth) {
        storage.SetInt(openSubmenuKey(popupId, depth), 0);
    }
}

void openSubmenu(ImGuiStorage& storage, ImGuiID popupId, std::size_t depth, ImGuiID submenuId) {
    closeSubmenusFrom(storage, popupId, depth);
    storage.SetInt(openSubmenuKey(popupId, depth), static_cast<int>(submenuId));
    storage.SetInt(itemCountKey(popupId, depth), 0);
    storage.SetFloat(submenuExpansionKey(popupId, depth), 0.f);
}

[[nodiscard]] float itemRadius() { return .5f * ImGui::GetFrameHeight() * LookAndFeel::instance().radialMenuItemDiameterFrameHeightRatio; }

/// The radius of the outline around the circle of an item, which grows with the circle
[[nodiscard]] float itemOutlineRadius(float hoverScale) { return (itemRadius() + LookAndFeel::instance().radialMenuOutlineThickness) * hoverScale; }

[[nodiscard]] float hoveredItemOutlineRadius() { return itemOutlineRadius(LookAndFeel::instance().radialMenuHoveredScale); }

[[nodiscard]] float centerDotRadius() { return ImGui::GetFrameHeight() * LookAndFeel::instance().radialMenuCenterDotRadiusFrameHeightRatio; }

[[nodiscard]] float minSpokeLength() { return 2.f * itemRadius() * LookAndFeel::instance().radialMenuSpokeMinLengthItemDiameterRatio; }

/// Half the shortest distance between the centers of two items at which their outlines do not overlap while both are hovered
[[nodiscard]] float halfDistanceBetweenHoveredItems() { return hoveredItemOutlineRadius() + .5f * ImGui::GetStyle().ItemSpacing.x; }

/// Half the shortest distance between the centers of two items at which their outlines do not overlap while neither is hovered
[[nodiscard]] float halfDistanceBetweenUnhoveredItems() { return itemOutlineRadius(1.f) + .5f * ImGui::GetStyle().ItemSpacing.x; }

/// The shortest spoke length at which the outlines of neighbouring hovered items do not overlap
[[nodiscard]] float spokeLength(std::size_t itemCount) {
    if (itemCount < 2UZ) {
        return minSpokeLength();
    }
    // the centers of neighbouring items are 2 * length * sin(pi / itemCount) apart
    return std::max(minSpokeLength(), halfDistanceBetweenHoveredItems() / std::sin(std::numbers::pi_v<float> / static_cast<float>(itemCount)));
}

/// The largest angle between a spoke of a submenu and the direction from the center of `parentLayout` to the item of the
/// submenu, at which the items of the submenu do not overlap with the neighbours of the item of the submenu while
/// neither is hovered
[[nodiscard]] float maxHalfSubmenuSpread(const RadialMenuLayout& parentLayout) {
    const float halfPi = .5f * std::numbers::pi_v<float>;
    if (parentLayout.itemCount < 2UZ) {
        return halfPi;
    }
    // seen from the item of the submenu, a neighbour is 2 * spokeLength * sin(angleBetweenItems / 2) away, in the direction
    // (pi + angleBetweenItems) / 2 away from the direction from the center of the parent. A spoke stays outside of the
    // circle around the neighbour that the items of the submenu must not reach into if it is turned less than that
    // direction minus the angle the circle covers
    const float neighbourDistance = 2.f * parentLayout.spokeLength * std::sin(.5f * parentLayout.angleBetweenItems);
    const float neighbourAngle    = halfPi + .5f * parentLayout.angleBetweenItems;
    return neighbourAngle - std::asin(std::min(1.f, 2.f * halfDistanceBetweenUnhoveredItems() / neighbourDistance));
}

/// The shortest spoke length of a submenu with `itemCount` items at which the outlines of neighbouring items that are not
/// hovered do not overlap, and the outermost spokes are turned at most `maxHalfSpread` away from the middle of the submenu
[[nodiscard]] float submenuSpokeLength(std::size_t itemCount, float maxHalfSpread) {
    const float minLength = std::max(minSpokeLength(), 2.f * halfDistanceBetweenUnhoveredItems());
    if (itemCount < 2UZ) {
        return minLength;
    }
    // the centers of neighbouring items are 2 * length * sin(angleBetweenItems / 2) apart, and the outermost items are
    // (itemCount - 1) / 2 * angleBetweenItems away from the middle
    const float maxHalfAngleBetweenItems = std::min(.5f * std::numbers::pi_v<float>, maxHalfSpread / static_cast<float>(itemCount - 1UZ));
    return std::max(minLength, halfDistanceBetweenUnhoveredItems() / std::sin(maxHalfAngleBetweenItems));
}

/// The smallest angle between the spokes of a submenu at which the outlines of neighbouring items that are not hovered do
/// not overlap
[[nodiscard]] float angleBetweenSubmenuItems(float spokeLength) { return 2.f * std::asin(std::min(1.f, halfDistanceBetweenUnhoveredItems() / spokeLength)); }

[[nodiscard]] float itemAngle(const RadialMenuLayout& layout, std::size_t index) { return layout.firstItemAngle + layout.angleBetweenItems * static_cast<float>(index); }

[[nodiscard]] ImVec2 itemDirection(const RadialMenuLayout& layout, std::size_t index) {
    const float angle = itemAngle(layout, index);
    return ImVec2{std::cos(angle), std::sin(angle)};
}

[[nodiscard]] ImVec2 itemCenter(const RadialMenuLayout& layout, std::size_t index) { return layout.center + itemDirection(layout, index) * (layout.expansion * layout.spokeLength); }

[[nodiscard]] ImRect hoveredItemRect(ImVec2 center) {
    const float  outlineRadius = hoveredItemOutlineRadius();
    const ImVec2 halfSize{outlineRadius, outlineRadius};
    return ImRect(center - halfSize, center + halfSize);
}

/// The rectangle that contains the item of a submenu and the items of the submenu, at any size and once its spokes have
/// grown
[[nodiscard]] ImRect expandedSubmenuRect(RadialMenuLayout layout) {
    layout.expansion = 1.f;
    ImRect rect      = hoveredItemRect(layout.center);
    for (std::size_t index = 0UZ; index < layout.itemCount; ++index) {
        rect.Add(hoveredItemRect(itemCenter(layout, index)));
    }
    return rect;
}

[[nodiscard]] bool isMouseInRect(const ImRect& rect) { return ImGui::IsMouseHoveringRect(rect.Min, rect.Max, false); }

/// The position the popup that is opened at the level of the next ImGui::BeginPopup() call was opened at. ImGui has no
/// public API for it outside of the popup
[[nodiscard]] ImVec2 nextPopupOpenPosition() {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    return context.OpenPopupStack[context.BeginPopupStack.Size].OpenPopupPos;
}

/// `position` moved as little as possible so that a square with half the side length `extent` around it is inside of the
/// main viewport, or so that it is at the top left of the viewport when the viewport is smaller than the square
[[nodiscard]] ImVec2 clampedIntoViewport(ImVec2 position, float extent) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2         min      = viewport->Pos + ImVec2{extent, extent};
    const ImVec2         max      = viewport->Pos + viewport->Size - ImVec2{extent, extent};
    return ImVec2{std::max(min.x, std::min(position.x, max.x)), std::max(min.y, std::min(position.y, max.y))};
}

struct DrawnItem {
    bool   isClicked;
    ImVec2 center;
    float  angle; // of its spoke
    float  outlineRadius;
};

/// Draws an item of the innermost menu or submenu as a circle in `circleColor` with its icon in `iconColor`, an outline
/// around it, and a spoke from the center of the menu or submenu to it. A hovered item grows. Only counts the item while
/// the menu or submenu has no layout for it
/// @returns the drawn item, or nothing if the item was only counted
std::optional<DrawnItem> drawItem(RadialMenuInProgress& menu, const char* label, const char* icon, ImVec4 circleColor, ImVec4 iconColor) {
    RadialMenuLevel&  level = menu.levels.back();
    const std::size_t index = level.drawnItemCount++;
    if (index >= level.layout.itemCount) {
        return std::nullopt;
    }
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    const ImVec2       direction   = itemDirection(level.layout, index);
    const ImVec2       center      = itemCenter(level.layout, index);

    ImGuiStorage& storage          = *ImGui::GetStateStorage();
    const ImGuiID hoverProgressKey = ImHashStr("##hoverProgress", 0, ImGui::GetID(label));
    const float   hoverProgress    = storage.GetFloat(hoverProgressKey, 0.f);
    const float   hoverScale       = std::lerp(1.f, lookAndFeel.radialMenuHoveredScale, hoverProgress);
    const float   outlineRadius    = itemOutlineRadius(hoverScale);

    ImGui::SetCursorScreenPos(center - ImVec2{outlineRadius, outlineRadius});
    const bool  isClicked = ImGui::InvisibleButton(label, ImVec2{2.f * outlineRadius, 2.f * outlineRadius});
    const float hoverStep = ImGui::GetIO().DeltaTime / lookAndFeel.radialMenuHoverAnimationSeconds;
    storage.SetFloat(hoverProgressKey, ImGui::IsItemHovered() ? std::min(1.f, hoverProgress + hoverStep) : std::max(0.f, hoverProgress - hoverStep));
    if (const char* labelEnd = ImGui::FindRenderedTextEnd(label); labelEnd != label) {
        ImGui::SetItemTooltip("%.*s", static_cast<int>(labelEnd - label), label);
    }
    menu.isAnyItemHovered = menu.isAnyItemHovered || ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

    ImDrawList* const drawList         = ImGui::GetWindowDrawList();
    const ImU32       outlineColor     = ImGui::GetColorU32(ImGuiCol_Text);
    const float       outlineThickness = lookAndFeel.radialMenuOutlineThickness * hoverScale;
    const float       circleRadius     = itemRadius() * hoverScale;
    if (level.layout.expansion * level.layout.spokeLength > level.layout.spokeStartRadius + outlineRadius) {
        drawList->AddLine(level.layout.center + direction * level.layout.spokeStartRadius, center - direction * outlineRadius, outlineColor, lookAndFeel.radialMenuSpokeThickness);
    }
    drawList->AddCircle(center, circleRadius + .5f * outlineThickness, outlineColor, 0, outlineThickness);
    drawList->AddCircleFilled(center, circleRadius, ImGui::GetColorU32(circleColor));

    ImFont* const iconFont          = lookAndFeel.fontIconsSolid;
    const float   maxIconWidth      = 2.f * circleRadius * lookAndFeel.radialMenuIconMaxWidthItemDiameterRatio;
    const float   unscaledIconSize  = ImGui::GetFontSize() * hoverScale;
    const float   unscaledIconWidth = iconFont->CalcTextSizeA(unscaledIconSize, FLT_MAX, 0.f, icon).x;
    const float   iconSize          = unscaledIconWidth > maxIconWidth ? unscaledIconSize * maxIconWidth / unscaledIconWidth : unscaledIconSize;
    const ImVec2  iconMin           = center - iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.f, icon) * .5f;
    drawList->AddText(iconFont, iconSize, ImVec2{std::floor(iconMin.x), std::floor(iconMin.y)}, ImGui::GetColorU32(iconColor), icon);
    return DrawnItem{.isClicked = isClicked, .center = center, .angle = itemAngle(level.layout, index), .outlineRadius = outlineRadius};
}

/// Draws an item that is drawn with its colours swapped while `isHighlighted`, like a checked toggle or an open submenu
std::optional<DrawnItem> drawItemWithLook(RadialMenuInProgress& menu, const char* label, const RadialMenuItemLook& look, bool isHighlighted) {
    ImVec4 circleColor     = look.color.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Button));
    circleColor.w          = 1.f; // the spoke ends below the circle
    const ImVec4 iconColor = LookAndFeel::instance().radialMenuIconColor;
    const char*  icon      = look.icon != nullptr ? look.icon : kMissingIconIcon;
    return isHighlighted ? drawItem(menu, label, icon, iconColor, circleColor) : drawItem(menu, label, icon, circleColor, iconColor);
}

[[nodiscard]] bool wasItemClicked(const std::optional<DrawnItem>& item) { return item.has_value() && item->isClicked; }

} // namespace

bool beginRadialMenu(const char* popupID) {
    assert(!radialMenuInProgress().has_value() && "radial menus cannot be nested");
    const ImGuiID popupId          = ImGui::GetID(popupID);
    const bool    isCloseRequested = detail::takePopupCloseRequest(popupId);
    if (!ImGui::IsPopupOpen(popupID)) {
        return false;
    }
    ImGuiStorage* const callerStorage     = ImGui::GetStateStorage();
    const auto          previousItemCount = static_cast<std::size_t>(callerStorage->GetInt(itemCountKey(popupId, 0UZ), 0));
    const float         length            = spokeLength(previousItemCount);
    const float         extent            = length + hoveredItemOutlineRadius(); // half the side length of the square that contains the menu
    const ImVec2        center            = clampedIntoViewport(nextPopupOpenPosition(), extent);

    // the window covers the viewport, so the items of submenus can be hovered wherever they are
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    const bool isOpen = [popupID] {
        imw::StyleVar      noPadding(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
        imw::StyleFloatVar noBorder(ImGuiStyleVar_PopupBorderSize, 0.f);
        return ImGui::BeginPopup(popupID, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    }();
    if (!isOpen) {
        return false;
    }
    if (isCloseRequested) {
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsWindowAppearing()) {
        closeSubmenusFrom(*callerStorage, popupId, 1UZ);
    }

    // the count of a previous opening may be wrong, so in the frame the menu opens in the items are only counted
    const std::size_t itemCount      = ImGui::IsWindowAppearing() ? 0UZ : previousItemCount;
    const ImGuiID     fadeKey        = fadeInProgressKey(popupId);
    const float       fadeInProgress = itemCount == 0UZ ? 0.f : std::min(1.f, callerStorage->GetFloat(fadeKey, 0.f) + ImGui::GetIO().DeltaTime / LookAndFeel::instance().radialMenuFadeInSeconds);
    callerStorage->SetFloat(fadeKey, fadeInProgress);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * fadeInProgress);

    if (itemCount > 0UZ) {
        ImGui::GetWindowDrawList()->AddCircleFilled(center, centerDotRadius(), ImGui::GetColorU32(ImGuiCol_Text));
    }
    const RadialMenuLayout layout{
        .center            = center,
        .spokeLength       = length,
        .expansion         = 1.f,
        .spokeStartRadius  = centerDotRadius(),
        .firstItemAngle    = -.5f * std::numbers::pi_v<float>,
        .angleBetweenItems = itemCount == 0UZ ? 0.f : 2.f * std::numbers::pi_v<float> / static_cast<float>(itemCount),
        .itemCount         = itemCount,
    };
    radialMenuInProgress().emplace(RadialMenuInProgress{
        .callerStorage   = callerStorage,
        .popupId         = popupId,
        .levels          = {RadialMenuLevel{.layout = layout}},
        .isMouseOverMenu = isMouseInRect(ImRect(center - ImVec2{extent, extent}, center + ImVec2{extent, extent})),
    });
    return true;
}

void endRadialMenu() {
    const RadialMenuInProgress menu = std::move(currentRadialMenu());
    radialMenuInProgress().reset();
    assert(menu.levels.size() == 1UZ && "every submenu must be ended before the menu");
    const RadialMenuLevel& level = menu.levels.front();
    if (level.layout.itemCount > 0UZ) {
        closeSubmenusFrom(*menu.callerStorage, menu.popupId, menu.deepestOpenSubmenuDepth + 1UZ); // for example because the items changed and an open submenu was not drawn
        const bool isMouseClicked        = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        const bool isClickedBetweenItems = isMouseClicked && ImGui::IsWindowHovered() && !menu.isAnyItemHovered;
        const bool isLeftByMouse         = !LookAndFeel::instance().touchscreenIsPrimaryPointer() && !menu.isMouseOverMenu;
        if (isClickedBetweenItems || isLeftByMouse) {
            ImGui::CloseCurrentPopup();
        }
    }
    menu.callerStorage->SetInt(itemCountKey(menu.popupId, 0UZ), static_cast<int>(level.drawnItemCount));
    ImGui::PopStyleVar(); // ImGuiStyleVar_Alpha
    ImGui::EndPopup();
}

bool radialMenuAction(const char* label, const RadialMenuItemLook& look) {
    const bool isItemClicked = wasItemClicked(drawItemWithLook(currentRadialMenu(), label, look, false));
    if (isItemClicked) {
        ImGui::CloseCurrentPopup();
    }
    return isItemClicked;
}

bool radialMenuToggle(const char* label, bool* isChecked, const RadialMenuItemLook& look) {
    const bool isItemClicked = wasItemClicked(drawItemWithLook(currentRadialMenu(), label, look, *isChecked));
    if (isItemClicked) {
        *isChecked = !*isChecked;
    }
    return isItemClicked;
}

bool beginRadialSubmenu(const char* label, const RadialMenuItemLook& look) {
    RadialMenuInProgress&          menu      = currentRadialMenu();
    ImGuiStorage&                  storage   = *menu.callerStorage;
    const std::size_t              depth     = menu.levels.size();
    const ImGuiID                  submenuId = ImGui::GetID(label);
    const bool                     wasOpen   = openSubmenuId(storage, menu.popupId, depth) == submenuId;
    const std::optional<DrawnItem> item      = drawItemWithLook(menu, label, look, wasOpen);
    if (!item.has_value()) {
        return false;
    }
    if (item->isClicked && wasOpen) {
        closeSubmenusFrom(storage, menu.popupId, depth);
    } else if (item->isClicked) {
        openSubmenu(storage, menu.popupId, depth, submenuId);
    }
    if (wasOpen == item->isClicked) {
        return false;
    }

    // the count of a previous opening may be wrong, so in the frame the submenu opens in the items are only counted
    const auto    itemCount    = static_cast<std::size_t>(storage.GetInt(itemCountKey(menu.popupId, depth), 0));
    const ImGuiID expansionKey = submenuExpansionKey(menu.popupId, depth);
    const float   expansion    = itemCount == 0UZ ? 0.f : std::min(1.f, storage.GetFloat(expansionKey, 0.f) + ImGui::GetIO().DeltaTime / LookAndFeel::instance().radialSubmenuExpandSeconds);
    storage.SetFloat(expansionKey, expansion);

    const float            length            = submenuSpokeLength(itemCount, maxHalfSubmenuSpread(menu.levels.back().layout));
    const float            angleBetweenItems = angleBetweenSubmenuItems(length);
    const float            spread            = angleBetweenItems * static_cast<float>(std::max(itemCount, 1UZ) - 1UZ);
    const RadialMenuLayout layout{
        .center            = item->center,
        .spokeLength       = length,
        .expansion         = expansion,
        .spokeStartRadius  = item->outlineRadius,
        .firstItemAngle    = item->angle - .5f * spread,
        .angleBetweenItems = angleBetweenItems,
        .itemCount         = itemCount,
    };
    menu.isMouseOverMenu         = menu.isMouseOverMenu || isMouseInRect(expandedSubmenuRect(layout));
    menu.deepestOpenSubmenuDepth = depth;
    menu.levels.push_back(RadialMenuLevel{.layout = layout});
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * expansion);
    return true;
}

void endRadialSubmenu() {
    RadialMenuInProgress& menu = currentRadialMenu();
    assert(menu.levels.size() > 1UZ && "endRadialSubmenu() must follow a beginRadialSubmenu() that returned true");
    const std::size_t depth = menu.levels.size() - 1UZ;
    menu.callerStorage->SetInt(itemCountKey(menu.popupId, depth), static_cast<int>(menu.levels.back().drawnItemCount));
    menu.levels.pop_back();
    ImGui::PopStyleVar(); // ImGuiStyleVar_Alpha
}

bool isRadialMenuAnimating(const char* popupID) {
    if (!ImGui::IsPopupOpen(popupID)) {
        return false;
    }
    const ImGuiStorage& storage = *ImGui::GetStateStorage();
    const ImGuiID       popupId = ImGui::GetID(popupID);
    if (storage.GetFloat(fadeInProgressKey(popupId), 1.f) < 1.f) {
        return true;
    }
    for (std::size_t depth = 1UZ; openSubmenuId(storage, popupId, depth) != 0U; ++depth) {
        if (storage.GetFloat(submenuExpansionKey(popupId, depth), 1.f) < 1.f) {
            return true;
        }
    }
    return false;
}

} // namespace gr::od
