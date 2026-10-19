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

        .mainWindowButtonIcon       = dark ? rgba(0xCCCCCC) : rgba(0xCCCCCC, 0x99),
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
        .contentSeparator           = dark ? rgba(0xFFFFFF, 0x33) : rgba(0x000000, 0x33),
        .clearColor                 = white,

        .layoutModePanel        = rgba(0x9EF7F1, overlayAlpha),
        .layoutModePanelHovered = rgba(0x6EC9CF, overlayAlpha),
        .layoutModePanelGrabbed = rgba(0x63E69F, overlayAlpha),

        .appHeaderFAIRLogoButton        = {0, 0, 0, 0},
        .appHeaderFAIRLogoButtonHovered = {.8f, .8f, .8f, .4f},
        .appHeaderFAIRLogoButtonActive  = {0, 0, 0, 0},
        .headerMenuButton               = rgba(0x7EBC89),
        .devMenuIcon                    = {.8f, .8f, .8f, 1.f},
        .devMenuButton                  = {.3f, .3f, 1.f, 1.f},
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
        .notificationInfo    = rgba(0x00FFFF),
        .notificationDefault = white,

        .chartTagFishy          = rgba(0xFF00FF),
        .chartTagDroppedSamples = {1.f, .2f, .2f, 1.f},
    };
}
} // namespace

const Palette& LookAndFeel::palette() const noexcept {
    static const Palette classicLight = classicPalette(Style::Light);
    static const Palette classicDark  = classicPalette(Style::Dark);
    return style == Style::Light ? classicLight : classicDark;
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
