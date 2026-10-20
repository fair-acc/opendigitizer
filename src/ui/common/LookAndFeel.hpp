#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <imgui.h>
#include <implot.h>

enum class WindowMode { FULLSCREEN, MAXIMISED, MINIMISED, RESTORED };

/**
 * @brief Convert RGB color (0xRRGGBB) to ImGui's ABGR format (0xAABBGGRR).
 * @param rgb Color in RGB format (0xRRGGBB)
 * @param alpha Alpha value (default 0xFF for fully opaque)
 * @return Color in ImGui's ABGR format
 */
constexpr std::uint32_t rgbToImGuiABGR(std::uint32_t rgb, std::uint8_t alpha = 0xFF) {
    const std::uint32_t r = (rgb >> 16) & 0xFF;
    const std::uint32_t g = (rgb >> 8) & 0xFF;
    const std::uint32_t b = (rgb >> 0) & 0xFF;
    return (static_cast<std::uint32_t>(alpha) << 24) | (b << 16) | (g << 8) | r;
}

constexpr std::uint32_t float4ToRGBA(ImVec4 float4) {
    const auto saturate = [](float f) { return (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f; };
    auto       r        = static_cast<std::uint32_t>(saturate(float4.x) * 255.0f + 0.5f);
    auto       g        = static_cast<std::uint32_t>(saturate(float4.y) * 255.0f + 0.5f);
    auto       b        = static_cast<std::uint32_t>(saturate(float4.z) * 255.0f + 0.5f);
    auto       a        = static_cast<std::uint32_t>(saturate(float4.w) * 255.0f + 0.5f);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

inline ImVec4 lightenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    s = std::max(0.0f, s * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}

inline ImVec4 darkenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    v = std::max(0.0f, v * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}

inline ImVec4 darkenOrLighten(ImVec4 color, float percentage);

struct ImFont;

namespace DigitizerUi {

/// Colors that are not obviously included in the regular ImGuiStyle
/// TODO: maybe include popup menu colors (light grey hamburger icon, green buttons
/// on hamburger popup, red/violet buttons on radial menu) here, or make those use
/// colors from a style
struct Palette {
    ImVec4 gridLines;

    // main window buttons, shown on top of windowBg color, bg colors can be transparent
    ImVec4 mainWindowButtonIcon;
    ImVec4 mainWindowButtonBgInactive;
    ImVec4 mainWindowButtonBgHovered;
    ImVec4 mainWindowButtonBgActive;

    ImVec4 notificationWindowBg;

    ImVec4 toolbarLineColor;

    ImVec4 flowgraphBg;
    ImVec4 flowgraphNodeBg;
    ImVec4 flowgraphNodeBorder;
    ImVec4 flowgraphSubgraphBorder;
    ImVec4 flowgraphSubgraphBorderText;
    ImVec4 flowgraphUiControlFill;
    ImVec4 flowgraphUiControlHighlight;
    ImVec4 flowgraphUiControlConnectionIndicator;
    ImVec4 flowgraphUiControlPendingConnection;
    ImVec4 flowgraphUiControlUnlink;
    ImVec4 flowgraphUiControlUnlinkPressed;

    ImVec4 flowgraphBoundingBoxExteriorSelection;
    ImVec4 flowgraphBoundingBoxExteriorSelectionOutline;
    ImVec4 flowgraphBoundingBoxExteriorSelectionHovered;
    ImVec4 flowgraphBoundingBoxExteriorSelectionOutlineHovered;

    ImVec4 flowgraphGrid;
    ImVec4 flowgraphNodeBorderHovered;
    ImVec4 flowgraphNodeBorderSelected;
    ImVec4 flowgraphSelectionRect;
    ImVec4 flowgraphSelectionRectBorder;
    ImVec4 flowgraphLinkHovered;
    ImVec4 flowgraphLinkSelected;
    ImVec4 flowgraphLinkHighlighted;
    ImVec4 flowgraphPinRect;
    ImVec4 flowgraphPinRectBorder;

    ImVec4 rowBgAlt;
    ImVec4 highlightedSearchResultsBg;
    ImVec4 errorColor;
    ImVec4 warningColor;
    ImVec4 successColor;
    ImVec4 currentDashboardPanelBg;
    ImVec4 contentSeparator; // subtle divider lines between page content areas
    ImVec4 clearColor;       // behind every window, visible only where no window covers the viewport

    ImVec4 layoutModePanel;
    ImVec4 layoutModePanelHovered;
    ImVec4 layoutModePanelGrabbed;

    ImVec4 appHeaderFAIRLogoButton;
    ImVec4 appHeaderFAIRLogoButtonHovered;
    ImVec4 appHeaderFAIRLogoButtonActive;
    ImVec4 headerMenuButton;
    ImVec4 devMenuIcon; // alpha is set by the fade-out
    ImVec4 devMenuButton;
    ImVec4 devMenuQuitButton;

    ImVec4 legendButton;
    ImVec4 legendButtonHovered;
    ImVec4 legendButtonActive;

    ImVec4 keypadButtonColor;
    ImVec4 keypadTextColor;
    ImVec4 keypadFunctionKeyButtonColorMultiplier; // multiplied with ImGuiCol_Button

    ImVec4 notificationSuccess;
    ImVec4 notificationWarning;
    ImVec4 notificationError;
    ImVec4 notificationInfo;
    ImVec4 notificationDefault;

    ImVec4 chartTagFishy;
    ImVec4 chartTagDroppedSamples;
};

// from https://github.com/adobe/imgui imgui_spectrum.h
struct SpectrumSchemeKeywordColors {
    std::uint32_t gray50, gray75, gray100, gray200, gray300, gray400, gray500, gray600, gray700, gray800, gray900;
    std::uint32_t blue400, blue500, blue600, blue700;
    std::uint32_t red400, red500, red600, red700;
    std::uint32_t orange400, orange500, orange600, orange700;
    std::uint32_t green400, green500, green600, green700;
    std::uint32_t magenta500;
};

/// chart look a host may set process-wide; an unset field keeps ImPlot's current style
struct ChartStyle {
    std::optional<ImVec4>         plotBackground; // alpha 0: transparent
    std::optional<float>          gridAlpha;
    std::optional<float>          axisAlpha;
    std::optional<float>          lineWidth; // px, for every series
    bool                          colourAxesBySignal = true;
    std::optional<ImPlotLocation> legendLocation;
    std::optional<float>          legendAlpha;
    ImFont*                       labelFont = nullptr; // not owned: ticks, axis titles, legend, tags and tooltips
    std::optional<float>          labelFontSize;       // unscaled base size, as ImGui::PushFont takes it
    std::optional<ImVec4>         axisColour;
    std::optional<ImVec4>         gridColour;

    float  tagAlpha               = .35f; // multiplied with the signal colour
    float  maxHoldTraceAlpha      = .9f;
    float  minHoldTraceAlpha      = .5f;
    float  averageTraceAlpha      = .7f;
    ImVec2 spectrumFitPadding     = {0.f, .05f};
    float  menuSpinnerSpacing     = 2.f; // between the spinner buttons and their drag field
    float  menuAxisLimitDragWidth = 70.f;
    float  menuAxisComboWidth     = 80.f;
    float  menuSettingWidgetWidth = 120.f;
    float  historyDepthDragWidth  = 60.f;
    float  historyDepthComboWidth = 65.f;
};

/// dashboard look a host may set process-wide, for every DashboardView
struct DashboardStyle {
    enum class LegendPosition { Bottom, Top, Left, Right, None }; // None: no shared bar, charts may show their own legend
    LegendPosition legend     = LegendPosition::Bottom;
    bool           background = true; // false: chart windows and frames, plots area, bar and toolbar draw no background or border

    float toolbarRowHeight                = 36.f;
    float toolbarLeftPadding              = 16.f;
    float layoutModeChartInset            = 5.f;
    float legendSwatchBorderThickness     = 2.f;
    float legendSwatchRounding            = 0.f;
    float layoutModePanelRounding         = 0.f;
    float statusDotRadiusEms              = .3f; // relative to the font size
    float infoTextAlpha                   = .6f; // multiplied with ImGuiCol_TextDisabled
    float statusDotUnlitAlpha             = .3f; // multiplied with ImGuiCol_TextDisabled
    float toolbarBlockButtonSize          = 28.f;
    float toolbarBlockButtonRoundingRatio = .5f; // of the button size, .5 is a circle
    float toolbarControlInputWidth        = 110.f;
    float toolbarControlMinimumWidth      = 60.f;
    float toolbarControlSliderWidth       = 160.f;
    float newChartPopupWidth              = 300.f;

    struct DragAreaInteractionState {
        bool grabbed = false;
        bool hovered = false;
    };

    std::uint32_t draggableLayoutModePanelColor(DragAreaInteractionState interactionState) const;
};

struct OpenDashboardPageStyle {
    float  dashboardPreviewRounding                = 4.f;
    float  dashboardPreviewChartRounding           = 0.f;
    float  currentDashboardPanelPadding            = 15.f;
    float  currentDashboardPanelSpacing            = 10.f;
    float  loadedDashboardIndicatorBorderThickness = 2.f;
    float  dashboardPreviewLineThickness           = 1.f;
    float  dashboardPreviewChartPadding            = 2.f;
    float  dashboardPreviewSpectrumWaterfallGap    = 2.f;
    float  sidebarWidthRatio                       = .25f; // of the available width
    float  sidebarMaxWidth                         = 320.f;
    ImVec2 saveAsPopupSize                         = {600.f, 300.f};
    float  oauthPopupWidth                         = 500.f;
    float  addSourcePopupWidth                     = 600.f;
};

struct LookAndFeel {
    enum class Style { Light, Dark };
    enum class Theme { Classic, Advanced };

    struct Flowgraph {
        float  pinWidth  = 10;
        float  pinHeight = 10;
        ImVec2 minimumBlockSize{80.0f, 0.0f};

        float flowgraphBoundingBoxExteriorSelectionOutlineThickness        = 1.f;
        float flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered = 3.f;

        float exportedTabOverlap  = 6.0f;
        float exportedTabPaddingH = 4.0f;
        float exportedTabPaddingV = 2.0f;

        float nodeRounding                            = 0.f;
        float nodeBorderWidth                         = 1.5f;
        float subgraphBorderWidth                     = 3.f;
        float pinRounding                             = 0.f; // node editor pin hover rect
        float groupRounding                           = 6.f; // node editor group nodes
        float portRounding                            = 0.f; // drawn ports and exported tabs
        float pinArrowSize                            = 10.f;
        float pinArrowWidth                           = 10.f;
        float linkStrength                            = 60.f;
        float newLinkThickness                        = 1.f;
        float highlightThickness                      = 2.f; // drop target outline, pending connection line
        float uiControlBlockDragHandleBorderThickness = 3.f;
        float uiControlRegionBorderThickness          = 2.f;
        float uiControlNotchLengthInwards             = 5.f;
        float uiControlNotchLengthOutwards            = 5.f;
        float previewBorderThickness                  = 2.f;
        float filteredOutBlockAlpha                   = .25f;
        float controlsBarHeight                       = 37.f;
        float controlsBarPadding                      = 16.f;
        float nestedEditorInset                       = 16.f;
        float controlsBarButtonAlpha                  = 1.f;
        float computeDomainTagMaxOffset               = 30.f; // from the left edge of the subgraph
        float computeDomainTagOffsetRatio             = .2f;  // of the space available for the tag
        float uiControlRegionGap                      = 16.f; // around the UI control block region and between its blocks
        float uiControlDragHandleIconSize             = 12.f;

        float  previewBlockInnerPadding = 5.f; // per side
        float  previewBlockHeight       = 35.f;
        float  previewBlockSpacing      = 40.f;
        float  previewVerticalSpacing   = 10.f;
        ImVec2 previewPortSize          = {6.f, 6.f};
        float  previewCenterBlockSize   = 50.f;
        ImVec2 previewMargin            = {10.f, 10.f};
        ImVec2 previewArrowSize         = {9.f, 9.f};
        float  previewLinkThickness     = 1.f;

        float exportAreaHoverFadeSeconds = .4f;
        float exportAreaFadeInSeconds    = 1.f;
        float exportAreaFadeInSpeedup    = 4.f; // while a pin is held

        bool canvasBackground = true;
        bool canvasGrid       = true;
        bool canvasBorder     = true;
    };

#ifdef __EMSCRIPTEN__
    static constexpr bool isDesktop = false;
#else
    static constexpr bool isDesktop = true;
#endif
    static constexpr std::chrono::milliseconds longPressDuration{500};

    bool                      prototypeMode = false;
    std::chrono::milliseconds execTime; /// time it took to handle events and draw one frame
    float                     defaultDPI  = 76.2f;
    float                     verticalDPI = defaultDPI;
    ImFont*                   fontIcons;
    ImFont*                   fontIconsBig;
    ImFont*                   fontIconsLarge;
    ImFont*                   fontIconsSolid;
    ImFont*                   fontIconsSolidBig;
    ImFont*                   fontIconsSolidLarge;
    std::chrono::seconds      editPaneCloseDelay{15};
    float                     requiredDragDistanceForWindowUndock     = 30.f;
    float                     floatingWindowMinSizeFractionOfViewport = .25f;
    float                     splitterWidth                           = 6.f;
    float                     splitterDefaultRatio                    = .2f;
    float                     splitterRatioDeltaPerFrame              = .02f; // how much of (splitter / rest of screen) fraction to add
    float                     splitterRatioDeltaPerFrameSlow          = .01f;
    float                     keypadRounding                          = 6.f;
    ImVec2                    keypadPortraitSize                      = {400.f, 600.f};
    ImVec2                    keypadLandscapeSize                     = {485.f, 400.f};
    float                     popupMenuButtonRounding                 = 6.f;
    float                     popupMenuRadialButtonRoundingPercentage = .5f; // percentage of button size, so .5 is a circle
    float                     popupMenuAnimationLengthSeconds         = .25f;
    float                     popupMenuMouseoutCloseDelaySeconds      = .5f;
    float                     popupMenuMouseInactiveCloseDelaySeconds = 5.f;
    float                     popupMenuButtonHoverLighten             = .5f; // saturation factor
    float                     popupMenuButtonActiveDarken             = .7f; // value factor
    float                     popupMenuButtonAlpha                    = 1.f;
    float                     appHeaderRightMenuStartDegrees          = 75.f;
    float                     appHeaderRightMenuStopDegrees           = 195.f;
    float                     yesNoPopupWidth                         = 300.f;
    ImVec2                    addContextPopupSize                     = {600.f, 120.f};
    ImVec2                    removeContextPopupSize                  = {600.f, 100.f};
    float                     editableStringListInputWidthEms         = 12.f;
    float                     sortArrowScale                          = .75f;
    float                     searchHighlightRounding                 = 0.f;
    float                     selectedLabelRoundingPercentage         = 1.f; // percentage of the label height
    float                     notificationRounding                    = .5f;
    float                     notificationBorderSize                  = 1.f;
    float                     devMenuHideDelaySeconds                 = 2.f;
    float                     devMenuButtonAlpha                      = 0.9f;
    std::chrono::seconds      infoDotLifetime{5};
    std::chrono::seconds      notificationDismissTime{5};
    std::chrono::seconds      notificationErrorDismissTime{10};
    double                    plotTooltipDelay         = 1.;  // s
    double                    plotTooltipDuration      = 30.; // s
    float                     plotTooltipStillDistance = 10.f;
    Flowgraph                 flowgraph;
    ChartStyle                chartStyle;
    DashboardStyle            dashboardStyle;
    OpenDashboardPageStyle    openDashboardPageStyle;

    [[nodiscard]] const Palette&                     palette() const noexcept;
    [[nodiscard]] const SpectrumSchemeKeywordColors& spectrumColours() const noexcept; // for the current style, also while theme is Classic
    [[nodiscard]] float                              mainWindowIconButtonSize() const noexcept;

    [[nodiscard]] ImFont* fontTiny() const noexcept { return _fontTiny[prototypeMode]; }
    [[nodiscard]] ImFont* fontSmall() const noexcept { return _fontSmall[prototypeMode]; }
    [[nodiscard]] ImFont* fontNormal() const noexcept { return _fontNormal[prototypeMode]; }
    [[nodiscard]] ImFont* fontBig() const noexcept { return _fontBig[prototypeMode]; }
    [[nodiscard]] ImFont* fontBigger() const noexcept { return _fontBigger[prototypeMode]; }
    [[nodiscard]] ImFont* fontLarge() const noexcept { return _fontLarge[prototypeMode]; }

    [[nodiscard]] float relativeFontSize(const ImFont* face) const noexcept {
        const ImFont* normal = fontNormal();
        return face && normal ? ImGui::GetStyle().FontSizeBase * face->LegacySize / normal->LegacySize : 0.f;
    }

    Style      style      = Style::Light;
    Theme      theme      = Theme::Classic;
    WindowMode windowMode = WindowMode::RESTORED;

    static LookAndFeel&       mutableInstance();
    static const LookAndFeel& instance();

    static std::uint8_t  getColorAlphaU8(ImVec4 Palette::*color) { return std::clamp(static_cast<std::uint8_t>((instance().palette().*color).w * 255.f), std::uint8_t{0x00}, std::uint8_t{0xFF}); }
    static std::uint32_t getColorU32(ImVec4 Palette::*color) { return float4ToRGBA(instance().palette().*color); }
    static std::uint32_t getColorU32ImGui(ImVec4 Palette::*color) {
        const auto vec4 = instance().palette().*color;
        const auto rgba = float4ToRGBA(vec4);
        return rgbToImGuiABGR(rgba >> 8, static_cast<std::uint8_t>(vec4.w * 0xFF));
    }
    static std::uint32_t getColorU32Opaque(ImVec4 Palette::*color) {
        const auto vec4 = instance().palette().*color;
        return float4ToRGBA({vec4.x, vec4.y, vec4.z, 1.f}) >> 8;
    }

    void loadFonts();
    void applyTheme();

private:
    std::array<ImFont*, 2> _fontTiny   = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontSmall  = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontNormal = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontBig    = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontBigger = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontLarge  = {nullptr, nullptr}; // [0] production, [1] prototype

    LookAndFeel() {}
};

} // namespace DigitizerUi

inline ImVec4 darkenOrLighten(ImVec4 color, float percentage) { //
    return DigitizerUi::LookAndFeel::instance().style == DigitizerUi::LookAndFeel::Style::Dark ? lightenColor(color, percentage) : darkenColor(color, percentage);
};

#endif
