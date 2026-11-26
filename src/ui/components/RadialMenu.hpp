#ifndef OPENDIGITIZER_UI_COMPONENTS_RADIAL_MENU_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_RADIAL_MENU_HPP_

#include "ChoiceMenu.hpp"

#include <stdex/c_resource.hpp>

#include <imgui.h>

namespace gr::od {

using RadialMenuItemLook = ChoiceMenuItemLook;

/// A popup drawn around the position it was opened at, as a dot with a spoke to each of its items. An item is a circle
/// with its icon and an outline, which grow while the item is hovered, and shows its label as a tooltip. Open it with
/// ImGui::OpenPopup(popupID). Between beginRadialMenu() and endRadialMenu() draw the items with radialMenuAction(),
/// radialMenuToggle() and beginRadialSubmenu(), and nothing else. An item is disabled between ImGui::BeginDisabled() and
/// ImGui::EndDisabled(), for example in the scope of an imw::Disabled.
///
/// The items are laid out by how many were drawn in the previous frame, so in the frame the menu opens it draws nothing
/// and only counts them. Then it fades in. A click between the items closes the menu. While a touchscreen is not the
/// primary pointing device (LookAndFeel::touchscreenIsPrimaryPointer()), the menu also closes when the mouse leaves the
/// square around the menu and the rectangles around its open submenus.
///
/// End it with endRadialMenu() when it returns true
/// @returns whether the items of the menu are drawn
bool beginRadialMenu(const char* popupID);
void endRadialMenu();
using RadialMenu = stdex::c_resource<bool, beginRadialMenu, endRadialMenu>;

/// An item that closes the menu when it is clicked
/// @returns whether it was clicked
bool radialMenuAction(const char* label, const RadialMenuItemLook& look = {});

/// An item that flips `*isChecked` when it is clicked, and keeps the menu open. A checked toggle is drawn with its
/// circle in the colour of the icon, and its icon in the colour of the circle
/// @returns whether `*isChecked` was flipped
bool radialMenuToggle(const char* label, bool* isChecked, const RadialMenuItemLook& look = {});

/// An item that opens a submenu when it is clicked, and closes it when it is clicked again. The items of the submenu are
/// drawn between beginRadialSubmenu() and endRadialSubmenu(), with the same functions as the items of the menu. They are
/// on spokes from the item of the submenu, clustered around the direction that points away from the center of the
/// menu, and spread at most as far as they can without overlapping with the neighbours of the item of the submenu while
/// neither is hovered. An open submenu is drawn with its
/// circle in the colour of the icon, and its icon in the colour of the circle. Opening a submenu closes the other
/// submenu of the same menu. The spokes grow out of the item when the submenu opens, after a frame in which the
/// submenu draws nothing and only counts its items.
///
/// End it with endRadialSubmenu() when it returns true
/// @returns whether the items of the submenu are drawn
bool beginRadialSubmenu(const char* label, const RadialMenuItemLook& look);
void endRadialSubmenu();
using RadialSubmenu = stdex::c_resource<bool, beginRadialSubmenu, endRadialSubmenu>;

/// Whether the radial menu that beginRadialMenu(popupID) draws from the current window is open and has not faded in yet,
/// or one of its open submenus has not finished growing its spokes. A menu or submenu without items never finishes
[[nodiscard]] bool isRadialMenuAnimating(const char* popupID);

} // namespace gr::od

#endif
