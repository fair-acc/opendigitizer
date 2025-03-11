#include "LookAndFeel.hpp"

#include "ImguiWrap.hpp"

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(fonts);
CMRC_DECLARE(ui_assets);

namespace DigitizerUi {

LookAndFeel& LookAndFeel::mutableInstance() {
    static LookAndFeel s_instance;
    return s_instance;
}

const LookAndFeel& LookAndFeel::instance() { return mutableInstance(); }

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

    static const auto fontSize = []() -> std::array<float, 4> {
        if (std::abs(LookAndFeel::instance().verticalDPI - LookAndFeel::instance().defaultDPI) < 8.f) {
            return {20, 24, 28, 46}; // 28" monitor
        } else if (LookAndFeel::instance().verticalDPI > 200) {
            return {16, 22, 23, 38}; // likely mobile monitor
        } else if (std::abs(LookAndFeel::instance().defaultDPI - LookAndFeel::instance().verticalDPI) >= 8.f) {
            return {22, 26, 30, 46}; // likely large fixed display monitor
        }
        return {18, 24, 26, 46}; // default
    }();

    static ImFontConfig config;
    // Originally oversampling of 4 was used to ensure good looking text for all zoom levels, but this led to huge texture atlas sizes, which did not work on mobile
    config.OversampleH          = 2;
    config.OversampleV          = 2;
    config.PixelSnapH           = true;
    config.FontDataOwnedByAtlas = false;

    auto loadDefaultFont = [](auto primaryFont, auto secondaryFont, std::size_t index, const std::vector<ImWchar>& ranges = {}) {
        auto loadFont = [&primaryFont, &secondaryFont, &ranges](float loadFontSize) {
            const auto resultFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(primaryFont.begin()), int(primaryFont.size()), loadFontSize, &config);
            if (!ranges.empty()) {
                config.MergeMode = true;
                ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(secondaryFont.begin()), int(secondaryFont.size()), loadFontSize, &config, ranges.data());
                config.MergeMode = false;
            }
            return resultFont;
        };

        auto& lookAndFeel             = LookAndFeel::mutableInstance();
        lookAndFeel.fontNormal[index] = loadFont(fontSize[0]);
        lookAndFeel.fontBig[index]    = loadFont(fontSize[1]);
        lookAndFeel.fontBigger[index] = loadFont(fontSize[2]);
        lookAndFeel.fontLarge[index]  = loadFont(fontSize[3]);
    };

    loadDefaultFont(cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 0);
    loadDefaultFont(cmrc::ui_assets::get_filesystem().open("assets/xkcd/xkcd-script.ttf"), cmrc::fonts::get_filesystem().open("Roboto-Medium.ttf"), 1, rangeLatinExtended);
    ImGui::GetIO().FontDefault = LookAndFeel::instance().fontNormal[LookAndFeel::instance().prototypeMode];

    auto loadIconsFont = [](auto name, float iconFontSize) {
        auto file = cmrc::ui_assets::get_filesystem().open(name);
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<char*>(file.begin()), static_cast<int>(file.size()), iconFontSize, &config, glyphRanges); // alt: fullRange
    };

    auto& lookAndFeel               = LookAndFeel::mutableInstance();
    lookAndFeel.fontIcons           = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 12);
    lookAndFeel.fontIconsBig        = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 18);
    lookAndFeel.fontIconsLarge      = loadIconsFont("assets/fontawesome/fa-regular-400.otf", 36);
    lookAndFeel.fontIconsSolid      = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 12);
    lookAndFeel.fontIconsSolidBig   = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 18);
    lookAndFeel.fontIconsSolidLarge = loadIconsFont("assets/fontawesome/fa-solid-900.otf", 36);
}
} // namespace DigitizerUi
