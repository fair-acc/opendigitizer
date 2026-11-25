#ifndef OPENDIGITIZER_UI_COMPONENTS_SHEET_POPUP_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SHEET_POPUP_HPP_

#include <stdex/c_resource.hpp>

#include <imgui.h>

#include "StyledWidgets.hpp"

namespace gr::od {

/// Probably don't call this directly, instead use beginPopup() which will automatically dispatch to the
/// correctly styled popup.
///
/// This is a mobile-friendly popup which can be opened the same way as regular popups, OpenPopup(id). It can be closed
/// externally with closePopup() or from within its contents with closeCurrentPopupSheet()
///
/// The size and position of the sheet cannot be controlled by SetNextWindow...() calls.
bool beginPopupSheet(const char* id, PopupFlags flags);
bool beginPopupSheetEx(ImGuiID popupId, PopupFlags flags);
void endPopupSheet();
using Sheet = stdex::c_resource<bool, beginPopupSheet, endPopupSheet>;

/// Whether the most nested popup whose contents are being drawn is a sheet
[[nodiscard]] bool isCurrentPopupASheet();

/// Start the closing animation of the most nested sheet popup whose contents are being drawn.
/// It is only valid to call this if isCurrentPopupASheet()
void closeCurrentPopupSheet();

/// Returns true if the current window is the direct child window (where the contents are drawn) of the currently
/// visible sheet popup. Returns false otherwise.
[[nodiscard]] bool isCurrentWindowSheetContents();

/// Draw a row in the popup which is a button with an @param icon, a @param text, and then a right arrow >
/// which, when pressed, opens a subpage. If this function returns true then the subpage is open and
/// the caller should begin drawing its contents
bool beginPopupSheetMenu(const char* label, const char* icon, bool isEnabled);
void endPopupSheetMenu();

// this namespace is exposed for tests to use
namespace detail {
inline constexpr const char* kSheetBackButtonLabel  = "\u{f060}##sheetBack";  // arrow-left
inline constexpr const char* kSheetCloseButtonLabel = "\u{f00d}##sheetClose"; // xmark

[[nodiscard]] bool isPopupSheetAnimating(const char* id);

/// Whether any open sheet slides in or out, or slides one of its pages, for tests that cannot name the ID stack the
/// sheet was opened in
[[nodiscard]] bool isAnyPopupSheetAnimating();
} // namespace detail

} // namespace gr::od

#endif
