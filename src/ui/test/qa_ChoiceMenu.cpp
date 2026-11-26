#include "ImGuiTestApp.hpp"

#include <common/Imw.hpp>
#include <common/LookAndFeel.hpp>
#include <components/ChoiceMenu.hpp>
#include <components/SheetPopup.hpp>
#include <components/StyledWidgets.hpp>

#include <boost/ut.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace gr::od;

using namespace boost::ut;
using test::resizeViewport;
using test::waitUntil;

namespace {

constexpr const char* kTestWindow = "Test Window";
constexpr const char* kMenu       = "menu";
constexpr const char* kPlainSheet = "plainSheet";
constexpr const char* kModalSheet = "modalSheet";
constexpr const char* kOtherPopup = "otherPopup";
constexpr const char* kSliderMenu = "sliderMenu";

constexpr const char*                kRadialMenu = "radialMenu";
constexpr std::array<const char*, 4> kRadialChoices{"Radial action", "Radial toggle", "Radial disabled", "Radial without icon"};
constexpr const char*                kRadialSubmenu = "Radial submenu";
// in the order they are drawn. The submenu is the third item, so it points to the bottom right, into the viewport, when
// the menu is opened in the top left corner of the viewport
constexpr std::array<const char*, 5> kRadialItems{kRadialChoices[0], kRadialChoices[1], kRadialSubmenu, kRadialChoices[2], kRadialChoices[3]};
constexpr const char*                kRadialDeeperSubmenu = "Radial deeper submenu";
// the deeper submenu is the middle item, so it points away from the menu as the submenu does
constexpr std::array<const char*, 3> kRadialSubmenuItems{"Radial nested action", kRadialDeeperSubmenu, "Radial other nested action"};
constexpr const char*                kRadialDeepestAction = "Radial deepest action";
// a menu without ChoiceMenuFlags::SupportsRadialMenu, drawn while the choice menu style is ChoiceMenuStyle::Radial
constexpr const char*                kFallbackMenu = "fallbackMenu";
constexpr std::array<const char*, 3> kFallbackChoices{"Fallback first", "Fallback second", "Fallback third"};

// a slider with few steps, so a drag of a few pixels does not change its value
constexpr int         kSliderMin = 0;
constexpr int         kSliderMax = 2;
constexpr const char* kCombo     = "##combo";
// the popup ID that beginCombo() gives the sheet of a combo box, in the ID space of the combo box
constexpr const char* kComboSheet = "##ComboPopup";

constexpr std::array<const char*, 3> kComboEntries{"First", "Second", "Third"};

constexpr ImVec2 kPortraitViewportSize{700.f, 1000.f};
constexpr ImVec2 kSquareViewportSize{1000.f, 1000.f};
constexpr ImVec2 kLandscapeViewportSize{1600.f, 1000.f};

constexpr std::size_t kLongListLength = 60UZ; // more rows than fit in any of the viewports above
constexpr float       kPixelTolerance = 1.f;  // ImGui truncates window positions to whole pixels

struct SheetObservations {
    bool isOpen         = false;
    bool isAnimating    = false;
    bool wasPartlyShown = false; // since the test last reset it
};

struct RadialMenuObservations {
    bool isOpen       = false;
    bool isAnimating  = false;
    bool wasAnimating = false; // since the test last reset it
};

struct TestState {
    int  actionCount          = 0;
    int  longLabelActionCount = 0;
    int  disabledCount        = 0;
    int  nestedActionCount    = 0;
    int  deepestActionCount   = 0;
    int  longListClickCount   = 0;
    bool isToggled            = false;
    bool shouldOpenOtherPopup = false;
    int  sliderValue          = kSliderMin;

    std::size_t comboEntry = 0UZ;

    SheetObservations menu;
    SheetObservations plainSheet;
    SheetObservations modalSheet;
    SheetObservations comboSheet;
    SheetObservations sliderMenu;
    std::string       comboSheetWindowName; // the window of the sheet of the combo box, while it is open
    bool              isOtherPopupOpen = false;

    int                    radialActionCount      = 0;
    int                    radialDisabledCount    = 0;
    int                    radialWithoutIconCount = 0;
    int                    radialNestedCount      = 0;
    int                    radialDeepestCount     = 0;
    bool                   isRadialToggled        = false;
    bool                   shouldCloseRadialMenu  = false;
    int                    fallbackActionCount    = 0;
    RadialMenuObservations radialMenu;
    RadialMenuObservations fallbackMenu;
};

TestState gState;

[[nodiscard]] const std::vector<std::string>& longListLabels() {
    static const std::vector<std::string> labels = [] {
        std::vector<std::string> result;
        for (std::size_t index = 0UZ; index < kLongListLength; ++index) {
            result.push_back(std::format("Long list item {}", index));
        }
        return result;
    }();
    return labels;
}

/// The root page has more rows and a longer label than the "Submenu" page, so the sheet is longer at the root page in
/// both directions a sheet slides in
void drawMenuChoices() {
    if (choiceMenuAction("Action")) {
        ++gState.actionCount;
    }
    if (choiceMenuAction("An action with a label longer than any submenu item")) {
        ++gState.longLabelActionCount;
    }
    std::ignore = choiceMenuToggle("Toggle", &gState.isToggled);
    choiceMenuSeparator();
    {
        imw::Disabled disabled(true);
        if (choiceMenuAction("Disabled")) {
            ++gState.disabledCount;
        }
    }
    if (auto submenu = ChoiceSubmenu("Submenu", ChoiceMenuItemLook{})) {
        if (choiceMenuAction("Nested action")) {
            ++gState.nestedActionCount;
        }
        if (auto deeperSubmenu = ChoiceSubmenu("Deeper", ChoiceMenuItemLook{})) {
            if (choiceMenuAction("Deepest action")) {
                ++gState.deepestActionCount;
            }
        }
    }
    if (auto longList = ChoiceSubmenu("Long list", ChoiceMenuItemLook{})) {
        for (const std::string& label : longListLabels()) {
            if (choiceMenuAction(label.c_str())) {
                ++gState.longListClickCount;
            }
        }
    }
}

[[nodiscard]] ImGuiWindow* popupWindow(ImGuiID popupId) { return ImGui::FindWindowByName(std::format("##Popup_{:08x}", popupId).c_str()); }

[[nodiscard]] float lengthAlongEdgeNormal(ImVec2 size) { return LookAndFeel::instance().sheetEdge() == SheetEdge::Bottom ? size.y : size.x; }

/// how many pixels of the sheet are inside the viewport, in the direction the sheet slides in
[[nodiscard]] float shownLength(const ImGuiWindow& window) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    return lengthAlongEdgeNormal(viewport->Pos + viewport->Size - window.Pos);
}

void observeSheet(SheetObservations& observations, const char* popupName, bool isAnimating) {
    observations.isOpen       = ImGui::IsPopupOpen(popupName);
    observations.isAnimating  = isAnimating;
    const ImGuiWindow* window = popupWindow(ImGui::GetID(popupName));
    if (window == nullptr || !window->Active) {
        return;
    }
    const float shown         = shownLength(*window);
    const bool  isPartlyShown = shown > kPixelTolerance && shown < lengthAlongEdgeNormal(window->Size) - kPixelTolerance;
    observations.wasPartlyShown |= isPartlyShown;
}

constexpr ImGuiWindowFlags kTestWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

void setNextWindowToMainViewport() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
}

void guiFunc(ImGuiTestContext*) {
    LookAndFeel::mutableInstance().choiceMenuStyle = ChoiceMenuStyle::Sheet;

    setNextWindowToMainViewport();
    imw::Window window(kTestWindow, nullptr, kTestWindowFlags);

    if (ImGui::Button("open menu")) {
        ImGui::OpenPopup(kMenu);
    }
    if (ImGui::Button("open slider menu")) {
        ImGui::OpenPopup(kSliderMenu);
    }
    if (ImGui::Button("open plain sheet")) {
        ImGui::OpenPopup(kPlainSheet);
    }
    if (ImGui::Button("open modal sheet")) {
        ImGui::OpenPopup(kModalSheet);
    }
    if (gState.shouldOpenOtherPopup) {
        ImGui::OpenPopup(kOtherPopup);
        gState.shouldOpenOtherPopup = false;
    }

    if (auto menu = ChoiceMenu(kMenu, ChoiceMenuFlags::WantsExclusiveFocus)) {
        drawMenuChoices();
    }
    bool isPlainSheetDonePressed = false;
    if (Sheet sheet(kPlainSheet, PopupFlags::None); sheet) {
        isPlainSheetDonePressed = ImGui::Button("Done");
        if (auto combo = ComboBox(kCombo, kComboEntries[gState.comboEntry], 0)) {
            gState.comboSheetWindowName = ImGui::GetCurrentWindow()->RootWindow->Name;
            for (std::size_t index = 0UZ; index < kComboEntries.size(); ++index) {
                if (selectable(kComboEntries[index], index == gState.comboEntry)) {
                    gState.comboEntry = index;
                }
            }
        }
        imw::OverrideId comboScope(ImGui::GetID(kCombo));
        observeSheet(gState.comboSheet, kComboSheet, gr::od::detail::isPopupSheetAnimating(kComboSheet));
    }
    if (isPlainSheetDonePressed) {
        closePopup(kPlainSheet);
    }
    if (imw::Popup otherPopup(kOtherPopup, ImGuiWindowFlags_None); otherPopup) {
        ImGui::TextUnformatted("another popup");
    }

    observeSheet(gState.menu, kMenu, gr::od::detail::isChoiceMenuAnimating(kMenu));
    observeSheet(gState.plainSheet, kPlainSheet, gr::od::detail::isPopupSheetAnimating(kPlainSheet));
    if (auto modalSheet = Popup(kModalSheet, PopupOptions{.flags = PopupFlags::Modal})) {
        if (ImGui::Button("Close from inside")) {
            closeCurrentPopup();
        }
    }
    observeSheet(gState.modalSheet, kModalSheet, gr::od::detail::isPopupAnimating(kModalSheet));
    if (auto sliderMenu = Popup(kSliderMenu, PopupOptions{})) {
        if (auto sliderPage = Menu("Slider page", nullptr)) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            std::ignore = sliderScalar("##slider", {.dataType = ImGuiDataType_S32, .data = &gState.sliderValue, .min = &kSliderMin, .max = &kSliderMax});
        }
    }
    observeSheet(gState.sliderMenu, kSliderMenu, gr::od::detail::isPopupAnimating(kSliderMenu));
    gState.isOtherPopupOpen = ImGui::IsPopupOpen(kOtherPopup);
}

void observeRadialMenu(RadialMenuObservations& observations, const char* popupName) {
    observations.isOpen      = ImGui::IsPopupOpen(popupName);
    observations.isAnimating = gr::od::detail::isChoiceMenuAnimating(popupName);
    observations.wasAnimating |= observations.isAnimating;
}

void radialGuiFunc(ImGuiTestContext*) {
    LookAndFeel::mutableInstance().choiceMenuStyle = ChoiceMenuStyle::Radial;

    setNextWindowToMainViewport();
    imw::Window window(kTestWindow, nullptr, kTestWindowFlags);

    if (ImGui::Button("open radial menu")) {
        ImGui::OpenPopup(kRadialMenu);
    }
    if (ImGui::Button("open fallback menu")) {
        ImGui::OpenPopup(kFallbackMenu);
    }
    if (gState.shouldCloseRadialMenu) {
        closePopup(kRadialMenu);
        gState.shouldCloseRadialMenu = false;
    }

    if (auto menu = ChoiceMenu(kRadialMenu, ChoiceMenuFlags::WantsExclusiveFocus | ChoiceMenuFlags::SupportsRadialMenu)) {
        if (choiceMenuAction(kRadialChoices[0], {.icon = "\u{f00c}"})) { // check
            ++gState.radialActionCount;
        }
        std::ignore = choiceMenuToggle(kRadialChoices[1], &gState.isRadialToggled, {.icon = "\u{f205}"}); // toggle-on
        if (auto submenu = ChoiceSubmenu(kRadialSubmenu, ChoiceMenuItemLook{.icon = "\u{f141}"})) {       // ellipsis
            if (choiceMenuAction(kRadialSubmenuItems[0], {.icon = "\u{f00c}"})) {                         // check
                ++gState.radialNestedCount;
            }
            if (auto deeperSubmenu = ChoiceSubmenu(kRadialDeeperSubmenu, ChoiceMenuItemLook{.icon = "\u{f141}"})) { // ellipsis
                if (choiceMenuAction(kRadialDeepestAction, {.icon = "\u{f00c}"})) {                                 // check
                    ++gState.radialDeepestCount;
                }
            }
            if (choiceMenuAction(kRadialSubmenuItems[2], {.icon = "\u{f00c}"})) { // check
                ++gState.radialNestedCount;
            }
        }
        choiceMenuSeparator();
        {
            imw::Disabled disabled(true);
            if (choiceMenuAction(kRadialChoices[2], {.icon = "\u{f05e}"})) { // ban
                ++gState.radialDisabledCount;
            }
        }
        if (choiceMenuAction(kRadialChoices[3])) {
            ++gState.radialWithoutIconCount;
        }
    }
    observeRadialMenu(gState.radialMenu, kRadialMenu);

    if (auto menu = ChoiceMenu(kFallbackMenu, ChoiceMenuFlags::WantsExclusiveFocus)) {
        for (const char* label : kFallbackChoices) {
            if (choiceMenuAction(label)) {
                ++gState.fallbackActionCount;
            }
        }
    }
    observeRadialMenu(gState.fallbackMenu, kFallbackMenu);
}

[[nodiscard]] std::string testWindowRef() { return std::format("//{}", kTestWindow); }

[[nodiscard]] ImGuiID popupId(ImGuiTestContext* ctx, const char* popupName) { return ctx->GetID(std::format("//{}/{}", kTestWindow, popupName).c_str()); }

[[nodiscard]] std::string popupRef(ImGuiTestContext* ctx, const char* popupName) { return std::format("//##Popup_{:08x}", popupId(ctx, popupName)); }

[[nodiscard]] ImGuiWindow& menuSheetWindow(ImGuiTestContext* ctx) {
    ImGuiWindow* window = popupWindow(popupId(ctx, kMenu));
    expect(window != nullptr) << "the menu sheet exists" << fatal;
    return *window;
}

/// an item in a page of the menu, which is drawn into a child window of the sheet
[[nodiscard]] std::string menuItemRef(std::string_view label) { return std::format("**/{}", label); }

[[nodiscard]] ImRect windowRect(const ImGuiWindow& window) { return ImRect(window.Pos, window.Pos + window.Size); }

[[nodiscard]] bool isNear(float value, float expected) { return std::abs(value - expected) <= kPixelTolerance; }

/// whether the window spans the whole edge of the viewport and touches it
[[nodiscard]] bool isAttachedToEdge(const ImGuiWindow& window, SheetEdge edge) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2         max      = viewport->Pos + viewport->Size;
    if (edge == SheetEdge::Bottom) {
        return isNear(window.Pos.x, viewport->Pos.x) && isNear(window.Size.x, viewport->Size.x) && isNear(window.Pos.y + window.Size.y, max.y);
    }
    return isNear(window.Pos.y, viewport->Pos.y) && isNear(window.Size.y, viewport->Size.y) && isNear(window.Pos.x + window.Size.x, max.x);
}

[[nodiscard]] float maxSheetLength() { return LookAndFeel::instance().sheetMaxLengthFractionOfDisplay * lengthAlongEdgeNormal(ImGui::GetMainViewport()->Size); }

/// a drag distance, as a fraction of the dragged length, that dismisses a sheet or a page
[[nodiscard]] float fractionPastDismissThreshold() {
    const float threshold = LookAndFeel::instance().sheetDismissThresholdFraction;
    return threshold + .5f * (1.f - threshold);
}

/// a drag distance, as a fraction of the dragged length, that does not dismiss a sheet or a page
[[nodiscard]] float fractionShortOfDismissThreshold() { return .5f * LookAndFeel::instance().sheetDismissThresholdFraction; }

void waitUntilMenuSettled(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    waitUntil(ctx, "the menu sheet is open and does not move", [] { return gState.menu.isOpen && !gState.menu.isAnimating; }, location);
}

void waitUntilMenuClosed(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    waitUntil(ctx, "the menu sheet is closed", [] { return !gState.menu.isOpen; }, location);
}

/// opens the menu, waits until it slid in, and makes the menu sheet the reference for item lookups
void openMenu(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    ctx->SetRef(testWindowRef().c_str());
    ctx->ItemClick("open menu");
    waitUntilMenuSettled(ctx, location);
    ctx->SetRef(popupRef(ctx, kMenu).c_str());
}

[[nodiscard]] ImGuiWindow& plainSheetWindow(ImGuiTestContext* ctx) {
    ImGuiWindow* window = popupWindow(popupId(ctx, kPlainSheet));
    expect(window != nullptr) << "the plain sheet exists" << fatal;
    return *window;
}

/// opens the plain sheet, waits until it slid in, and makes the plain sheet the reference for item lookups
void openPlainSheet(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    ctx->SetRef(testWindowRef().c_str());
    ctx->ItemClick("open plain sheet");
    waitUntil(ctx, "the plain sheet is open and does not move", [] { return gState.plainSheet.isOpen && !gState.plainSheet.isAnimating; }, location);
    ctx->SetRef(popupRef(ctx, kPlainSheet).c_str());
}

[[nodiscard]] ImGuiWindow& modalSheetWindow(ImGuiTestContext* ctx) {
    ImGuiWindow* window = popupWindow(popupId(ctx, kModalSheet));
    expect(window != nullptr) << "the modal sheet exists" << fatal;
    return *window;
}

/// opens the modal sheet, waits until it slid in, and makes the modal sheet the reference for item lookups
void openModalSheet(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    ctx->SetRef(testWindowRef().c_str());
    ctx->ItemClick("open modal sheet");
    waitUntil(ctx, "the modal sheet is open and does not move", [] { return gState.modalSheet.isOpen && !gState.modalSheet.isAnimating; }, location);
    ctx->SetRef(popupRef(ctx, kModalSheet).c_str());
}

void closeModalSheet(ImGuiTestContext* ctx) {
    ctx->SetRef(popupRef(ctx, kModalSheet).c_str());
    ctx->ItemClick(gr::od::detail::kSheetCloseButtonLabel);
    waitUntil(ctx, "the modal sheet is closed", [] { return !gState.modalSheet.isOpen; });
}

void closePlainSheet(ImGuiTestContext* ctx) {
    ctx->SetRef(popupRef(ctx, kPlainSheet).c_str());
    ctx->ItemClick(gr::od::detail::kSheetCloseButtonLabel);
    waitUntil(ctx, "the plain sheet is closed", [] { return !gState.plainSheet.isOpen; });
}

void closeMenuWithCloseButton(ImGuiTestContext* ctx) {
    ctx->SetRef(popupRef(ctx, kMenu).c_str());
    ctx->ItemClick(gr::od::detail::kSheetCloseButtonLabel);
    waitUntilMenuClosed(ctx);
}

/// whether the menu sheet does not move and shows the page with the row `label`. A sheet that does not move draws only
/// its top page
[[nodiscard]] bool isPageWithRowShown(ImGuiTestContext* ctx, std::string_view label) { return !gState.menu.isAnimating && ctx->ItemExists(menuItemRef(label).c_str()); }

void waitUntilPageWithRowShown(ImGuiTestContext* ctx, std::string_view label, std::source_location location = std::source_location::current()) {
    waitUntil(ctx, std::format("the page with the row \"{}\" is shown", label), [ctx, label] { return isPageWithRowShown(ctx, label); }, location);
}

void openSubmenu(ImGuiTestContext* ctx, std::string_view label, std::string_view rowOnSubmenuPage) {
    ctx->ItemClick(menuItemRef(label).c_str());
    waitUntilPageWithRowShown(ctx, rowOnSubmenuPage);
}

void dragFromTo(ImGuiTestContext* ctx, ImVec2 from, ImVec2 to) {
    ctx->MouseMoveToPos(from);
    ctx->MouseDown(ImGuiMouseButton_Left);
    ctx->MouseMoveToPos(to);
    ctx->MouseUp(ImGuiMouseButton_Left);
}

/// drags the header of the menu sheet toward the edge of the viewport the sheet is attached to
void dragMenuSheetHeader(ImGuiTestContext* ctx, float fractionOfSheetLength) {
    const ImRect       header = ctx->ItemInfo(ctx->GetID("##sheetHeader", popupId(ctx, kMenu))).RectFull;
    const ImGuiWindow& sheet  = menuSheetWindow(ctx);
    const float        length = fractionOfSheetLength * lengthAlongEdgeNormal(sheet.Size);
    const ImVec2       start  = header.GetCenter();
    dragFromTo(ctx, start, start + (LookAndFeel::instance().sheetEdge() == SheetEdge::Bottom ? ImVec2{0.f, length} : ImVec2{length, 0.f}));
}

/// presses on the row `label` of the shown page and drags to the right
void swipePageRight(ImGuiTestContext* ctx, std::string_view label, float fractionOfPageWidth) {
    const ImGuiTestItemInfo row       = ctx->ItemInfo(menuItemRef(label).c_str());
    const float             pageWidth = row.Window->Size.x;
    const ImVec2            start{row.RectFull.Min.x + ImGui::GetFrameHeight(), row.RectFull.GetCenter().y};
    dragFromTo(ctx, start, start + ImVec2{fractionOfPageWidth * pageWidth, 0.f});
}

/// drags the middle of the header of the modal sheet, which has no handle, toward the edge of the viewport the sheet is
/// attached to
void dragModalSheetHeader(ImGuiTestContext* ctx, float fractionOfSheetLength) {
    const ImGuiWindow& sheet           = modalSheetWindow(ctx);
    const ImVec2       padding         = ImGui::GetStyle().WindowPadding;
    const bool         isBottom        = LookAndFeel::instance().sheetEdge() == SheetEdge::Bottom;
    const float        headerThickness = ImGui::GetFrameHeight() + 2.f * (isBottom ? padding.y : padding.x);
    const float        length          = fractionOfSheetLength * lengthAlongEdgeNormal(sheet.Size);
    const ImVec2       start           = isBottom ? ImVec2{sheet.Pos.x + .5f * sheet.Size.x, sheet.Pos.y + .5f * headerThickness} : ImVec2{sheet.Pos.x + .5f * headerThickness, sheet.Pos.y + .5f * sheet.Size.y};
    dragFromTo(ctx, start, start + (isBottom ? ImVec2{0.f, length} : ImVec2{length, 0.f}));
}

/// clicks the middle of the part of the viewport that no sheet covers, as a sheet covers at most
/// LookAndFeel::sheetMaxLengthFractionOfDisplay of the viewport. The buttons of the test window are in its top left corner
void clickOutsideOfSheets(ImGuiTestContext* ctx) {
    const ImGuiViewport* viewport           = ImGui::GetMainViewport();
    const float          uncoveredFraction  = 1.f - LookAndFeel::instance().sheetMaxLengthFractionOfDisplay;
    const ImVec2         fractionOfViewport = LookAndFeel::instance().sheetEdge() == SheetEdge::Bottom ? ImVec2{.5f, .5f * uncoveredFraction} : ImVec2{.5f * uncoveredFraction, .5f};
    ctx->MouseMoveToPos(viewport->Pos + viewport->Size * fractionOfViewport);
    ctx->MouseClick(ImGuiMouseButton_Left);
}

[[nodiscard]] std::string edgeName(SheetEdge edge) { return edge == SheetEdge::Bottom ? "bottom" : "right"; }

void testEdgeSelection(ImGuiTestContext* ctx) {
    struct DisplayCase {
        const char* name;
        ImVec2      viewportSize;
        SheetEdge   expectedEdge;
    };
    constexpr std::array displayCases{
        DisplayCase{.name = "portrait", .viewportSize = kPortraitViewportSize, .expectedEdge = SheetEdge::Bottom},
        DisplayCase{.name = "square", .viewportSize = kSquareViewportSize, .expectedEdge = SheetEdge::Bottom},
        DisplayCase{.name = "landscape", .viewportSize = kLandscapeViewportSize, .expectedEdge = SheetEdge::Right},
    };
    for (const DisplayCase& displayCase : displayCases) {
        boost::ut::test(std::format("a {} display gets sheets attached to the {} edge", displayCase.name, edgeName(displayCase.expectedEdge))) = [&] {
            resizeViewport(ctx, displayCase.viewportSize);
            expect(LookAndFeel::instance().sheetEdge() == displayCase.expectedEdge);
            openMenu(ctx);
            expect(isAttachedToEdge(menuSheetWindow(ctx), displayCase.expectedEdge));
            closeMenuWithCloseButton(ctx);
        };
    }

    boost::ut::test("an open sheet moves to the other edge when the display changes orientation") = [&] {
        resizeViewport(ctx, kPortraitViewportSize);
        openMenu(ctx);
        expect(isAttachedToEdge(menuSheetWindow(ctx), SheetEdge::Bottom));
        resizeViewport(ctx, kLandscapeViewportSize);
        waitUntil(ctx, "the sheet is attached to the right edge", [ctx] { return isAttachedToEdge(menuSheetWindow(ctx), SheetEdge::Right) && !gState.menu.isAnimating; });
        closeMenuWithCloseButton(ctx);
    };
}

void testSheet(ImGuiTestContext* ctx) { // NOSONAR test function length
    const SheetEdge edge  = LookAndFeel::instance().sheetEdge();
    const auto      named = [edge](std::string_view description) { return std::format("[{} sheet] {}", edgeName(edge), description); };

    boost::ut::test(named("opening slides the sheet in from its edge")) = [&] {
        gState.menu.wasPartlyShown = false;
        openMenu(ctx);
        expect(gState.menu.wasPartlyShown) << "the sheet was partly shown while it opened";
        expect(isAttachedToEdge(menuSheetWindow(ctx), edge));
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("a sheet with little content is just long enough to show all of it")) = [&] {
        openMenu(ctx);
        const ImGuiWindow& sheet = menuSheetWindow(ctx);
        expect(lengthAlongEdgeNormal(sheet.Size) < maxSheetLength());
        for (std::string_view label : {"Action", "An action with a label longer than any submenu item", "Toggle", "Disabled", "Submenu", "Long list"}) {
            expect(windowRect(sheet).Contains(ctx->ItemInfo(menuItemRef(label).c_str()).RectFull)) << label << " is inside the sheet";
        }
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("the close button slides the sheet out and closes it")) = [&] {
        openMenu(ctx);
        gState.menu.wasPartlyShown = false;
        ctx->ItemClick(gr::od::detail::kSheetCloseButtonLabel);
        waitUntilMenuClosed(ctx);
        expect(gState.menu.wasPartlyShown) << "the sheet was partly shown while it closed";
    };

    boost::ut::test(named("tapping outside of the sheet slides it out and closes it")) = [&] {
        openMenu(ctx);
        gState.menu.wasPartlyShown = false;
        clickOutsideOfSheets(ctx);
        waitUntilMenuClosed(ctx);
        expect(gState.menu.wasPartlyShown) << "the sheet was partly shown while it closed";
    };

    boost::ut::test(named("dragging the header past the threshold closes the sheet")) = [&] {
        openMenu(ctx);
        dragMenuSheetHeader(ctx, fractionPastDismissThreshold());
        waitUntilMenuClosed(ctx);
    };

    boost::ut::test(named("dragging the header short of the threshold slides the sheet back")) = [&] {
        openMenu(ctx);
        dragMenuSheetHeader(ctx, fractionShortOfDismissThreshold());
        waitUntilMenuSettled(ctx);
        expect(isAttachedToEdge(menuSheetWindow(ctx), edge));
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("dragging the header of a modal sheet past the threshold does not move or close it")) = [&] {
        openModalSheet(ctx);
        gState.modalSheet.wasPartlyShown = false;
        dragModalSheetHeader(ctx, fractionPastDismissThreshold());
        expect(gState.modalSheet.isOpen && !gState.modalSheet.isAnimating) << "the modal sheet is not closing";
        expect(!gState.modalSheet.wasPartlyShown) << "the modal sheet did not move while it was dragged";
        expect(isAttachedToEdge(modalSheetWindow(ctx), edge));
        closeModalSheet(ctx);
    };

    boost::ut::test(named("tapping outside of a modal sheet does not close it")) = [&] {
        openModalSheet(ctx);
        gState.modalSheet.wasPartlyShown = false;
        clickOutsideOfSheets(ctx);
        expect(gState.modalSheet.isOpen && !gState.modalSheet.isAnimating) << "the modal sheet is not closing";
        expect(!gState.modalSheet.wasPartlyShown);
        closeModalSheet(ctx);
    };

    boost::ut::test(named("closeCurrentPopup() inside of a sheet closes it with the closing animation")) = [&] {
        openModalSheet(ctx);
        gState.modalSheet.wasPartlyShown = false;
        ctx->ItemClick(menuItemRef("Close from inside").c_str());
        waitUntil(ctx, "the modal sheet is closed", [] { return !gState.modalSheet.isOpen; });
        expect(gState.modalSheet.wasPartlyShown) << "the sheet was partly shown while it closed";
    };

    boost::ut::test(named("CloseSheet() closes the sheet with the closing animation")) = [&] {
        openPlainSheet(ctx);
        gState.plainSheet.wasPartlyShown = false;
        ctx->ItemClick(menuItemRef("Done").c_str());
        waitUntil(ctx, "the plain sheet is closed", [] { return !gState.plainSheet.isOpen; });
        expect(gState.plainSheet.wasPartlyShown) << "the sheet was partly shown while it closed";
    };

    boost::ut::test(named("a sheet is just long enough to show its contents")) = [&] {
        openPlainSheet(ctx);
        const ImGuiWindow& sheet = plainSheetWindow(ctx);
        expect(lengthAlongEdgeNormal(sheet.Size) < maxSheetLength());
        for (const char* label : {"Done", kCombo}) {
            expect(windowRect(sheet).Contains(ctx->ItemInfo(menuItemRef(label).c_str()).RectFull)) << label << " is inside the sheet";
        }
        closePlainSheet(ctx);
    };

    boost::ut::test(named("a combo box in a sheet opens a second sheet, and picking an entry closes only that sheet")) = [&] {
        openPlainSheet(ctx);
        const std::size_t pickedEntry = (gState.comboEntry + 1UZ) % kComboEntries.size();
        ctx->ItemClick(menuItemRef(kCombo).c_str());
        waitUntil(ctx, "the sheet of the combo box is open and does not move", [] { return gState.comboSheet.isOpen && !gState.comboSheet.isAnimating; });
        ctx->SetRef(std::format("//{}", gState.comboSheetWindowName).c_str());
        gState.comboSheet.wasPartlyShown = false;
        ctx->ItemClick(menuItemRef(kComboEntries[pickedEntry]).c_str());
        waitUntil(ctx, "the sheet of the combo box is closed", [] { return !gState.comboSheet.isOpen; });
        expect(eq(gState.comboEntry, pickedEntry));
        expect(gState.comboSheet.wasPartlyShown) << "the sheet of the combo box was partly shown while it closed";
        expect(gState.plainSheet.isOpen) << "the sheet with the combo box stays open";
        closePlainSheet(ctx);
    };

    boost::ut::test(named("another popup opened at the sheet's level replaces the sheet")) = [&] {
        openMenu(ctx);
        gState.shouldOpenOtherPopup = true;
        waitUntilMenuClosed(ctx);
        expect(gState.isOtherPopupOpen) << "closing the sheet did not close the other popup";
        ctx->PopupCloseAll();
        waitUntil(ctx, "the other popup is closed", [] { return !gState.isOtherPopupOpen; });
    };
}

void testChoiceMenu(ImGuiTestContext* ctx) { // NOSONAR test function length
    const SheetEdge edge  = LookAndFeel::instance().sheetEdge();
    const auto      named = [edge](std::string_view description) { return std::format("[{} sheet] {}", edgeName(edge), description); };

    boost::ut::test(named("an action is called and closes the menu")) = [&] {
        openMenu(ctx);
        const int previousCount    = gState.actionCount;
        gState.menu.wasPartlyShown = false;
        ctx->ItemClick(menuItemRef("Action").c_str());
        waitUntilMenuClosed(ctx);
        expect(eq(gState.actionCount, previousCount + 1));
        expect(gState.menu.wasPartlyShown) << "the sheet was partly shown while it closed";
    };

    boost::ut::test(named("a toggle flips and keeps the menu open")) = [&] {
        openMenu(ctx);
        const bool wasToggled = gState.isToggled;
        ctx->ItemClick(menuItemRef("Toggle").c_str());
        expect(gState.isToggled != wasToggled);
        ctx->ItemClick(menuItemRef("Toggle").c_str());
        expect(gState.isToggled == wasToggled);
        expect(gState.menu.isOpen);
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("a disabled action cannot be called")) = [&] {
        openMenu(ctx);
        ctx->ItemClick(menuItemRef("Disabled").c_str());
        expect(eq(gState.disabledCount, 0));
        expect(gState.menu.isOpen);
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("a submenu opens as a page, and the sheet fits that page")) = [&] {
        openMenu(ctx);
        const float rootPageSheetLength = lengthAlongEdgeNormal(menuSheetWindow(ctx).Size);
        openSubmenu(ctx, "Submenu", "Nested action");
        const ImGuiWindow& sheet = menuSheetWindow(ctx);
        expect(lengthAlongEdgeNormal(sheet.Size) < rootPageSheetLength) << "the submenu page is shorter than the root page";
        for (std::string_view label : {"Nested action", "Deeper"}) {
            expect(windowRect(sheet).Contains(ctx->ItemInfo(menuItemRef(label).c_str()).RectFull)) << label << " is inside the sheet";
        }
        closeMenuWithCloseButton(ctx);
    };

    if (edge == SheetEdge::Right) { // the pages of the menu are all longer than the minimum length of a bottom sheet
        boost::ut::test(named("a page narrower than LookAndFeel::sheetMinContentSizeEms gets a sheet of that width")) = [&] {
            openMenu(ctx);
            openSubmenu(ctx, "Submenu", "Nested action");
            const ImVec2       padding          = ImGui::GetStyle().WindowPadding;
            const float        headerWidth      = ImGui::GetFrameHeight() + 2.f * padding.x;
            const float        minContentWidth  = LookAndFeel::instance().sheetMinContentSizeEms.x * ImGui::GetFontSize();
            const ImGuiWindow& page             = *ctx->ItemInfo(menuItemRef("Nested action").c_str()).Window;
            const float        pageContentWidth = page.ContentSizeIdeal.x + 2.f * padding.x;
            expect(pageContentWidth < minContentWidth) << "the page is narrower than the minimum width, which the test relies on";
            expect(menuSheetWindow(ctx).Size.x >= headerWidth + minContentWidth - kPixelTolerance);
            closeMenuWithCloseButton(ctx);
        };
    }

    boost::ut::test(named("an action in a submenu is called and closes the menu")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        const int previousCount = gState.nestedActionCount;
        ctx->ItemClick(menuItemRef("Nested action").c_str());
        waitUntilMenuClosed(ctx);
        expect(eq(gState.nestedActionCount, previousCount + 1));
    };

    boost::ut::test(named("an action two submenus deep is called and closes the menu")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        openSubmenu(ctx, "Deeper", "Deepest action");
        const int previousCount = gState.deepestActionCount;
        ctx->ItemClick(menuItemRef("Deepest action").c_str());
        waitUntilMenuClosed(ctx);
        expect(eq(gState.deepestActionCount, previousCount + 1));
    };

    boost::ut::test(named("the back button returns to the parent page, one page at a time")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        openSubmenu(ctx, "Deeper", "Deepest action");
        ctx->ItemClick(gr::od::detail::kSheetBackButtonLabel);
        waitUntilPageWithRowShown(ctx, "Nested action");
        ctx->ItemClick(gr::od::detail::kSheetBackButtonLabel);
        waitUntilPageWithRowShown(ctx, "Action");
        expect(gState.menu.isOpen);
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("swiping a page right past the threshold returns to the parent page without calling the swiped item")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        const int previousCount = gState.nestedActionCount;
        swipePageRight(ctx, "Nested action", fractionPastDismissThreshold());
        waitUntilPageWithRowShown(ctx, "Action");
        expect(eq(gState.nestedActionCount, previousCount));
        expect(gState.menu.isOpen);
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("swiping a page right short of the threshold slides it back without calling the swiped item")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        const int previousCount = gState.nestedActionCount;
        swipePageRight(ctx, "Nested action", fractionShortOfDismissThreshold());
        waitUntilMenuSettled(ctx);
        expect(isPageWithRowShown(ctx, "Nested action"));
        expect(eq(gState.nestedActionCount, previousCount));
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("swiping the root page right does not close the menu or call the swiped item")) = [&] {
        openMenu(ctx);
        const int previousCount = gState.actionCount;
        swipePageRight(ctx, "Action", fractionPastDismissThreshold());
        waitUntilMenuSettled(ctx);
        expect(isPageWithRowShown(ctx, "Action"));
        expect(eq(gState.actionCount, previousCount));
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("a page longer than the sheet scrolls with a vertical drag, which does not call the dragged item")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Long list", longListLabels()[0]);
        if (edge == SheetEdge::Bottom) {
            expect(isNear(lengthAlongEdgeNormal(menuSheetWindow(ctx).Size), maxSheetLength())) << "the sheet has its maximum length";
        }
        const ImGuiTestItemInfo row            = ctx->ItemInfo(menuItemRef(longListLabels()[5]).c_str()); // with rows above it, so the drag starts and ends on the page
        ImGuiWindow* const      page           = row.Window;
        const float             scrollBefore   = page->Scroll.y;
        const float             dragDistance   = 3.f * row.RectFull.GetHeight();
        const int               previousClicks = gState.longListClickCount;
        dragFromTo(ctx, row.RectFull.GetCenter(), row.RectFull.GetCenter() - ImVec2{0.f, dragDistance});
        waitUntil(ctx, "the page scrolled", [page, scrollBefore] { return page->Scroll.y != scrollBefore; });
        expect(isNear(page->Scroll.y - scrollBefore, dragDistance)) << "the page moved as far as the pointer";
        expect(eq(gState.longListClickCount, previousClicks));
        closeMenuWithCloseButton(ctx);
    };

    boost::ut::test(named("dragging a slider on a page to the right moves the slider instead of swiping the page back")) = [&] {
        gState.sliderValue = kSliderMin;
        ctx->SetRef(testWindowRef().c_str());
        ctx->ItemClick("open slider menu");
        waitUntil(ctx, "the slider menu is open and does not move", [] { return gState.sliderMenu.isOpen && !gState.sliderMenu.isAnimating; });
        ctx->SetRef(popupRef(ctx, kSliderMenu).c_str());
        ctx->ItemClick(menuItemRef("Slider page").c_str());
        const auto isSliderPageShown = [ctx] { return !gState.sliderMenu.isAnimating && ctx->ItemExists(menuItemRef("##slider").c_str()); };
        waitUntil(ctx, "the slider page is shown", isSliderPageShown);

        const ImGuiTestItemInfo slider    = ctx->ItemInfo(menuItemRef("##slider").c_str());
        const float             pageWidth = slider.Window->Size.x;
        const ImVec2            start{slider.RectFull.Min.x + ImGui::GetStyle().FramePadding.x, slider.RectFull.GetCenter().y};
        dragFromTo(ctx, start, start + ImVec2{fractionPastDismissThreshold() * pageWidth, 0.f});
        waitUntil(ctx, "the slider menu does not move", [] { return !gState.sliderMenu.isAnimating; });
        expect(gState.sliderValue > kSliderMin) << "the slider moved";
        expect(isSliderPageShown()) << "the page was not swiped back";

        ctx->ItemClick(gr::od::detail::kSheetCloseButtonLabel);
        waitUntil(ctx, "the slider menu is closed", [] { return !gState.sliderMenu.isOpen; });
    };

    boost::ut::test(named("opening the menu again shows the root page")) = [&] {
        openMenu(ctx);
        openSubmenu(ctx, "Submenu", "Nested action");
        closeMenuWithCloseButton(ctx);
        openMenu(ctx);
        expect(isPageWithRowShown(ctx, "Action"));
        closeMenuWithCloseButton(ctx);
    };
}

void testSheetAndChoiceMenuInViewport(ImGuiTestContext* ctx, ImVec2 viewportSize) {
    resizeViewport(ctx, viewportSize);
    testSheet(ctx);
    testChoiceMenu(ctx);
}

[[nodiscard]] std::vector<ImVec2> choiceCentres(ImGuiTestContext* ctx, std::span<const char* const> labels) {
    std::vector<ImVec2> centres;
    for (const char* label : labels) {
        centres.push_back(ctx->ItemInfo(menuItemRef(label).c_str()).RectFull.GetCenter());
    }
    return centres;
}

/// For points evenly spaced on a circle, their mean is the centre of the circle
[[nodiscard]] ImVec2 meanOf(std::span<const ImVec2> points) {
    ImVec2 sum{0.f, 0.f};
    for (const ImVec2& point : points) {
        sum += point;
    }
    return sum / static_cast<float>(points.size());
}

[[nodiscard]] float distanceBetween(ImVec2 first, ImVec2 second) { return std::hypot(first.x - second.x, first.y - second.y); }

[[nodiscard]] bool areEquallyFarFromTheirMean(std::span<const ImVec2> points) {
    const ImVec2 mean          = meanOf(points);
    const float  firstDistance = distanceBetween(points.front(), mean);
    return firstDistance > kPixelTolerance && std::ranges::all_of(points, [mean, firstDistance](ImVec2 point) { return isNear(distanceBetween(point, mean), firstDistance); });
}

/// opens the radial menu, waits until it faded in, and makes it the reference for item lookups
void openRadialMenu(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    ctx->SetRef(testWindowRef().c_str());
    ctx->ItemClick("open radial menu");
    waitUntil(ctx, "the radial menu is open and faded in", [] { return gState.radialMenu.isOpen && !gState.radialMenu.isAnimating; }, location);
    ctx->SetRef(popupRef(ctx, kRadialMenu).c_str());
}

void waitUntilRadialMenuSettled(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    waitUntil(ctx, "the radial menu is open and does not move", [] { return gState.radialMenu.isOpen && !gState.radialMenu.isAnimating; }, location);
}

void openRadialSubmenu(ImGuiTestContext* ctx, const char* label, std::source_location location = std::source_location::current()) {
    ctx->ItemClick(menuItemRef(label).c_str());
    waitUntilRadialMenuSettled(ctx, location);
}

void waitUntilRadialMenuClosed(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
    waitUntil(ctx, "the radial menu is closed", [] { return !gState.radialMenu.isOpen; }, location);
}

void closeRadialMenu(ImGuiTestContext* ctx) {
    gState.shouldCloseRadialMenu = true;
    waitUntilRadialMenuClosed(ctx);
}

void testRadialMenu(ImGuiTestContext* ctx) { // NOSONAR test function length
    resizeViewport(ctx, kSquareViewportSize);

    boost::ut::test("[radial] a radial menu fades in") = [&] {
        gState.radialMenu.wasAnimating = false;
        openRadialMenu(ctx);
        expect(gState.radialMenu.wasAnimating) << "the radial menu was animating while it opened";
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] the choices are on a circle inside of the viewport when the menu is opened near its corner") = [&] {
        openRadialMenu(ctx); // the button that opens the menu is in the top left corner of the viewport
        expect(areEquallyFarFromTheirMean(choiceCentres(ctx, kRadialItems)));
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        for (const char* label : kRadialItems) {
            expect(ImRect(viewport->Pos, viewport->Pos + viewport->Size).Contains(ctx->ItemInfo(menuItemRef(label).c_str()).RectFull)) << label << " is inside of the viewport";
        }
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] closePopup() closes the menu") = [&] {
        openRadialMenu(ctx);
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] an action is called and closes the menu") = [&] {
        openRadialMenu(ctx);
        const int previousCount = gState.radialActionCount;
        ctx->ItemClick(menuItemRef(kRadialChoices[0]).c_str());
        waitUntilRadialMenuClosed(ctx);
        expect(eq(gState.radialActionCount, previousCount + 1));
    };

    boost::ut::test("[radial] an action without an icon is called and closes the menu") = [&] {
        openRadialMenu(ctx);
        const int previousCount = gState.radialWithoutIconCount;
        ctx->ItemClick(menuItemRef(kRadialChoices[3]).c_str());
        waitUntilRadialMenuClosed(ctx);
        expect(eq(gState.radialWithoutIconCount, previousCount + 1));
    };

    boost::ut::test("[radial] a toggle flips and keeps the menu open") = [&] {
        openRadialMenu(ctx);
        const bool wasToggled = gState.isRadialToggled;
        ctx->ItemClick(menuItemRef(kRadialChoices[1]).c_str());
        expect(gState.isRadialToggled != wasToggled);
        ctx->ItemClick(menuItemRef(kRadialChoices[1]).c_str());
        expect(gState.isRadialToggled == wasToggled);
        expect(gState.radialMenu.isOpen);
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] a disabled action cannot be called and keeps the menu open") = [&] {
        openRadialMenu(ctx);
        ctx->ItemClick(menuItemRef(kRadialChoices[2]).c_str());
        expect(eq(gState.radialDisabledCount, 0));
        expect(gState.radialMenu.isOpen);
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] a click between the choices closes the menu without calling a choice") = [&] {
        openRadialMenu(ctx);
        const int previousActionCount      = gState.radialActionCount;
        const int previousWithoutIconCount = gState.radialWithoutIconCount;
        ctx->MouseMoveToPos(meanOf(choiceCentres(ctx, kRadialItems)));
        ctx->MouseClick(ImGuiMouseButton_Left);
        waitUntilRadialMenuClosed(ctx);
        expect(eq(gState.radialActionCount, previousActionCount));
        expect(eq(gState.radialWithoutIconCount, previousWithoutIconCount));
    };

    boost::ut::test("[radial] the menu closes when the mouse leaves it") = [&] {
        openRadialMenu(ctx); // in the top left corner of the viewport
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ctx->MouseMoveToPos(viewport->Pos + viewport->Size * .75f);
        waitUntilRadialMenuClosed(ctx);
    };

    boost::ut::test("[radial] the choices of a submenu are clustered on spokes pointing away from the menu, and do not overlap with its items while not hovered") = [&] {
        openRadialMenu(ctx);
        openRadialSubmenu(ctx, kRadialSubmenu);
        const ImVec2              menuCenter       = meanOf(choiceCentres(ctx, kRadialItems));
        const ImVec2              submenuCenter    = ctx->ItemInfo(menuItemRef(kRadialSubmenu).c_str()).RectFull.GetCenter();
        const ImVec2              awayFromMenu     = submenuCenter - menuCenter;
        const std::vector<ImVec2> submenuCentres   = choiceCentres(ctx, kRadialSubmenuItems);
        const float               firstSpokeLength = distanceBetween(submenuCentres.front(), submenuCenter);
        // the hovered submenu item is larger than the others, which are as large as its choices
        const float unhoveredDiameter = ctx->ItemInfo(menuItemRef(kRadialChoices[0]).c_str()).RectFull.GetWidth();
        for (std::size_t index = 0UZ; index < kRadialSubmenuItems.size(); ++index) {
            const ImVec2 spoke = submenuCentres[index] - submenuCenter;
            expect(isNear(distanceBetween(submenuCentres[index], submenuCenter), firstSpokeLength)) << kRadialSubmenuItems[index] << " is as far from the submenu as the first choice of the submenu";
            expect(ImDot(spoke, awayFromMenu) > 0.f) << kRadialSubmenuItems[index] << " is on a spoke pointing away from the menu";
            for (const ImVec2 menuItemCentre : choiceCentres(ctx, kRadialItems)) {
                expect(distanceBetween(submenuCentres[index], menuItemCentre) >= unhoveredDiameter) << kRadialSubmenuItems[index] << " does not overlap with an item of the menu";
            }
        }
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] a click on an open submenu closes it and keeps the menu open") = [&] {
        openRadialMenu(ctx);
        openRadialSubmenu(ctx, kRadialSubmenu);
        ctx->ItemClick(menuItemRef(kRadialSubmenu).c_str());
        waitUntil(ctx, "the choices of the submenu are not drawn", [ctx] { return !ctx->ItemExists(menuItemRef(kRadialSubmenuItems[0]).c_str()); });
        expect(gState.radialMenu.isOpen);
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] an action in a submenu is called and closes the menu") = [&] {
        openRadialMenu(ctx);
        openRadialSubmenu(ctx, kRadialSubmenu);
        const int previousCount = gState.radialNestedCount;
        ctx->ItemClick(menuItemRef(kRadialSubmenuItems[0]).c_str());
        waitUntilRadialMenuClosed(ctx);
        expect(eq(gState.radialNestedCount, previousCount + 1));
    };

    boost::ut::test("[radial] an action two submenus deep is called and closes the menu") = [&] {
        openRadialMenu(ctx);
        openRadialSubmenu(ctx, kRadialSubmenu);
        openRadialSubmenu(ctx, kRadialDeeperSubmenu);
        const int previousCount = gState.radialDeepestCount;
        ctx->ItemClick(menuItemRef(kRadialDeepestAction).c_str());
        waitUntilRadialMenuClosed(ctx);
        expect(eq(gState.radialDeepestCount, previousCount + 1));
    };

    boost::ut::test("[radial] a submenu is closed when the menu opens again") = [&] {
        openRadialMenu(ctx);
        openRadialSubmenu(ctx, kRadialSubmenu);
        closeRadialMenu(ctx);
        openRadialMenu(ctx);
        expect(!ctx->ItemExists(menuItemRef(kRadialSubmenuItems[0]).c_str()));
        closeRadialMenu(ctx);
    };

    boost::ut::test("[radial] a choice menu without ChoiceMenuFlags::SupportsRadialMenu lists its choices one below the other") = [&] {
        ctx->SetRef(testWindowRef().c_str());
        ctx->ItemClick("open fallback menu");
        waitUntil(ctx, "the fallback menu is open and does not move", [] { return gState.fallbackMenu.isOpen && !gState.fallbackMenu.isAnimating; });
        ctx->SetRef(popupRef(ctx, kFallbackMenu).c_str());
        const std::vector<ImVec2> centres = choiceCentres(ctx, kFallbackChoices);
        expect(!areEquallyFarFromTheirMean(centres));
        for (std::size_t index = 1UZ; index < centres.size(); ++index) {
            expect(isNear(centres[index].x, centres[0].x) && centres[index].y > centres[index - 1UZ].y) << kFallbackChoices[index] << " is below the choice before it";
        }
        const int previousCount = gState.fallbackActionCount;
        ctx->ItemClick(menuItemRef(kFallbackChoices[0]).c_str());
        waitUntil(ctx, "the fallback menu is closed", [] { return !gState.fallbackMenu.isOpen; });
        expect(eq(gState.fallbackActionCount, previousCount + 1));
    };
}

} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* edgeTest = IM_REGISTER_TEST(engine(), "choicemenu", "edge");
        edgeTest->GuiFunc   = guiFunc;
        edgeTest->TestFunc  = testEdgeSelection;

        ImGuiTest* portraitTest = IM_REGISTER_TEST(engine(), "choicemenu", "portrait");
        portraitTest->GuiFunc   = guiFunc;
        portraitTest->TestFunc  = [](ImGuiTestContext* ctx) { testSheetAndChoiceMenuInViewport(ctx, kPortraitViewportSize); };

        ImGuiTest* landscapeTest = IM_REGISTER_TEST(engine(), "choicemenu", "landscape");
        landscapeTest->GuiFunc   = guiFunc;
        landscapeTest->TestFunc  = [](ImGuiTestContext* ctx) { testSheetAndChoiceMenuInViewport(ctx, kLandscapeViewportSize); };

        ImGuiTest* radialTest = IM_REGISTER_TEST(engine(), "choicemenu", "radial");
        radialTest->GuiFunc   = radialGuiFunc;
        radialTest->TestFunc  = testRadialMenu;
    }
};

int main(int argc, char** argv) {
    TestApp app(test::TestOptions::fromArgs(argc, argv));
    return app.runTests() ? 0 : 1;
}
