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

const Palette& LookAndFeel::palette() const noexcept {
    static const ImVec4  quarterTransparentWhite = {1.0f, 1.0f, 1.0f, 0.25f};
    static const ImVec4  quarterTransparentBlack = {0.0f, 0.0f, 0.0f, 0.25f};
    static const Palette darkModePalette{
        .gridLines = quarterTransparentWhite,

        .mainWindowButtonIcon       = {.8f, .8f, .8f, 1.0f},
        .mainWindowButtonBgInactive = {1.f, 1.f, 1.f, 0.0f},
        .mainWindowButtonBgHovered  = {1.f, 1.f, 1.f, 0.1f},
        .mainWindowButtonBgActive   = {1.f, 1.f, 1.f, 0.2f},

        .notificationWindowBg = {0.10f, 0.10f, 0.10f, 1.00f},
        .toolbarLineColor     = quarterTransparentWhite,
        .flowgraphBg          = {0.1f, 0.1f, 0.1f, 1.f},
        .flowgraphNodeBg      = {0.2f, 0.2f, 0.2f, 1.f},
        .flowgraphNodeBorder  = {0.7f, 0.7f, 0.7f, 1.f},
    };
    static const Palette lightModePalette{
        .gridLines = quarterTransparentBlack,

        .mainWindowButtonIcon       = {.8f, .8f, .8f, 0.6f},
        .mainWindowButtonBgInactive = {0.f, 0.f, 0.f, 0.0f},
        .mainWindowButtonBgHovered  = {0.f, 0.f, 0.f, 0.1f},
        .mainWindowButtonBgActive   = {0.f, 0.f, 0.f, 0.2f},

        .notificationWindowBg = {1.00f, 1.00f, 1.00f, 1.00f},
        .toolbarLineColor     = quarterTransparentBlack,
        .flowgraphBg          = {1.f, 1.f, 1.f, 1.f},
        .flowgraphNodeBg      = {0.94f, 0.92f, 1.f, 1.f},
        .flowgraphNodeBorder  = {0.38f, 0.38f, 0.38f, 1.f},
    };

    return style == Style::Light ? lightModePalette : darkModePalette;
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
        0XF005, 0XF2ED,                   // 0xf005 is "", 0xf2ed is "trash can"
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

        auto& lookAndFeel             = LookAndFeel::mutableInstance();
        lookAndFeel.fontTiny[index]   = loadFont(fontSize<FontSizeTiny>(lookAndFeel));
        lookAndFeel.fontSmall[index]  = loadFont(fontSize<FontSizeSmall>(lookAndFeel));
        lookAndFeel.fontNormal[index] = loadFont(fontSize<FontSizeNormal>(lookAndFeel));
        lookAndFeel.fontBig[index]    = loadFont(fontSize<FontSizeBig>(lookAndFeel));
        lookAndFeel.fontBigger[index] = loadFont(fontSize<FontSizeBigger>(lookAndFeel));
        lookAndFeel.fontLarge[index]  = loadFont(fontSize<FontSizeLarge>(lookAndFeel));
    };

    loadDefaultFont(cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 0, rangeLatinPlusExtended);
    loadDefaultFont(cmrc::ui_assets::get_filesystem().open("assets/xkcd/xkcd-script.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 1, {}, rangeLatinExtended);
    ImGui::GetIO().FontDefault = LookAndFeel::instance().fontNormal[LookAndFeel::instance().prototypeMode];

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
