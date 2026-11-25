#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <imgui.h>
#include <implot.h>

#include "AppState.hpp"

enum class ChoiceMenuStyle {
    Automatic, // Sheet while a touchscreen is the primary pointing device (isTouchscreenPrimaryPointer()), otherwise Desktop
    Desktop,   // normal imgui style
    Sheet,
    Radial, // choice menus with ChoiceMenuFlags::SupportsRadialMenu are radial menus, all other popups are drawn as with Automatic
};

enum class SheetEdge { Bottom, Right };

constexpr std::uint32_t rgbToImGuiABGR(std::uint32_t rgb, std::uint8_t alpha = 0xFF) {
    const std::uint32_t r = (rgb >> 16) & 0xFF;
    const std::uint32_t g = (rgb >> 8) & 0xFF;
    const std::uint32_t b = (rgb >> 0) & 0xFF;
    return (static_cast<std::uint32_t>(alpha) << 24) | (b << 16) | (g << 8) | r;
}

constexpr std::uint32_t float3ToRGB(const float (&rgb)[3]) { return (static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f); }

constexpr std::uint32_t float4ToRGBA(ImVec4 float4) {
    const auto saturate = [](float f) { return (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f; };
    auto       r        = static_cast<std::uint32_t>(saturate(float4.x) * 255.0f + 0.5f);
    auto       g        = static_cast<std::uint32_t>(saturate(float4.y) * 255.0f + 0.5f);
    auto       b        = static_cast<std::uint32_t>(saturate(float4.z) * 255.0f + 0.5f);
    auto       a        = static_cast<std::uint32_t>(saturate(float4.w) * 255.0f + 0.5f);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

inline ImVec4 lightenColorByReducingSaturation(const ImVec4& color, float percent) {
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

inline ImVec4 darkenColorByReducingValue(const ImVec4& color, float percent) {
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

inline ImVec4 overlayTransparentOverOpaque(ImVec4 color, ImVec4 opaqueBackground) {
    const float a = color.w;
    return {color.x * a + opaqueBackground.x * (1.f - a), color.y * a + opaqueBackground.y * (1.f - a), color.z * a + opaqueBackground.z * (1.f - a), 1.f};
}

struct ImFont;

namespace gr::od {

/// colours ImGuiStyle lacks
struct Palette {
    ImVec4 gridLines;

    std::optional<ImVec4> buttonContents; // text on buttons, selectables and checkboxes; unset: ImGuiCol_Text

    std::optional<ImVec4> mainWindowButtonIcon; // unset: ImGuiCol_Text
    ImVec4                mainWindowButtonBgInactive;
    ImVec4                mainWindowButtonBgHovered;
    ImVec4                mainWindowButtonBgActive;

    ImVec4 notificationWindowBg;

    ImVec4 toolbarLineColor;

    ImVec4 widgetTrackInactive; // slider track right of the knob, toggle switch track when off
    ImVec4 toggleKnob;

    ImVec4 flowgraphBg;
    ImVec4 flowgraphNodeBg;
    ImVec4 flowgraphNodeBorder;
    ImVec4 flowgraphSubgraphBorder;
    ImVec4 flowgraphSubgraphBorderText;
    ImVec4 flowgraphUiControlHighlight;
    ImVec4 flowgraphUiControlConnectionIndicator;
    ImVec4 flowgraphUiControlPendingConnection;
    ImVec4 flowgraphUiControlUnlink;
    ImVec4 flowgraphUiControlUnlinkPressed;
    ImVec4 flowgraphNodeMenuButtonHovered;
    ImVec4 flowgraphNodeMenuButtonActive;

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
    ImVec4 clearColor;

    ImVec4                appHeaderFAIRLogoButton;
    ImVec4                appHeaderFAIRLogoButtonHovered;
    ImVec4                appHeaderFAIRLogoButtonActive;
    ImVec4                headerMenuButton;
    std::optional<ImVec4> devMenuIcon; // ImGuiCol_Text by default
    ImVec4                devMenuButton;
    std::optional<ImVec4> popupMenuButtonIcon; // ImGuiCol_Text by default
    ImVec4                devMenuQuitButton;

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
    std::uint32_t red500, red700;
    std::uint32_t orange500;
    std::uint32_t green500;
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
    ImVec4 frequencyMarkerColour  = {.95f, .55f, .1f, 1.f}; // spectrum charts' frequency range and f0: orange, legible on light and dark
    float  rangeFillAlpha         = .2f;                    // the range's shaded area: 80 % translucent
    float  rangeLineWidth         = 2.f;                    // px, the range's edges and the f0 line
    float  f0DashLength           = 6.f;                    // px, dash and gap of a dashed f0
    double markerInitialInset     = .05;                    // an unset range starts this fraction of the visible span inside its edges
    float  menuSpinnerSpacing     = 2.f;                    // between the spinner buttons and their drag field
    float  menuAxisLimitDragWidth = 70.f;
    float  menuAxisComboWidth     = 80.f;
    float  menuSettingWidgetWidth = 120.f;
    float  historyDepthDragWidth  = 60.f;
    float  historyDepthComboWidth = 65.f;
    int    colormapPreviewSteps   = 32;

    float       imageTileGap       = .04f;          // between ImageChart tiles, as a fraction of the larger tile extent
    std::size_t imageCpuMaxCells   = 512UZ * 512UZ; // ImageChart CPU heatmap budget; larger frames are decimated
    std::size_t imageCpuMaxRects   = 120UZ * 120UZ; // CPU cells of a layer with transparency: 4 vertices each, within 16-bit indices (WebGL2)
    ImVec2      imageStatusPadding = {6.f, 4.f};    // skipped-frame indicator, from the plot's top-left corner

    float scatterMarkerSize       = 2.f; // px, YYChart scatter markers
    float scatterNewestMarkerSize = 5.f; // px, the newest point

    float  localisationCurveWidth             = 1.5f;       // px
    float  localisationSensorMarkerSize       = 5.f;        // px, receivers and transmitters
    float  localisationIntersectionMarkerSize = 3.f;        // px
    float  localisationMeanMarkerSize         = 9.f;        // px, the detected mean, alternatives and the reference
    float  localisationMeanMarkerWeight       = 2.f;        // px
    float  localisationLabelOffset            = 10.f;       // px above a marker
    float  localisationTagEdgeMargin          = .1f;        // a curve's annotation keeps this fraction of the roi from its edge
    ImVec2 localisationStatusPadding          = {6.f, 6.f}; // status line, from the plot's top-left corner

    float  rdsRounding          = 6.f;
    ImVec2 rdsPillPadding       = {8.f, 2.f};
    float  rdsLitAlpha          = .15f; // also of a warning row
    float  rdsUnlitAlpha        = .06f;
    float  rdsDotRadius         = 4.f;
    float  rdsLineWidth         = 2.f;
    float  rdsGraphHeightLines  = 3.f;
    float  rdsTwoColumnMinWidth = 640.f; // px
};

/// dashboard look a host may set process-wide, for every DashboardView
struct DashboardStyle {
    enum class LegendPosition { Bottom, Top, Left, Right, None }; // None: no shared bar, charts may show their own legend
    LegendPosition legend     = LegendPosition::Bottom;
    bool           background = true; // false: chart windows and frames, plots area, bar and toolbar draw no background or border

    float toolbarRowHeight               = 36.f;
    float toolbarLeftPadding             = 16.f;
    float toolbarItemSpacing             = 24.f;
    float layoutModeChartInset           = 5.f;
    float legendColorRectBorderThickness = 2.f;
    float legendAddButtonIconScale       = .7f; // icon font size relative to the button size
    float legendSwatchRounding           = 0.f;
    int   legendNumRowsBeforeShrinking   = 2;
    float legendMinFontScale             = .6f; // the legend will accept shrinking down to this size before starting to ellipsize the text
    float layoutModePanelRounding        = 0.f;
    float statusDotRadiusEms             = .3f; // relative to the font size
    float infoTextAlpha                  = .6f; // multiplied with ImGuiCol_TextDisabled
    float statusDotUnlitAlpha            = .3f; // multiplied with ImGuiCol_TextDisabled
    float toolbarControlInputWidth       = 110.f;
    float toolbarControlMinimumWidth     = 60.f;
    float toolbarControlSliderTrackWidth = 115.f;
    float newChartPopupWidth             = 300.f;
    float newChartPopupMinWidth          = 200.f;
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
    float  compactFiltersMaxHeightRatio            = .5f; // of the height below the search field
    ImVec2 saveAsPopupSize                         = {600.f, 300.f};
    float  oauthPopupWidth                         = 500.f;
    float  addSourcePopupWidth                     = 600.f;
};

struct LookAndFeel {
    enum class Style { Light, Dark };
    enum class Theme { Classic, Advanced };

    struct Flowgraph {
        float  pinWidth      = 16;
        float  pinHeight     = 16;
        float  minimumPinGap = 10; // edge to edge, between vertically neighbouring pins
        ImVec2 minimumBlockSize{80.0f, 0.0f};

        float flowgraphBoundingBoxExteriorSelectionOutlineThickness        = 1.f;
        float flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered = 3.f;

        float exportedTabOverlap  = 6.0f;
        float exportedTabPaddingH = 4.0f;
        float exportedTabPaddingV = 2.0f;

        float nodeRounding                         = 6.f;
        float nodePadding                          = 13.f;
        float nodeBorderWidth                      = 1.5f;
        float subgraphBorderWidth                  = 3.f;
        float imguiNodeEditorPortRounding          = 2.f;
        float imguiNodeEditorGroupNodeRounding     = 6.f;
        float rectangularPortRounding              = 2.f;
        float dataTypeIconLineThickness            = 1.f;
        float float64IconRingGapWidthPixels        = 1.f;
        float float64InnerRingRadiusRatio          = .55f; // of the dot radius
        float pinHoverHaloWidth                    = 2.f;
        float eventLinkDashLength                  = 6.f;
        float eventLinkGapLength                   = 4.f;
        float complexNumbersIconDotWidthFactor     = 1.5f; // relative to the regular single dot diameter
        float complexNumbersIconDotPairGap         = 1.f;
        float containerIconsCornerRingRadiusFactor = 0.25f; // relative to shape size
        float containerIconsCornerRingInsetFactor  = 0.1f;  // relative to shape size
        float dataSetIconWaveAmplitudeFactor       = 0.1f;
        int   dataSetIconNumLineSegmentsPerWave    = 16;
        float pinArrowSize                         = 10.f;
        float pinArrowWidth                        = 10.f;
        float linkStrength                         = 60.f;
        float newLinkThickness                     = 1.f;
        float highlightThickness                   = 2.f; // drop target outline, pending connection line
        float dropTargetOutlineThickness           = 4.f;
        float menuButtonInset                      = .15f; // fraction of the block menu button inside the node's corner, on each axis
        float menuButtonRounding                   = 4.f;
        float uiControlRegionBorderThickness       = 2.f;
        float uiControlNotchLengthInwards          = 5.f;
        float uiControlNotchLengthOutwards         = 5.f;
        float uiControlTargetLineThickness         = 1.f;
        float uiControlTargetDotRadius             = 3.f;
        float uiControlTargetRowSpacing            = 2.f;
        float previewBorderThickness               = 2.f;
        float filteredOutBlockAlpha                = .25f;
        float controlsBarHeight                    = 37.f;
        float controlsBarPadding                   = 16.f;
        float nestedEditorInset                    = 16.f;
        float computeDomainTagMaxOffset            = 30.f; // from the left edge of the subgraph
        float computeDomainTagOffsetRatio          = .2f;  // of the space available for the tag
        float uiControlRegionGap                   = 16.f; // around the UI control block region and between its blocks
        float uiControlDragHandleRadius            = 5.f;
        float uiControlDragHandleHoveredRadius     = 6.f;
        float unplacedBlockGap                     = 32.f; // px

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
    static constexpr bool kIsDesktop = false;
#else
    static constexpr bool kIsDesktop = true;
#endif
    static constexpr std::chrono::milliseconds kLongPressDuration{500};

    float                  compactEnterShortSide = 600.f;
    float                  compactLeaveShortSide = 640.f;
    float                  compactFontScale      = .75f;
    ImFont*                fontIcons;
    ImFont*                fontIconsBig;
    ImFont*                fontIconsSolid;
    ImFont*                fontIconsSolidBig;
    ImFont*                fontIconsSolidLarge;
    std::chrono::seconds   editPaneCloseDelay{15};
    float                  editPaneCloseBarHeight                  = 3.f;
    float                  requiredDragDistanceForWindowUndock     = 30.f;
    float                  floatingWindowMinSizeFractionOfViewport = .25f;
    float                  splitterWidth                           = 6.f;
    float                  splitterDefaultRatio                    = .2f;
    float                  splitterRatioDeltaPerFrame              = .02f; // how much of (splitter / rest of screen) fraction to add
    float                  splitterRatioDeltaPerFrameSlow          = .01f;
    float                  keypadRounding                          = 6.f;
    ImVec2                 keypadPortraitSize                      = {400.f, 600.f};
    ImVec2                 keypadLandscapeSize                     = {485.f, 400.f};
    float                  popupMenuButtonRounding                 = 6.f;
    float                  popupMenuRadialButtonRoundingPercentage = .5f; // percentage of button size, so .5 is a circle
    float                  popupMenuAnimationLengthSeconds         = .25f;
    float                  popupMenuMouseoutCloseDelaySeconds      = .5f;
    float                  popupMenuMouseInactiveCloseDelaySeconds = 5.f;
    float                  popupMenuButtonHoverLighten             = .5f; // saturation factor
    float                  popupMenuButtonActiveDarken             = .7f; // value factor
    float                  popupMenuButtonAlpha                    = 1.f;
    ImVec2                 buttonFramePaddingMultiplier            = {2.f, 1.f}; // of ImGuiStyle::FramePadding
    float                  appHeaderMenuToLogoGap                  = 12.f;
    float                  appHeaderRightMenuStartDegrees          = 90.f;
    float                  appHeaderRightMenuStopDegrees           = 195.f;
    float                  yesNoPopupWidth                         = 300.f;
    ImVec2                 addContextPopupSize                     = {600.f, 120.f};
    ImVec2                 removeContextPopupSize                  = {600.f, 100.f};
    float                  editableStringListInputWidthEms         = 12.f;
    float                  filterComboTextWidthScale               = 1.3f; // of the label width
    float                  filterComboWrapScale                    = 1.2f; // of the combo width
    ImVec2                 selectionPopupListSize                  = {280.f, 180.f};
    float                  selectionPopupDoneWidth                 = 100.f;
    float                  sortArrowScale                          = .75f;
    float                  sliderTrackThickness                    = 4.f;
    float                  sliderKnobRadiusFrameHeightRatio        = .35f;
    float                  sliderKnobRingThickness                 = 3.f;
    float                  sliderTextInputMinWidthEms              = 3.5f;
    float                  sliderNumberLabelMaxWidthEms            = 4.f;
    float                  toggleWidthFrameHeightRatio             = 1.75f;
    float                  toggleKnobInset                         = 2.f;
    float                  toggleHoveredDarkenPercentage           = .15f;
    float                  toggleAnimationSeconds                  = .12f;
    float                  comboOutlineThickness                   = 1.5f;
    float                  comboChevronThickness                   = 2.f;
    float                  comboChevronWidthEms                    = .55f;
    float                  comboTextMaxWidthEms                    = 16.f; // longer values are drawn with an ellipsis
    float                  searchHighlightRounding                 = 0.f;
    float                  selectedLabelRoundingPercentage         = 1.f; // percentage of the label height
    float                  notificationRounding                    = .5f;
    float                  notificationBorderSize                  = 1.f;
    float                  autoHideDelaySeconds                    = 2.f;   // developer menu, menu button and dashboard bar controls
    bool                   autoHideLegends                         = true;  // unlocked charts' legends: shown while the mouse moves in the plot, faded after the delay
    float                  autoHideRevealDistance                  = 100.f; // px around an auto-hidden control that brings it back
    float                  autoHideSlideSeconds                    = .5f;
    float                  devMenuButtonAlpha                      = 0.9f;
    std::chrono::seconds   infoDotLifetime{5};
    std::chrono::seconds   notificationDismissTime{5};
    std::chrono::seconds   notificationErrorDismissTime{10};
    double                 plotTooltipDelay         = 1.;  // s
    double                 plotTooltipDuration      = 30.; // s
    float                  plotTooltipStillDistance = 10.f;
    double                 maxFrameRate             = 60.; // Hz
    Flowgraph              flowgraph;
    ChartStyle             chartStyle;
    DashboardStyle         dashboardStyle;
    OpenDashboardPageStyle openDashboardPageStyle;
    ChoiceMenuStyle        choiceMenuStyle                  = ChoiceMenuStyle::Automatic;
    float                  sheetMaxLengthFractionOfDisplay  = .75f;
    float                  sheetDismissThresholdFraction    = .3f;
    float                  sheetHeaderRoundingEms           = 1.f;
    float                  sheetHandlePillLengthEms         = 2.5f;
    float                  sheetHandlePillThicknessEms      = .3f;
    float                  sheetHandlePillAlpha             = .4f; // multiplied with the alpha of ImGuiCol_Text
    float                  sheetRowHeightFrameHeightRatio   = 1.5f;
    ImVec2                 sheetMinContentSizeEms           = ImVec2{16.f, 3.f}; // the x component for a sheet on the right edge, y for the bottom edge
    float                  sheetAnimationDurationSeconds    = .25f;
    float                  colorPickerWidthFrameHeightRatio = 12.f; // default value is taken from ImGui::ColorEdit4() which uses 12*FrameHeight

    Style style = Style::Light;
    Theme theme = Theme::Classic;

    std::array<ImFont*, 2> _fontTiny   = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontSmall  = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontNormal = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontBig    = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontBigger = {nullptr, nullptr}; // [0] production, [1] prototype
    std::array<ImFont*, 2> _fontLarge  = {nullptr, nullptr}; // [0] production, [1] prototype

    bool _isTouchscreenPrimaryPointer = false; // isTouchscreenPrimaryPointer() at the start of the current frame

    LookAndFeel() {}

    [[nodiscard]] const Palette&                     palette() const noexcept;
    [[nodiscard]] const SpectrumSchemeKeywordColors& spectrumColours() const noexcept; // for the current style, also while theme is Classic
    [[nodiscard]] float                              mainWindowIconButtonSize() const noexcept;

    /// whether popups that can be drawn as a sheet, like Popup and Menu, are drawn as one
    [[nodiscard]] bool usesSheetPopups() const;

    [[nodiscard]] SheetEdge sheetEdge() const noexcept {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        return displaySize.x <= displaySize.y ? SheetEdge::Bottom : SheetEdge::Right;
    }

    [[nodiscard]] ImFont* fontTiny() const noexcept { return _fontTiny[gAppState.prototypeMode]; }
    [[nodiscard]] ImFont* fontSmall() const noexcept { return _fontSmall[gAppState.prototypeMode]; }
    [[nodiscard]] ImFont* fontNormal() const noexcept { return _fontNormal[gAppState.prototypeMode]; }
    [[nodiscard]] ImFont* fontBig() const noexcept { return _fontBig[gAppState.prototypeMode]; }
    [[nodiscard]] ImFont* fontBigger() const noexcept { return _fontBigger[gAppState.prototypeMode]; }
    [[nodiscard]] ImFont* fontLarge() const noexcept { return _fontLarge[gAppState.prototypeMode]; }

    [[nodiscard]] float relativeFontSize(const ImFont* face) const noexcept {
        const ImFont* normal = fontNormal();
        return face && normal ? ImGui::GetStyle().FontSizeBase * face->LegacySize / normal->LegacySize : 0.f;
    }

    static LookAndFeel&       mutableInstance();
    static const LookAndFeel& instance();

    static std::uint8_t  getColorAlphaU8(ImVec4 Palette::*color) { return std::clamp(static_cast<std::uint8_t>((instance().palette().*color).w * 255.f), std::uint8_t{0x00}, std::uint8_t{0xFF}); }
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
	/// this should be called every frame to detect whether to use mobile styles
    void readTouchscreenDevices();
    void updateFormFactor(ImVec2 viewportSize);
};

} // namespace gr::od

#endif
