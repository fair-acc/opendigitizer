#ifndef OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_
// #define STB_IMAGE_IMPLEMENTATION

#include <chrono>
#include <string_view>

#include <cmrc/cmrc.hpp>
#include <format>

#include "../common/AppDefinitions.hpp"
#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include "../components/ImGuiNotify.hpp"

#include <SDL3/SDL_opengl.h>
#include <magic_enum.hpp>
#include <magic_enum_utility.hpp>
#include <stb_image.h>

#include "PopupMenu.hpp"
#include "settings.hpp"

CMRC_DECLARE(ui_assets);

namespace DigitizerUi::components {
namespace detail {

inline void TextCentered(const std::string_view text) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.data());
}

inline void TextRight(const std::string_view text, float rightInset) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX(windowWidth - textWidth - rightInset);
    ImGui::Text("%s", text.data());
}

inline bool LoadTextureFromFile(const char* filename, GLuint* out_texture, ImVec2& textureSize) {
    // Load from file
    int image_width  = 0;
    int image_height = 0;

    auto fs   = cmrc::ui_assets::get_filesystem();
    auto file = fs.open(filename);

    unsigned char* image_data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.begin()), static_cast<int>(file.size()), &image_width, &image_height, nullptr, 4);
    if (image_data == NULL) {
        return false;
    }

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture  = image_texture;
    textureSize.x = static_cast<float>(image_width);
    textureSize.y = static_cast<float>(image_height);

    return true;
}
} // namespace detail

class AppHeader {
public:
    std::function<void()>                   requestApplicationStop;
    std::function<void(ViewMode)>           requestApplicationSwitchMode;
    std::function<void(LookAndFeel::Style)> requestApplicationSwitchStyle;
    std::function<void(LookAndFeel::Theme)> requestApplicationSwitchTheme;

    ImVec2 logoSize{0.f, 0.f};
    GLuint imgFairLogo     = 0;
    GLuint imgFairLogoDark = 0;

    void loadAssets() {
        [[maybe_unused]] bool ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi.png", &imgFairLogo, logoSize);
        IM_ASSERT(ret);

        ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png", &imgFairLogoDark, logoSize);
        IM_ASSERT(ret);
    }

    void draw(std::string_view title, ImFont* title_font, LookAndFeel::Style style_) {
        using namespace detail;

        const auto  now           = std::chrono::round<std::chrono::seconds>(std::chrono::system_clock::now());
        const auto  timeT         = std::chrono::system_clock::to_time_t(now);
        std::tm     localTm       = *std::localtime(&timeT);
        std::tm     utcTm         = *std::gmtime(&timeT);
        std::string localClock    = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} (LOC)", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday, localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
        std::string utcClock      = std::format("{:02}:{:02}:{:02} (UTC)", utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
        const auto  utcStringSize = ImGui::CalcTextSize(utcClock.c_str());

        const auto& settings = Digitizer::Settings::instance();
        const auto& palette  = DigitizerUi::LookAndFeel::instance().palette();

        const auto  topLeft            = ImGui::GetCursorPos();
        const float headerHeight       = 2.f * ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y; // hardcodes that the largest item in the header is date/time text display
        const auto  verticallyCentredY = [&](float itemHeight) { return topLeft.y + .5f * (headerHeight - itemHeight); };

        ImFont*     headerIconFont = LookAndFeel::instance().fontIconsSolidLarge;
        const float iconButtonSize = VerticalPopupMenu<1>::calcAddButtonIconButtonSize(headerIconFont);

        const auto headerIconButton = [&](const char* label, ImVec4 iconColor) {
            IMW::StyleColor    buttonStyle(ImGuiCol_Button, palette.mainWindowButtonBgInactive);
            IMW::StyleColor    textStyle(ImGuiCol_Text, iconColor);
            IMW::StyleColor    buttonActiveStyle(ImGuiCol_ButtonActive, palette.mainWindowButtonBgActive);
            IMW::StyleColor    buttonHoveredStyle(ImGuiCol_ButtonHovered, palette.mainWindowButtonBgHovered);
            IMW::StyleFloatVar rounding(ImGuiStyleVar_FrameRounding, LookAndFeel::instance().popupMenuButtonRounding);
            IMW::Font          font(headerIconFont);
            return ImGui::Button(label, ImVec2{iconButtonSize, iconButtonSize});
        };

        const bool   mouseMoved    = ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0;
        static float buttonTimeOut = 0;
        buttonTimeOut -= std::max(ImGui::GetIO().DeltaTime, 0.f);
        if (mouseMoved) {
            buttonTimeOut = DigitizerUi::LookAndFeel::instance().devMenuHideDelaySeconds;
        }
        const bool showDevMenuButton = settings.editableMode && (mouseMoved || buttonTimeOut > 0.f);

        constexpr const char* kDevMenuIcon    = "";
        const float           devMenuButtonX  = ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - iconButtonSize;
        const float           clockRightInset = showDevMenuButton ? ImGui::GetWindowSize().x - devMenuButtonX + ImGui::GetStyle().ItemSpacing.x : ImGui::GetStyle().ItemSpacing.x;

        // draw title
        const ImVec2 localLogoSize = logoSize * (headerHeight / logoSize.y);
        {
            IMW::Font  font(title_font);
            const auto titleSize = ImGui::CalcTextSize(title.data());
            // suppress title if it doesn't fit or is likely to overlap
            if (0.5f * ImGui::GetIO().DisplaySize.x > (0.5f * titleSize.x + utcStringSize.x)) {
                TextCentered(title);
            }
        }

        ImGui::SameLine();
        auto pos = ImGui::GetCursorPos();
        TextRight(localClock, clockRightInset);
        pos.y += ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPos(pos);
        TextRight(utcClock, clockRightInset);
        auto posBeneathClock = ImGui::GetCursorPos();

        // left menu
        VerticalPopupMenu<1> leftMenu;
        ImGui::SetCursorPos(ImVec2{topLeft.x, verticallyCentredY(iconButtonSize)});

        const auto menuButtonPushed = settings.editableMode && headerIconButton(LookAndFeel::instance().prototypeMode ? "" : "", palette.mainWindowButtonIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)));

        if ((menuButtonPushed || ImGui::IsItemHovered()) && settings.editableMode) {
            using DigitizerUi::MenuButton;
            const bool wasAlreadyOpen = leftMenu.isOpen();

            leftMenu.addButton("\uF201", [this]() { requestApplicationSwitchMode(ViewMode::INTERACTION); }, headerIconFont, "switch to chart view", palette.headerMenuButton, palette.popupMenuButtonIcon);
            leftMenu.addButton("\uF542", [this]() { requestApplicationSwitchMode(ViewMode::FLOWGRAPH); }, headerIconFont, "click to edit flow-graph", palette.headerMenuButton, palette.popupMenuButtonIcon);
            leftMenu.addButton("", [this]() { requestApplicationSwitchMode(ViewMode::OPEN_SAVE_DASHBOARD); }, headerIconFont, "click to open/save new dashboards", palette.headerMenuButton, palette.popupMenuButtonIcon);

            if (wasAlreadyOpen && !ImGui::IsItemHovered()) {
                std::print("was already open -> closing\n");
                leftMenu.forceClose();
            }
        }

        // draw fair logo
        const float logoX = settings.editableMode ? topLeft.x + iconButtonSize + LookAndFeel::instance().appHeaderMenuToLogoGap : topLeft.x;
        ImGui::SetCursorPos(ImVec2{logoX, verticallyCentredY(localLogoSize.y)});
        {
            IMW::StyleColor normalStyle(ImGuiCol_Button, palette.appHeaderFAIRLogoButton);
            IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, palette.appHeaderFAIRLogoButtonHovered);
            IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, palette.appHeaderFAIRLogoButtonActive);
            IMW::StyleVar   noPadding(ImGuiStyleVar_FramePadding, ImVec2{});
            const auto      imgLogo = static_cast<ImTextureID>(style_ == LookAndFeel::Style::Light ? imgFairLogo : imgFairLogoDark);
            if (ImGui::ImageButton("logo", imgLogo, localLogoSize)) {
                // call url to project site
            }
        }

        // right menu
        const float           rightMenuStartAngle = LookAndFeel::instance().appHeaderRightMenuStartDegrees;
        const float           rightMenuStopAngle  = LookAndFeel::instance().appHeaderRightMenuStopDegrees;
        RadialCircularMenu<2> rightMenu(localLogoSize, rightMenuStartAngle, rightMenuStopAngle);
        ImGui::SetCursorPos(ImVec2(devMenuButtonX, verticallyCentredY(iconButtonSize)));

        const bool devMenuButtonPushed = showDevMenuButton && [&] {
            ImVec4 iconColor = palette.devMenuIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text));
            iconColor.w      = LookAndFeel::instance().devMenuButtonAlpha;
            return headerIconButton(kDevMenuIcon, iconColor);
        }();

        // check showDevMenuButton first to make sure that IsItemHovered is referring to the dev menu and not some other item in the header
        if (showDevMenuButton && (devMenuButtonPushed || ImGui::IsItemHovered())) {
            using enum WindowMode;

            {
                rightMenu.addButton(
                    LookAndFeel::instance().windowMode == FULLSCREEN ? "\uF066" : "\uF065",
                    [&rightMenu](MenuButton& button) {
                        LookAndFeel::mutableInstance().windowMode = LookAndFeel::instance().windowMode == FULLSCREEN ? RESTORED : FULLSCREEN;
                        button.label                              = LookAndFeel::instance().windowMode == FULLSCREEN ? "\uF066" : "\uF065";
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsSolidLarge, "toggle between fullscreen and windowed mode", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            {
                using enum LookAndFeel::Style;
                rightMenu.addButton((LookAndFeel::instance().style == Light) ? "" : "",
                    [this](MenuButton& button) {
                        const bool isDarkMode = LookAndFeel::instance().style == Dark;
                        requestApplicationSwitchStyle(isDarkMode ? Light : Dark);
                        button.label   = isDarkMode ? "" : "";
                        button.toolTip = isDarkMode ? "switch to dark mode" : "switch to light mode";
                    },
                    LookAndFeel::instance().fontIconsSolidBig, LookAndFeel::instance().style == Dark ? "switch to light mode" : "switch to dark mode", palette.devMenuButton, palette.popupMenuButtonIcon);

                rightMenu.addButton(
                    "\uF1FC",
                    [this](MenuButton& button) {
                        const auto next = magic_enum::enum_next_value_circular(LookAndFeel::instance().theme);
                        requestApplicationSwitchTheme(next);
                        button.toolTip = std::format("switch to the {} theme", magic_enum::enum_name(magic_enum::enum_next_value_circular(next)));
                    },
                    LookAndFeel::instance().fontIconsSolidBig, std::format("switch to the {} theme", magic_enum::enum_name(magic_enum::enum_next_value_circular(LookAndFeel::instance().theme))), palette.devMenuButton, palette.popupMenuButtonIcon);

                rightMenu.addButton(
                    LookAndFeel::instance().prototypeMode ? "" : "",
                    [](MenuButton& button) {
                        LookAndFeel::mutableInstance().prototypeMode = !LookAndFeel::instance().prototypeMode;
                        button.label                                 = LookAndFeel::instance().prototypeMode ? "" : "";
                        ImGui::GetIO().FontDefault                   = LookAndFeel::instance().fontNormal();
                    },
                    LookAndFeel::instance().fontIconsSolidBig, "switch between prototype and production mode", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            if (LookAndFeel::instance().isDesktop) {
                rightMenu.addButton("", []() { LookAndFeel::mutableInstance().windowMode = MINIMISED; }, LookAndFeel::instance().fontIconsSolidBig, "minimise window", palette.devMenuButton, palette.popupMenuButtonIcon);
                rightMenu.addButton(
                    LookAndFeel::instance().windowMode == RESTORED ? "" : "",
                    [&rightMenu](MenuButton& button) {
                        LookAndFeel::mutableInstance().windowMode = LookAndFeel::instance().windowMode == MAXIMISED ? RESTORED : MAXIMISED;
                        button.label                              = LookAndFeel::instance().windowMode == RESTORED ? "" : "";
                        button.toolTip                            = LookAndFeel::instance().windowMode == MAXIMISED ? "restore window" : "maximise window";
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsSolidBig, LookAndFeel::instance().windowMode == MAXIMISED ? "restore window" : "maximise window", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            {
#ifdef __EMSCRIPTEN__
                constexpr bool newLine = false;
#else
                constexpr bool newLine = true;
#endif
                rightMenu.addButton<false, newLine>(
                    "",
                    [](MenuButton& button) {
                        TouchHandler<>::diagnostics = !TouchHandler<>::diagnostics;
                        button.font                 = TouchHandler<>::diagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig;
                        button.toolTip              = TouchHandler<>::diagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics";
                    },
                    TouchHandler<>::diagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig, TouchHandler<>::diagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            if (LookAndFeel::instance().isDesktop) {
                rightMenu.addButton(
                    "",
                    [this, &rightMenu]() {
                        std::print("requesting exit\n");
                        requestApplicationStop();
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsBig, "close app", palette.devMenuQuitButton, palette.popupMenuButtonIcon);
            }
        }

        posBeneathClock.x = 0.f;
        ImGui::SetCursorPos(posBeneathClock); // set to end of image
    }
};

} // namespace DigitizerUi::components

#endif
