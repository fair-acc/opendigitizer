#include "LookAndFeel.hpp"

#include "ImguiWrap.hpp"

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(fonts);
CMRC_DECLARE(ui_assets);

enum FontSizeIndex { FontSizeTiny = 0, FontSizeSmall, FontSizeNormal, FontSizeBig, FontSizeBigger, FontSizeLarge, FontSizeCount };

template<size_t sizeIndex>
requires(sizeIndex < FontSizeCount)
static float fontSize(const DigitizerUi::LookAndFeel& instance) {
    using Sizes = std::array<float, FontSizeCount>;
    if (std::abs(instance.verticalDPI - instance.defaultDPI) < 8.f) {
        return Sizes{12, 17, 20, 24, 28, 46}[sizeIndex]; // 28" monitor
    } else if (instance.verticalDPI > 200) {
        return Sizes{8, 13, 16, 22, 23, 38}[sizeIndex]; // likely mobile monitor
    } else if (std::abs(instance.defaultDPI - instance.verticalDPI) >= 8.f) {
        return Sizes{12, 18, 22, 26, 30, 46}[sizeIndex]; // likely large fixed display monitor
    }
    return Sizes{12, 16, 18, 24, 26, 46}[sizeIndex]; // default
};

namespace DigitizerUi {

LookAndFeel& LookAndFeel::mutableInstance() {
    static LookAndFeel s_instance;
    return s_instance;
}

const LookAndFeel& LookAndFeel::instance() { return mutableInstance(); }

namespace {
ImVec4 rgba(std::uint32_t rgb, std::uint8_t alpha = 0xFF) { return ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(rgb, alpha)); }

Palette classicPalette(LookAndFeel::Style style) {
    const bool          dark                    = style == LookAndFeel::Style::Dark;
    const ImVec4        quarterTransparentWhite = {1.0f, 1.0f, 1.0f, 0.25f};
    const ImVec4        quarterTransparentBlack = {0.0f, 0.0f, 0.0f, 0.25f};
    const ImVec4        black                   = {0.0f, 0.0f, 0.0f, 1.f};
    const ImVec4        white                   = {1.0f, 1.0f, 1.0f, 1.f};
    const std::uint32_t uiControlHighlight      = 0x1E90FF;
    const std::uint32_t nodeEditorHover         = 0x32B0FF;
    const std::uint32_t nodeEditorSelection     = 0xFFB032;
    const std::uint32_t nodeEditorSelectionRect = 0x0582FF;
    const std::uint32_t nodeEditorPinRect       = 0x3CB4FF;
    const std::uint8_t  overlayAlpha            = 0x99;
    return Palette{
        .gridLines = dark ? quarterTransparentWhite : quarterTransparentBlack,

        .buttonContents = std::nullopt,

        .mainWindowButtonIcon       = dark ? std::make_optional(rgba(0xCCCCCC)) : std::nullopt,
        .mainWindowButtonBgInactive = dark ? rgba(0xFFFFFF, 0x00) : rgba(0x000000, 0x00),
        .mainWindowButtonBgHovered  = dark ? rgba(0xFFFFFF, 0x20) : rgba(0x000000, 0x20),
        .mainWindowButtonBgActive   = dark ? rgba(0xFFFFFF, 0x33) : rgba(0x000000, 0x33),

        .notificationWindowBg = dark ? rgba(0x202020) : rgba(0xFFFFFF),
        .toolbarLineColor     = dark ? quarterTransparentWhite : quarterTransparentBlack,

        .flowgraphBg                           = dark ? rgba(0x202020) : rgba(0xFFFFFF),
        .flowgraphNodeBg                       = dark ? rgba(0x333333) : rgba(0xEFEAFF),
        .flowgraphNodeBorder                   = dark ? rgba(0xB3B3B3) : rgba(0x606060),
        .flowgraphSubgraphBorder               = rgba(0xDFA32B),
        .flowgraphSubgraphBorderText           = black,
        .flowgraphUiControlFill                = rgba(uiControlHighlight, 0x33),
        .flowgraphUiControlHighlight           = rgba(uiControlHighlight),
        .flowgraphUiControlConnectionIndicator = rgba(0x555555),
        .flowgraphUiControlPendingConnection   = rgba(uiControlHighlight),
        .flowgraphUiControlUnlink              = rgba(0xD32F2F),
        .flowgraphUiControlUnlinkPressed       = rgba(0xFF8A80),

        .flowgraphBoundingBoxExteriorSelection               = rgba(0x8684F5, 0x33),
        .flowgraphBoundingBoxExteriorSelectionOutline        = rgba(0x806AF7),
        .flowgraphBoundingBoxExteriorSelectionHovered        = rgba(0x7B69FF, 0x55),
        .flowgraphBoundingBoxExteriorSelectionOutlineHovered = rgba(0x563AE0),

        .flowgraphGrid                = rgba(0x787878, 40),
        .flowgraphNodeBorderHovered   = rgba(nodeEditorHover),
        .flowgraphNodeBorderSelected  = rgba(nodeEditorSelection),
        .flowgraphSelectionRect       = rgba(nodeEditorSelectionRect, 64),
        .flowgraphSelectionRectBorder = rgba(nodeEditorSelectionRect, 128),
        .flowgraphLinkHovered         = rgba(nodeEditorHover),
        .flowgraphLinkSelected        = rgba(nodeEditorSelection),
        .flowgraphLinkHighlighted     = rgba(0xCC6900),
        .flowgraphPinRect             = rgba(nodeEditorPinRect, 100),
        .flowgraphPinRectBorder       = rgba(nodeEditorPinRect, 128),

        .rowBgAlt                   = dark ? rgba(0x333333) : rgba(0xCCCCCC),
        .highlightedSearchResultsBg = dark ? rgba(0x1A94F0, 0x66) : rgba(0xFFFF00, 0x88), // dark: saturated but transparent blue, goes behind white text
        .errorColor                 = rgba(0xFF0000),
        .warningColor               = {1.f, .65f, 0.f, 1.f},
        .successColor               = {.2f, .75f, .2f, 1.f},
        .currentDashboardPanelBg    = dark ? rgba(0xFFFFFF, 0x33) : rgba(0x000000, 0x33),
        .clearColor                 = white,

        .layoutModePanel        = rgba(0x9EF7F1, overlayAlpha),
        .layoutModePanelHovered = rgba(0x6EC9CF, overlayAlpha),
        .layoutModePanelGrabbed = rgba(0x63E69F, overlayAlpha),

        .appHeaderFAIRLogoButton        = {0, 0, 0, 0},
        .appHeaderFAIRLogoButtonHovered = {.8f, .8f, .8f, .4f},
        .appHeaderFAIRLogoButtonActive  = {0, 0, 0, 0},
        .headerMenuButton               = rgba(0x7EBC89),
        .devMenuIcon                    = dark ? std::make_optional(ImVec4{.8f, .8f, .8f, 1.f}) : std::nullopt,
        .devMenuButton                  = {.3f, .3f, 1.f, 1.f},
        .popupMenuButtonIcon            = std::nullopt,
        .devMenuQuitButton              = rgba(0xFF0000),

        .legendButton        = {0.f, 0.f, 0.f, 0.f},
        .legendButtonHovered = {0.f, 0.f, 0.f, .1f},
        .legendButtonActive  = {0.f, 0.f, 0.f, .2f},

        .keypadButtonColor                      = rgba(0x0B59BF),
        .keypadTextColor                        = white,
        .keypadFunctionKeyButtonColorMultiplier = {.6f, .6f, .8f, 1.f},

        .notificationSuccess = rgba(0x00FF00),
        .notificationWarning = rgba(0xFFFF00),
        .notificationError   = rgba(0xFF0000),
        .notificationInfo    = rgba(0x009DFF),
        .notificationDefault = white,

        .chartTagFishy          = rgba(0xFF00FF),
        .chartTagDroppedSamples = {1.f, .2f, .2f, 1.f},
    };
}

constexpr SpectrumSchemeKeywordColors kSpectrumLight{
    .gray50     = 0xFFFFFF,
    .gray75     = 0xFAFAFA,
    .gray100    = 0xF5F5F5,
    .gray200    = 0xEAEAEA,
    .gray300    = 0xE1E1E1,
    .gray400    = 0xCACACA,
    .gray500    = 0xB3B3B3,
    .gray600    = 0x8E8E8E,
    .gray700    = 0x707070,
    .gray800    = 0x4B4B4B,
    .gray900    = 0x2C2C2C,
    .blue400    = 0x2680EB,
    .blue500    = 0x1473E6,
    .blue600    = 0x0D66D0,
    .blue700    = 0x095ABA,
    .red400     = 0xE34850,
    .red500     = 0xD7373F,
    .red600     = 0xC9252D,
    .red700     = 0xBB121A,
    .orange400  = 0xE68619,
    .orange500  = 0xDA7B11,
    .orange600  = 0xCB6F10,
    .orange700  = 0xBD640D,
    .green400   = 0x2D9D78,
    .green500   = 0x268E6C,
    .green600   = 0x12805C,
    .green700   = 0x107154,
    .magenta500 = 0xCE2783,
};

constexpr SpectrumSchemeKeywordColors kSpectrumDark{
    .gray50     = 0x252525,
    .gray75     = 0x2F2F2F,
    .gray100    = 0x323232,
    .gray200    = 0x393939,
    .gray300    = 0x3E3E3E,
    .gray400    = 0x4D4D4D,
    .gray500    = 0x5C5C5C,
    .gray600    = 0x7B7B7B,
    .gray700    = 0x999999,
    .gray800    = 0xCDCDCD,
    .gray900    = 0xFFFFFF,
    .blue400    = 0x2680EB,
    .blue500    = 0x378EF0,
    .blue600    = 0x4B9CF5,
    .blue700    = 0x5AA9FA,
    .red400     = 0xE34850,
    .red500     = 0xEC5B62,
    .red600     = 0xF76D74,
    .red700     = 0xFF7B82,
    .orange400  = 0xE68619,
    .orange500  = 0xF29423,
    .orange600  = 0xF9A43F,
    .orange700  = 0xFFB55B,
    .green400   = 0x2D9D78,
    .green500   = 0x33AB84,
    .green600   = 0x39B990,
    .green700   = 0x3FC89C,
    .magenta500 = 0xE2499D,
};

const SpectrumSchemeKeywordColors& spectrumRamp(LookAndFeel::Style style) { return style == LookAndFeel::Style::Dark ? kSpectrumDark : kSpectrumLight; }

Palette spectrumPalette(LookAndFeel::Style style) {
    const bool                         dark = style == LookAndFeel::Style::Dark;
    const SpectrumSchemeKeywordColors& c    = spectrumRamp(style);
    const ImVec4                       white{1.f, 1.f, 1.f, 1.f};
    return Palette{
        .gridLines = rgba(c.gray400),

        // it would be nice to use white with a bright highlight color, but there are some limitations
        // such as the imgui date picker which cannot use this component of our custom style, causing
        // illegible buttons or clashing in that popup
        // .buttonContents = white,
        .buttonContents = std::nullopt,

        .mainWindowButtonIcon       = dark ? std::make_optional(rgba(c.gray700)) : std::nullopt,
        .mainWindowButtonBgInactive = rgba(c.gray200, 0x00),
        .mainWindowButtonBgHovered  = rgba(c.gray200),
        .mainWindowButtonBgActive   = rgba(c.gray300),

        .notificationWindowBg = rgba(c.gray50),
        .toolbarLineColor     = rgba(c.gray300),

        .flowgraphBg                           = dark ? rgba(c.gray50) : rgba(c.gray200),
        .flowgraphNodeBg                       = dark ? rgba(c.gray200) : rgba(c.gray50),
        .flowgraphNodeBorder                   = rgba(c.gray400),
        .flowgraphSubgraphBorder               = rgba(c.orange500),
        .flowgraphSubgraphBorderText           = rgba(c.gray50),
        .flowgraphUiControlFill                = rgba(c.blue400, 0x33),
        .flowgraphUiControlHighlight           = rgba(c.blue500),
        .flowgraphUiControlConnectionIndicator = rgba(c.gray800),
        .flowgraphUiControlPendingConnection   = rgba(c.blue500),
        .flowgraphUiControlUnlink              = rgba(c.red500),
        .flowgraphUiControlUnlinkPressed       = rgba(c.red700),

        .flowgraphBoundingBoxExteriorSelection               = rgba(c.blue400, 0x22),
        .flowgraphBoundingBoxExteriorSelectionOutline        = rgba(c.blue500),
        .flowgraphBoundingBoxExteriorSelectionHovered        = rgba(c.blue400, 0x44),
        .flowgraphBoundingBoxExteriorSelectionOutlineHovered = rgba(c.blue700),

        .flowgraphGrid                = rgba(c.gray400, 0x80),
        .flowgraphNodeBorderHovered   = rgba(c.gray600),
        .flowgraphNodeBorderSelected  = rgba(c.blue500),
        .flowgraphSelectionRect       = rgba(c.blue400, 0x26),
        .flowgraphSelectionRectBorder = rgba(c.blue500, 0x99),
        .flowgraphLinkHovered         = rgba(c.blue400),
        .flowgraphLinkSelected        = rgba(c.blue500),
        .flowgraphLinkHighlighted     = rgba(c.orange500),
        .flowgraphPinRect             = rgba(c.blue400, 0x40),
        .flowgraphPinRectBorder       = rgba(c.blue500, 0x80),

        .rowBgAlt                   = rgba(c.gray200),
        .highlightedSearchResultsBg = rgba(c.blue400, 0x55),
        .errorColor                 = rgba(c.red500),
        .warningColor               = rgba(c.orange500),
        .successColor               = rgba(c.green500),
        .currentDashboardPanelBg    = dark ? rgba(c.gray200) : rgba(c.gray50),
        .clearColor                 = rgba(c.gray100),

        .layoutModePanel        = rgba(c.blue400, 0x40),
        .layoutModePanelHovered = rgba(c.blue500, 0x66),
        .layoutModePanelGrabbed = rgba(c.green400, 0x80),

        .appHeaderFAIRLogoButton        = {},
        .appHeaderFAIRLogoButtonHovered = rgba(c.gray200),
        .appHeaderFAIRLogoButtonActive  = {},
        .headerMenuButton               = rgba(c.blue500),
        .devMenuIcon                    = dark ? std::make_optional(rgba(c.gray700)) : std::nullopt,
        .devMenuButton                  = rgba(c.blue500),
        .popupMenuButtonIcon            = dark ? std::nullopt : std::make_optional(white),
        .devMenuQuitButton              = rgba(c.red500),

        .legendButton        = {0, 0, 0, 0},
        .legendButtonHovered = rgba(c.gray900, 0x14),
        .legendButtonActive  = rgba(c.gray900, 0x29),

        .keypadButtonColor                      = rgba(c.blue500),
        .keypadTextColor                        = white,
        .keypadFunctionKeyButtonColorMultiplier = {.9f, .9f, 1.f, 1.f},

        .notificationSuccess = rgba(c.green500),
        .notificationWarning = rgba(c.orange500),
        .notificationError   = rgba(c.red500),
        .notificationInfo    = rgba(c.blue500),
        .notificationDefault = rgba(c.gray800),

        .chartTagFishy          = rgba(c.magenta500),
        .chartTagDroppedSamples = rgba(c.red500),
    };
}

void applySpectrumColours(ImGuiStyle& imStyle, LookAndFeel::Style style) {
    const bool                         dark             = style == LookAndFeel::Style::Dark;
    const SpectrumSchemeKeywordColors& c                = spectrumRamp(style);
    ImVec4*                            col              = imStyle.Colors;
    const ImVec4                       highlight        = rgba(c.blue400, 0x55);
    const ImVec4                       hoveredHighlight = rgba(c.blue400, 0x88);

    col[ImGuiCol_Text]                      = rgba(c.gray800);
    col[ImGuiCol_TextDisabled]              = rgba(c.gray500);
    col[ImGuiCol_WindowBg]                  = rgba(c.gray100);
    col[ImGuiCol_ChildBg]                   = rgba(c.gray100, 0x00);
    col[ImGuiCol_PopupBg]                   = rgba(c.gray50);
    col[ImGuiCol_Border]                    = rgba(c.gray300);
    col[ImGuiCol_BorderShadow]              = rgba(c.gray300, 0x00);
    col[ImGuiCol_FrameBg]                   = rgba(c.gray75);
    col[ImGuiCol_FrameBgHovered]            = rgba(c.gray50);
    col[ImGuiCol_FrameBgActive]             = rgba(c.gray200);
    col[ImGuiCol_TitleBg]                   = rgba(c.gray300);
    col[ImGuiCol_TitleBgActive]             = rgba(c.gray200);
    col[ImGuiCol_TitleBgCollapsed]          = rgba(c.gray400);
    col[ImGuiCol_MenuBarBg]                 = rgba(c.gray100);
    col[ImGuiCol_ScrollbarBg]               = rgba(c.gray100);
    col[ImGuiCol_ScrollbarGrab]             = rgba(c.gray400);
    col[ImGuiCol_ScrollbarGrabHovered]      = rgba(c.gray600);
    col[ImGuiCol_ScrollbarGrabActive]       = rgba(c.gray700);
    col[ImGuiCol_CheckMark]                 = rgba(c.blue500);
    col[ImGuiCol_SliderGrab]                = highlight;
    col[ImGuiCol_SliderGrabActive]          = rgba(c.gray800);
    col[ImGuiCol_Button]                    = highlight;
    col[ImGuiCol_ButtonHovered]             = hoveredHighlight;
    col[ImGuiCol_ButtonActive]              = rgba(c.gray200);
    col[ImGuiCol_Header]                    = highlight;
    col[ImGuiCol_HeaderHovered]             = hoveredHighlight;
    col[ImGuiCol_HeaderActive]              = rgba(c.gray300);
    col[ImGuiCol_Separator]                 = rgba(c.gray400);
    col[ImGuiCol_SeparatorHovered]          = rgba(c.gray600);
    col[ImGuiCol_SeparatorActive]           = rgba(c.gray700);
    col[ImGuiCol_ResizeGrip]                = rgba(c.gray400);
    col[ImGuiCol_ResizeGripHovered]         = rgba(c.gray600);
    col[ImGuiCol_ResizeGripActive]          = rgba(c.gray700);
    col[ImGuiCol_InputTextCursor]           = rgba(c.gray900);
    col[ImGuiCol_TabHovered]                = rgba(c.gray50);
    col[ImGuiCol_Tab]                       = rgba(c.gray200);
    col[ImGuiCol_TabSelected]               = rgba(c.gray50);
    col[ImGuiCol_TabSelectedOverline]       = rgba(c.blue500);
    col[ImGuiCol_TabDimmed]                 = rgba(c.gray200);
    col[ImGuiCol_TabDimmedSelected]         = rgba(c.gray75);
    col[ImGuiCol_TabDimmedSelectedOverline] = rgba(c.gray500);
    col[ImGuiCol_DockingPreview]            = rgba(c.blue400, 0x66);
    col[ImGuiCol_DockingEmptyBg]            = rgba(c.gray200);
    col[ImGuiCol_PlotLines]                 = rgba(c.blue400);
    col[ImGuiCol_PlotLinesHovered]          = rgba(c.blue600);
    col[ImGuiCol_PlotHistogram]             = rgba(c.blue400);
    col[ImGuiCol_PlotHistogramHovered]      = rgba(c.blue600);
    col[ImGuiCol_TableHeaderBg]             = rgba(c.gray200);
    col[ImGuiCol_TableBorderStrong]         = rgba(c.gray400);
    col[ImGuiCol_TableBorderLight]          = rgba(c.gray300);
    col[ImGuiCol_TableRowBg]                = rgba(c.gray50, 0x00);
    col[ImGuiCol_TableRowBgAlt]             = rgba(c.gray900, 0x08);
    col[ImGuiCol_TextLink]                  = rgba(c.blue600);
    col[ImGuiCol_TextSelectedBg]            = rgba(c.blue400, 0x33);
    col[ImGuiCol_TreeLines]                 = rgba(c.gray400);
    col[ImGuiCol_DragDropTarget]            = rgba(c.blue400);
    col[ImGuiCol_DragDropTargetBg]          = rgba(c.blue400, 0x1A);
    col[ImGuiCol_UnsavedMarker]             = rgba(c.orange500);
    col[ImGuiCol_NavCursor]                 = rgba(c.blue400);
    col[ImGuiCol_NavWindowingHighlight]     = {1.f, 1.f, 1.f, .7f};
    col[ImGuiCol_NavWindowingDimBg]         = {.8f, .8f, .8f, .2f};
    col[ImGuiCol_ModalWindowDimBg]          = {0.f, 0.f, 0.f, dark ? .6f : .4f};
}

void applySpectrumPlotColours(ImPlotStyle& plotStyle, LookAndFeel::Style style) {
    const SpectrumSchemeKeywordColors& c   = spectrumRamp(style);
    ImVec4*                            col = plotStyle.Colors;

    col[ImPlotCol_PlotBorder]   = rgba(c.gray300);
    col[ImPlotCol_LegendBg]     = rgba(c.gray50, 0xE6);
    col[ImPlotCol_LegendBorder] = rgba(c.gray300);
    col[ImPlotCol_LegendText]   = rgba(c.gray800);
    col[ImPlotCol_TitleText]    = rgba(c.gray900);
    col[ImPlotCol_InlayText]    = rgba(c.gray800);
    col[ImPlotCol_AxisText]     = rgba(c.gray700);
    col[ImPlotCol_AxisGrid]     = rgba(c.gray400);
    col[ImPlotCol_AxisTick]     = rgba(c.gray500);
    col[ImPlotCol_Selection]    = rgba(c.blue500);
    col[ImPlotCol_Crosshairs]   = rgba(c.gray800, 0x80);
}
} // namespace

const SpectrumSchemeKeywordColors& LookAndFeel::spectrumColours() const noexcept { return spectrumRamp(style); }

const Palette& LookAndFeel::palette() const noexcept {
    static const Palette classicLight  = classicPalette(Style::Light);
    static const Palette classicDark   = classicPalette(Style::Dark);
    static const Palette spectrumLight = spectrumPalette(Style::Light);
    static const Palette spectrumDark  = spectrumPalette(Style::Dark);
    switch (theme) {
    case Theme::Classic: return style == Style::Light ? classicLight : classicDark;
    case Theme::Advanced: return style == Style::Light ? spectrumLight : spectrumDark;
    }
    std::unreachable();
}

void LookAndFeel::applyTheme() {
    const bool        classic = theme == Theme::Classic;
    const auto        pick    = [classic]<typename T>(T classicValue, T themedValue) { return classic ? classicValue : themedValue; };
    const ImGuiStyle  imDefaults;
    const LookAndFeel lookAndFeelDefaults;

    ImGuiStyle& im             = ImGui::GetStyle();
    im.WindowPadding           = pick(imDefaults.WindowPadding, ImVec2{10.f, 10.f});
    im.WindowRounding          = pick(imDefaults.WindowRounding, 4.f);
    im.ChildRounding           = pick(imDefaults.ChildRounding, 4.f);
    im.PopupRounding           = pick(imDefaults.PopupRounding, 4.f);
    im.FramePadding            = pick(imDefaults.FramePadding, ImVec2{8.f, 4.f});
    im.FrameRounding           = pick(imDefaults.FrameRounding, 4.f);
    im.FrameBorderSize         = pick(imDefaults.FrameBorderSize, 1.f);
    im.ItemSpacing             = pick(imDefaults.ItemSpacing, ImVec2{8.f, 6.f});
    im.ItemInnerSpacing        = pick(imDefaults.ItemInnerSpacing, ImVec2{6.f, 4.f});
    im.CellPadding             = pick(imDefaults.CellPadding, ImVec2{6.f, 4.f});
    im.ScrollbarSize           = pick(imDefaults.ScrollbarSize, 12.f);
    im.ScrollbarRounding       = pick(imDefaults.ScrollbarRounding, 6.f);
    im.ScrollbarPadding        = pick(imDefaults.ScrollbarPadding, 3.f);
    im.GrabRounding            = pick(imDefaults.GrabRounding, 4.f);
    im.ImageRounding           = pick(imDefaults.ImageRounding, 4.f);
    im.TabRounding             = pick(imDefaults.TabRounding, 4.f);
    im.TabBarOverlineSize      = pick(imDefaults.TabBarOverlineSize, 2.f);
    im.TreeLinesRounding       = pick(imDefaults.TreeLinesRounding, 4.f);
    im.DragDropTargetRounding  = pick(imDefaults.DragDropTargetRounding, 4.f);
    im.SeparatorTextBorderSize = pick(imDefaults.SeparatorTextBorderSize, 1.f);
    im.DisabledAlpha           = pick(imDefaults.DisabledAlpha, .45f);

    if (classic) {
        style == Style::Dark ? ImGui::StyleColorsDark(&im) : ImGui::StyleColorsLight(&im);
    } else {
        applySpectrumColours(im, style);
    }
    im.Colors[ImGuiCol_WindowBg].w = 1.f;

    ImPlotStyle& plot = ImPlot::GetStyle();
    ImPlot::StyleColorsAuto(&plot);
    if (!classic) {
        applySpectrumPlotColours(plot, style);
    }
    // dark style: the plot frame would match a button
    plot.Colors[ImPlotCol_FrameBg] = im.Colors[ImGuiCol_WindowBg];

    const Flowgraph& fgDefaults           = lookAndFeelDefaults.flowgraph;
    flowgraph.nodeRounding                = pick(fgDefaults.nodeRounding, 4.f);
    flowgraph.nodeBorderWidth             = pick(fgDefaults.nodeBorderWidth, 1.f);
    flowgraph.subgraphBorderWidth         = pick(fgDefaults.subgraphBorderWidth, 2.f);
    flowgraph.imguiNodeEditorPortRounding = pick(fgDefaults.imguiNodeEditorPortRounding, 2.f);
    flowgraph.rectangularPortRounding     = pick(fgDefaults.rectangularPortRounding, 2.f);
    flowgraph.previewBorderThickness      = pick(fgDefaults.previewBorderThickness, 1.f);
    keypadRounding                        = pick(lookAndFeelDefaults.keypadRounding, 4.f);
    popupMenuButtonRounding               = pick(lookAndFeelDefaults.popupMenuButtonRounding, 4.f);
    notificationRounding                  = pick(lookAndFeelDefaults.notificationRounding, 4.f);
}

[[nodiscard]] float LookAndFeel::mainWindowIconButtonSize() const noexcept { return fontSize<3>(*this) * 1.7f; }

void LookAndFeel::loadFonts() {
    // static const ImWchar fullRange[] = {
    //     0x0020, 0XFFFF, 0, 0 // '0', '0' are the end marker
    //     // N.B. a bit unsafe but in imgui_draw.cpp::ImFontAtlasBuildWithStbTruetype break condition is:
    //     // 'for (const ImWchar* src_range = src_tmp.SrcRanges; src_range[0] && src_range[1]; src_range += 2)'
    // };
    static const std::vector<ImWchar> rangeLatin             = {0x0020, 0x0080, // Basic Latin
                    0, 0};
    static const std::vector<ImWchar> rangeLatinExtended     = {0x80, 0xFFFF, 0}; // Latin-1 Supplement
    static const std::vector<ImWchar> rangeLatinPlusExtended = {0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement (standard + extended ASCII)
        0x2200, 0x22FF,                                                           // Mathematical Operators (√, ∑, ∫, ≤, ≥, ≠, ∞, etc.)
        0, 0};
    static const ImWchar              glyphRanges[]          = {// pick individual glyphs and specific sub-ranges rather than full range
        0XF002, 0XF2ED,                   // 0xf002 is "magnifying glass", 0xf005 is "", 0xf2ed is "trash can"
        0X2B, 0X2B,                       // plus
        0XF055, 0XF055,                   // circle-plus
        0XF201, 0XF83E,                   // fa-chart-line, fa-wave-square
        0xF58D, 0xF58D,                   // grid layout
        0XF7A5, 0XF7A5,                   // horizontal layout,
        0xF248, 0xF248,                   // free layout,
        0XF7A4, 0XF7A4,                   // vertical layout
        0XEF808D, 0XEF808D,               // notification ICON_FA_XMARK
        0XEF8198, 0XEF8198,               // notification ICON_FA_CIRCLE_CHECK
        0XEF81B1, 0XEF81B1,               // notification ICON_FA_TRIANGLE_EXCLAMATION
        0XEF81AA, 0XEF81AA,               // notification ICON_FA_CIRCLE_EXCLAMATION
        0XEF819A, 0XEF819A,               // notification ICON_FA_CIRCLE_INFO
        0, 0};

    static ImFontConfig config;
    // Originally oversampling of 4 was used to ensure good looking text for all zoom levels, but this led to huge texture atlas sizes, which did not work on mobile
    config.OversampleH          = 2;
    config.OversampleV          = 2;
    config.PixelSnapH           = true;
    config.FontDataOwnedByAtlas = false;

    auto loadDefaultFont = [](auto primaryFont, auto secondaryFont, std::size_t index, const std::vector<ImWchar>& primaryRanges = {}, const std::vector<ImWchar>& secondaryRanges = {}) {
        auto loadFont = [&primaryFont, &secondaryFont, &primaryRanges, &secondaryRanges](float loadFontSize) {
            const auto resultFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(primaryFont.begin()), static_cast<int>(primaryFont.size()), loadFontSize, &config, primaryRanges.empty() ? nullptr : primaryRanges.data());
            if (!secondaryRanges.empty()) {
                config.MergeMode = true;
                ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(secondaryFont.begin()), static_cast<int>(secondaryFont.size()), loadFontSize, &config, secondaryRanges.data());
                config.MergeMode = false;
            }
            return resultFont;
        };

        auto& lookAndFeel              = LookAndFeel::mutableInstance();
        lookAndFeel._fontTiny[index]   = loadFont(fontSize<FontSizeTiny>(lookAndFeel));
        lookAndFeel._fontSmall[index]  = loadFont(fontSize<FontSizeSmall>(lookAndFeel));
        lookAndFeel._fontNormal[index] = loadFont(fontSize<FontSizeNormal>(lookAndFeel));
        lookAndFeel._fontBig[index]    = loadFont(fontSize<FontSizeBig>(lookAndFeel));
        lookAndFeel._fontBigger[index] = loadFont(fontSize<FontSizeBigger>(lookAndFeel));
        lookAndFeel._fontLarge[index]  = loadFont(fontSize<FontSizeLarge>(lookAndFeel));
    };

    loadDefaultFont(cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 0, rangeLatinPlusExtended);
    loadDefaultFont(cmrc::ui_assets::get_filesystem().open("assets/xkcd/xkcd-script.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 1, {}, rangeLatinExtended);
    ImGui::GetIO().FontDefault = LookAndFeel::instance().fontNormal();

    auto loadIconsFont = [](auto name, float iconFontSize) {
        auto file = cmrc::ui_assets::get_filesystem().open(name);
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(file.begin()), static_cast<int>(file.size()), iconFontSize, &config, glyphRanges); // alt: fullRange
    };

    auto& lookAndFeel               = LookAndFeel::mutableInstance();
    lookAndFeel.fontIcons           = loadIconsFont("assets/fontawesome/fa-regular-400.otf", fontSize<0>(lookAndFeel));
    lookAndFeel.fontIconsBig        = loadIconsFont("assets/fontawesome/fa-regular-400.otf", fontSize<1>(lookAndFeel));
    lookAndFeel.fontIconsLarge      = loadIconsFont("assets/fontawesome/fa-regular-400.otf", fontSize<3>(lookAndFeel));
    lookAndFeel.fontIconsSolid      = loadIconsFont("assets/fontawesome/fa-solid-900.otf", fontSize<0>(lookAndFeel));
    lookAndFeel.fontIconsSolidBig   = loadIconsFont("assets/fontawesome/fa-solid-900.otf", fontSize<1>(lookAndFeel));
    lookAndFeel.fontIconsSolidLarge = loadIconsFont("assets/fontawesome/fa-solid-900.otf", fontSize<3>(lookAndFeel));
}
} // namespace DigitizerUi
