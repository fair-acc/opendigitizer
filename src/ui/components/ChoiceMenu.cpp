#include "ChoiceMenu.hpp"

#include "RadialMenu.hpp"
#include "StyledWidgets.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include <cassert>
#include <vector>

namespace gr::od {
namespace {

enum class ChoiceMenuKind { Desktop, Sheet, Radial };

/// Every frame the items will Begin and push this to a stack and then pop it when it Ends
struct ChoiceMenuFrameState {
    ChoiceMenuKind kind;
    // in Sheet mode we put a gap this big instead of ImGui::Separator, it looks better
    // (this has to be stored because it's derived from a style var that we push to/override during the sheet drawing)
    float sheetSeparatorHeight = 0.f;
};

[[nodiscard]] std::vector<ChoiceMenuFrameState>& choiceMenusInProgress() {
    static std::vector<ChoiceMenuFrameState> menus;
    return menus;
}

[[nodiscard]] const ChoiceMenuFrameState& innermostChoiceMenu() {
    assert(!choiceMenusInProgress().empty() && "choices must be drawn between beginChoiceMenu() and endChoiceMenu()");
    return choiceMenusInProgress().back();
}

[[nodiscard]] ChoiceMenuKind choiceMenuKind(ChoiceMenuFlags flags) {
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    if (lookAndFeel.choiceMenuStyle == ChoiceMenuStyle::Radial && hasFlag(flags, ChoiceMenuFlags::SupportsRadialMenu)) {
        return ChoiceMenuKind::Radial;
    }
    return lookAndFeel.usesSheetPopups() ? ChoiceMenuKind::Sheet : ChoiceMenuKind::Desktop;
}

bool beginNonRadialMenu(const char* popupID, ChoiceMenuFlags flags, ChoiceMenuFrameState& menu) {
    const PopupFlags popupFlags = hasFlag(flags, ChoiceMenuFlags::WantsExclusiveFocus) ? PopupFlags::WantsExclusiveFocus : PopupFlags::None;
    if (!beginPopup(popupID, PopupOptions{.flags = popupFlags})) {
        return false;
    }
    if (menu.kind == ChoiceMenuKind::Sheet) {
        menu.sheetSeparatorHeight = ImGui::GetStyle().ItemSpacing.y;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ImGui::GetStyle().ItemSpacing.x, 0.f});
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2{0.f, .5f}); // vertically centred labels
    }
    return true;
}

[[nodiscard]] float sheetRowHeight() { return ImGui::GetFrameHeight() * LookAndFeel::instance().sheetRowHeightFrameHeightRatio; }

[[nodiscard]] ImVec4 sheetTextColor(const ChoiceMenuItemLook& look) { return look.color.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)); }

} // namespace

bool beginChoiceMenu(const char* popupID, ChoiceMenuFlags flags) {
    ChoiceMenuFrameState menu{.kind = choiceMenuKind(flags)};
    const bool           isOpen = menu.kind == ChoiceMenuKind::Radial ? beginRadialMenu(popupID) : beginNonRadialMenu(popupID, flags, menu);
    if (!isOpen) {
        return false;
    }
    choiceMenusInProgress().push_back(menu);
    return true;
}

void endChoiceMenu() {
    const ChoiceMenuKind kind = innermostChoiceMenu().kind;
    choiceMenusInProgress().pop_back();
    switch (kind) {
    case ChoiceMenuKind::Radial: endRadialMenu(); return;
    case ChoiceMenuKind::Sheet: ImGui::PopStyleVar(2); break;
    case ChoiceMenuKind::Desktop: break;
    }
    endPopup();
}

bool choiceMenuAction(const char* label, const ChoiceMenuItemLook& look) {
    switch (innermostChoiceMenu().kind) {
    case ChoiceMenuKind::Desktop: return menuItem(label);
    case ChoiceMenuKind::Sheet: {
        imw::StyleColor textColor(ImGuiCol_Text, sheetTextColor(look));
        return selectable(label, false, ImGuiSelectableFlags_None, ImVec2{0.f, sheetRowHeight()});
    }
    case ChoiceMenuKind::Radial: return radialMenuAction(label, look);
    }
    std::unreachable();
}

bool choiceMenuToggle(const char* label, bool* isChecked, const ChoiceMenuItemLook& look) {
    switch (innermostChoiceMenu().kind) {
    case ChoiceMenuKind::Desktop: {
        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false); // toggles keep the menu open
        const bool isClicked = menuItem(label, nullptr, *isChecked);
        ImGui::PopItemFlag();
        if (isClicked) {
            *isChecked = !*isChecked;
        }
        return isClicked;
    }
    case ChoiceMenuKind::Sheet: {
        imw::StyleColor textColor(ImGuiCol_Text, sheetTextColor(look));
        const float     rowHeight        = sheetRowHeight();
        const float     frameHeight      = ImGui::GetFrameHeight();
        const float     spaceAboveToggle = .5f * (rowHeight - frameHeight);
        ImGui::Dummy(ImVec2{0.f, spaceAboveToggle});
        const bool isClicked = checkbox(label, isChecked);
        ImGui::Dummy(ImVec2{0.f, rowHeight - frameHeight - spaceAboveToggle});
        return isClicked;
    }
    case ChoiceMenuKind::Radial: return radialMenuToggle(label, isChecked, look);
    }
    std::unreachable();
}

bool beginChoiceSubmenu(const char* label, const ChoiceMenuItemLook& look) {
    if (innermostChoiceMenu().kind == ChoiceMenuKind::Radial) {
        return beginRadialSubmenu(label, look);
    }
    return beginMenu(label);
}

void endChoiceSubmenu() {
    if (innermostChoiceMenu().kind == ChoiceMenuKind::Radial) {
        endRadialSubmenu();
    } else {
        endMenu();
    }
}

void choiceMenuSeparator() {
    const ChoiceMenuFrameState& menu = innermostChoiceMenu();
    switch (menu.kind) {
    case ChoiceMenuKind::Desktop: ImGui::Separator(); return;
    case ChoiceMenuKind::Sheet: {
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        const float  width    = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2{0.f, menu.sheetSeparatorHeight});
        const float lineY = rowStart.y + .5f * menu.sheetSeparatorHeight;
        ImGui::GetWindowDrawList()->AddLine(ImVec2{rowStart.x, lineY}, ImVec2{rowStart.x + width, lineY}, ImGui::GetColorU32(ImGuiCol_Separator));
        return;
    }
    case ChoiceMenuKind::Radial: return; // separators aren't supported by the radial menu
    }
}

namespace detail {
bool isChoiceMenuAnimating(const char* popupID) { return isPopupAnimating(popupID) || isRadialMenuAnimating(popupID); }
} // namespace detail

} // namespace gr::od
