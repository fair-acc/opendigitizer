#ifndef OPENDIGITIZER_UI_COMPONENTS_CHOICE_MENU_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_CHOICE_MENU_HPP_

#include <stdex/c_resource.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace gr::od {

struct ChoiceMenuItemLook {
    const char*           icon  = nullptr;
    std::optional<ImVec4> color = std::nullopt; // ImGuiCol_Button by default
};

enum class ChoiceMenuFlags : std::uint8_t {
    None = 0,
    /// darken the background outside of the choice menu if true (only applies for sheet popups ATM)
    WantsExclusiveFocus = 1 << 0,
    /// This should be specified if the menu is only going to have actions, toggles and choice submenus, it enables
    /// radial menu in the event that that is the global popup style. By default a popup assumes that
    /// it doesn't work as a radial menu and just automatically picks based on whether a touchscreen
    /// is in use
    SupportsRadialMenu = 1 << 1,
};
[[nodiscard]] constexpr ChoiceMenuFlags operator|(ChoiceMenuFlags lhs, ChoiceMenuFlags rhs) { return static_cast<ChoiceMenuFlags>(std::to_underlying(lhs) | std::to_underlying(rhs)); }
[[nodiscard]] constexpr bool            hasFlag(ChoiceMenuFlags flags, ChoiceMenuFlags flag) { return (std::to_underlying(flags) & std::to_underlying(flag)) != 0; }

/// A choice menu is a generic version of a context menu. On desktop it appears as a normal context menu
/// popup, and when using a touchscreen it appears as a sheet popup. It also supports using radial menus
/// for some subsets of the choice menu. closePopup(popupID) closes it in any of these styles.
/// @returns whether the contents of the menu should be drawn
bool beginChoiceMenu(const char* popupID, ChoiceMenuFlags flags = ChoiceMenuFlags::WantsExclusiveFocus);
void endChoiceMenu();
using ChoiceMenu = stdex::c_resource<bool, beginChoiceMenu, endChoiceMenu>;

/// Add a button control to the menu
/// @returns true if it was clicked
bool choiceMenuAction(const char* label, const ChoiceMenuItemLook& look = {});
/// Add a toggle control to the menu
/// @returns true if it wrote to isChecked
bool choiceMenuToggle(const char* label, bool* isChecked, const ChoiceMenuItemLook& look = {});
/// Add a submenu to the menu, whose choices are drawn between beginChoiceSubmenu() and endChoiceSubmenu(). On desktop
/// it opens beside the menu, in a sheet as a page, and in a radial menu as spokes around its item (beginRadialSubmenu())
/// @returns whether the choices of the submenu should be drawn
bool beginChoiceSubmenu(const char* label, const ChoiceMenuItemLook& look);
void endChoiceSubmenu();
using ChoiceSubmenu = stdex::c_resource<bool, beginChoiceSubmenu, endChoiceSubmenu>;
void choiceMenuSeparator();

namespace detail {
/// Whether the menu is doing some open/close animation, useful for testing
[[nodiscard]] bool isChoiceMenuAnimating(const char* popupID);
} // namespace detail

} // namespace gr::od

#endif
