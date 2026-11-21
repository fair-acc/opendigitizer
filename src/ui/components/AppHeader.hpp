#ifndef OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_

#include <chrono>
#include <string_view>

#include <cmrc/cmrc.hpp>
#include <format>

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include "../common/ViewMode.hpp"
#include "../components/ImGuiNotify.hpp"

#include <SDL3/SDL_opengl.h>
#include <gnuradio-4.0/meta/reflection.hpp>
#include <magic_enum.hpp>
#include <magic_enum_utility.hpp>
#include <stb_image.h>

#include "AutoHide.hpp"
#include "PopupMenu.hpp"

CMRC_DECLARE(ui_assets);

namespace gr::od::components {
namespace detail {

inline void textCentered(const std::string_view text) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.data());
}

inline void textRight(const std::string_view text, float rightInset) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX(windowWidth - textWidth - rightInset);
    ImGui::Text("%s", text.data());
}

inline void textFitted(std::string_view text, ImFont* font, float minSize, float left, float right, auto&& topForHeight) {
    const float available = right - left;
    if (available <= 0.f) {
        return;
    }
    float size = font->LegacySize;
    {
        imw::Font   measured(font);
        const float width = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
        if (width > available) {
            size = std::max(minSize, size * available / width);
        }
    }
    imw::FontWithSize sized(font, size);
    const ImVec2      textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2      min      = ImGui::GetWindowPos() + ImVec2{left + std::max(0.f, 0.5f * (available - textSize.x)), topForHeight(textSize.y)};
    const ImVec2      max{ImGui::GetWindowPos().x + right, min.y + textSize.y};
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, text.data(), text.data() + text.size(), &textSize);
}

inline bool loadTextureFromFile(const char* filename, GLuint* outTexture, ImVec2& textureSize) {
    int imageWidth  = 0;
    int imageHeight = 0;

    auto fs   = cmrc::ui_assets::get_filesystem();
    auto file = fs.open(filename);

    unsigned char* imageData = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.begin()), static_cast<int>(file.size()), &imageWidth, &imageHeight, nullptr, 4);
    if (imageData == nullptr) {
        return false;
    }

    GLuint imageTexture;
    glGenTextures(1, &imageTexture);
    glBindTexture(GL_TEXTURE_2D, imageTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // WebGL requires clamping for non-power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, imageData);
    stbi_image_free(imageData);

    *outTexture   = imageTexture;
    textureSize.x = static_cast<float>(imageWidth);
    textureSize.y = static_cast<float>(imageHeight);

    return true;
}
} // namespace detail

struct AppHeader {
    std::function<void()>                   requestApplicationStop;
    std::function<void(ViewMode)>           requestApplicationSwitchMode;
    std::function<void(LookAndFeel::Style)> requestApplicationSwitchStyle;
    std::function<void(LookAndFeel::Theme)> requestApplicationSwitchTheme;

    bool     editable = true; // false: the dashboard is locked, without menu and developer button
    AutoHide menuAutoHide;
    AutoHide devMenuAutoHide;

    ImVec2 logoSize{0.f, 0.f};
    GLuint imgFairLogo     = 0;
    GLuint imgFairLogoDark = 0;

    void loadAssets() {
        [[maybe_unused]] bool ret = detail::loadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi.png", &imgFairLogo, logoSize);
        IM_ASSERT(ret);

        ret = detail::loadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png", &imgFairLogoDark, logoSize);
        IM_ASSERT(ret);
    }

    void draw(std::string_view title, ImFont* titleFont, LookAndFeel::Style style) {
        using namespace detail;

        const auto  now           = std::chrono::round<std::chrono::seconds>(std::chrono::system_clock::now());
        const auto  timeT         = std::chrono::system_clock::to_time_t(now);
        std::tm     localTm       = *std::localtime(&timeT);
        std::tm     utcTm         = *std::gmtime(&timeT);
        std::string localClock    = std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02} (LOC)", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday, localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
        std::string utcClock      = std::format("{:02}:{:02}:{:02} (UTC)", utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec);
        const auto  utcStringSize = ImGui::CalcTextSize(utcClock.c_str());

        const auto& palette = LookAndFeel::instance().palette();

        const auto  topLeft            = ImGui::GetCursorPos();
        const float headerHeight       = 2.f * ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y; // the clock's two text lines are the tallest item
        const auto  verticallyCentredY = [&](float itemHeight) { return topLeft.y + .5f * (headerHeight - itemHeight); };

        ImFont*     headerIconFont = LookAndFeel::instance().fontIconsSolidLarge;
        const float iconButtonSize = VerticalPopupMenu<1>::calcAddButtonIconButtonSize(headerIconFont);

        const auto headerIconButton = [&](const char* label, ImVec4 iconColor) {
            imw::StyleColor    buttonStyle(ImGuiCol_Button, palette.mainWindowButtonBgInactive);
            imw::StyleColor    textStyle(ImGuiCol_Text, iconColor);
            imw::StyleColor    buttonActiveStyle(ImGuiCol_ButtonActive, palette.mainWindowButtonBgActive);
            imw::StyleColor    buttonHoveredStyle(ImGuiCol_ButtonHovered, palette.mainWindowButtonBgHovered);
            imw::StyleFloatVar rounding(ImGuiStyleVar_FrameRounding, LookAndFeel::instance().popupMenuButtonRounding);
            imw::Font          font(headerIconFont);
            return ImGui::Button(label, ImVec2{iconButtonSize, iconButtonSize});
        };

        constexpr const char* kDevMenuIcon   = "";
        const float           devMenuButtonX = ImGui::GetWindowSize().x - ImGui::GetStyle().WindowPadding.x - iconButtonSize;
        const ImVec2          devMenuHome{devMenuButtonX, verticallyCentredY(iconButtonSize)};
        const float           devMenuHidden     = !editable ? 1.f : devMenuAutoHide.update(ImRect(ImGui::GetWindowPos() + devMenuHome, ImGui::GetWindowPos() + devMenuHome + ImVec2{iconButtonSize, iconButtonSize}));
        const bool            showDevMenuButton = devMenuHidden < 1.f;
        const float           clockRightInset   = showDevMenuButton ? ImGui::GetWindowSize().x - devMenuButtonX + ImGui::GetStyle().ItemSpacing.x : ImGui::GetStyle().ItemSpacing.x;

        const ImVec2 localLogoSize = logoSize * (headerHeight / logoSize.y);
        ImVec2       posBeneathClock;
        if (gAppState.compact) {
            const float spacing    = ImGui::GetStyle().ItemSpacing.x;
            const float titleLeft  = topLeft.x + (editable ? iconButtonSize + LookAndFeel::instance().appHeaderMenuToLogoGap : 0.f) + localLogoSize.x + spacing;
            const float titleRight = ImGui::GetWindowSize().x - clockRightInset - utcStringSize.x - spacing;
            textFitted(title, titleFont, LookAndFeel::instance().fontNormal()->LegacySize, titleLeft, titleRight, verticallyCentredY);
            ImGui::SetCursorPos(ImVec2{topLeft.x, verticallyCentredY(ImGui::GetTextLineHeight())});
            textRight(utcClock, clockRightInset);
            posBeneathClock = ImVec2{topLeft.x, topLeft.y + headerHeight + ImGui::GetStyle().ItemSpacing.y};
        } else {
            {
                imw::Font  font(titleFont);
                const auto titleSize = ImGui::CalcTextSize(title.data());
                if (0.5f * ImGui::GetIO().DisplaySize.x > (0.5f * titleSize.x + utcStringSize.x)) {
                    textCentered(title);
                }
            }

            ImGui::SameLine();
            auto pos = ImGui::GetCursorPos();
            textRight(localClock, clockRightInset);
            pos.y += ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetCursorPos(pos);
            textRight(utcClock, clockRightInset);
            posBeneathClock = ImGui::GetCursorPos();
        }

        VerticalPopupMenu<1> leftMenu;
        const ImVec2         menuHome{topLeft.x, verticallyCentredY(iconButtonSize)};
        const float          menuHidden = !editable ? 0.f : menuAutoHide.update(ImRect(ImGui::GetWindowPos() + menuHome, ImGui::GetWindowPos() + menuHome + ImVec2{iconButtonSize, iconButtonSize}));
        ImGui::SetCursorPos(ImVec2{menuHome.x - menuHidden * (menuHome.x + iconButtonSize), menuHome.y});

        const auto menuButtonPushed = editable && headerIconButton(gAppState.prototypeMode ? "" : "", palette.mainWindowButtonIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)));

        if ((menuButtonPushed || ImGui::IsItemHovered()) && editable) {
            const bool wasAlreadyOpen = leftMenu.isOpen();

            leftMenu.addButton("\uF201", [this]() { requestApplicationSwitchMode(ViewMode::INTERACTION); }, headerIconFont, "switch to chart view", palette.headerMenuButton, palette.popupMenuButtonIcon);
            leftMenu.addButton("\uF542", [this]() { requestApplicationSwitchMode(ViewMode::FLOWGRAPH); }, headerIconFont, "click to edit flow-graph", palette.headerMenuButton, palette.popupMenuButtonIcon);
            leftMenu.addButton("", [this]() { requestApplicationSwitchMode(ViewMode::OPEN_SAVE_DASHBOARD); }, headerIconFont, "click to open/save new dashboards", palette.headerMenuButton, palette.popupMenuButtonIcon);

            if (wasAlreadyOpen && !ImGui::IsItemHovered()) {
                std::print("was already open -> closing\n");
                leftMenu.forceClose();
            }
        }

        const float logoX = !editable ? topLeft.x : topLeft.x + (1.f - menuHidden) * (iconButtonSize + LookAndFeel::instance().appHeaderMenuToLogoGap);
        ImGui::SetCursorPos(ImVec2{logoX, verticallyCentredY(localLogoSize.y)});
        {
            imw::StyleColor normalStyle(ImGuiCol_Button, palette.appHeaderFAIRLogoButton);
            imw::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, palette.appHeaderFAIRLogoButtonHovered);
            imw::StyleColor activeStyle(ImGuiCol_ButtonActive, palette.appHeaderFAIRLogoButtonActive);
            imw::StyleVar   noPadding(ImGuiStyleVar_FramePadding, ImVec2{});
            const auto      imgLogo = static_cast<ImTextureID>(style == LookAndFeel::Style::Light ? imgFairLogo : imgFairLogoDark);
            ImGui::ImageButton("logo", imgLogo, localLogoSize);
        }

        const float           rightMenuStartAngle = LookAndFeel::instance().appHeaderRightMenuStartDegrees;
        const float           rightMenuStopAngle  = LookAndFeel::instance().appHeaderRightMenuStopDegrees;
        RadialCircularMenu<2> rightMenu(localLogoSize, rightMenuStartAngle, rightMenuStopAngle);
        ImGui::SetCursorPos(ImVec2{devMenuHome.x + devMenuHidden * (ImGui::GetWindowSize().x - devMenuHome.x), devMenuHome.y});

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
                    gAppState.windowMode == FULLSCREEN ? "\uF066" : "\uF065",
                    [&rightMenu](MenuButton& button) {
                        gAppState.windowMode = gAppState.windowMode == FULLSCREEN ? RESTORED : FULLSCREEN;
                        button.label         = gAppState.windowMode == FULLSCREEN ? "\uF066" : "\uF065";
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
                        button.toolTip = std::format("switch to the {} theme", gr::meta::enumName(magic_enum::enum_next_value_circular(next)).value_or(""));
                    },
                    LookAndFeel::instance().fontIconsSolidBig, std::format("switch to the {} theme", gr::meta::enumName(magic_enum::enum_next_value_circular(LookAndFeel::instance().theme)).value_or("")), palette.devMenuButton, palette.popupMenuButtonIcon);

                rightMenu.addButton(
                    gAppState.prototypeMode ? "" : "",
                    [](MenuButton& button) {
                        gAppState.prototypeMode    = !gAppState.prototypeMode;
                        button.label               = gAppState.prototypeMode ? "" : "";
                        ImGui::GetIO().FontDefault = LookAndFeel::instance().fontNormal();
                    },
                    LookAndFeel::instance().fontIconsSolidBig, "switch between prototype and production mode", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            if (LookAndFeel::instance().kIsDesktop) {
                rightMenu.addButton("", []() { gAppState.windowMode = MINIMISED; }, LookAndFeel::instance().fontIconsSolidBig, "minimise window", palette.devMenuButton, palette.popupMenuButtonIcon);
                rightMenu.addButton(
                    gAppState.windowMode == RESTORED ? "" : "",
                    [&rightMenu](MenuButton& button) {
                        gAppState.windowMode = gAppState.windowMode == MAXIMISED ? RESTORED : MAXIMISED;
                        button.label         = gAppState.windowMode == RESTORED ? "" : "";
                        button.toolTip       = gAppState.windowMode == MAXIMISED ? "restore window" : "maximise window";
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsSolidBig, gAppState.windowMode == MAXIMISED ? "restore window" : "maximise window", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            {
#ifdef __EMSCRIPTEN__
                constexpr bool kNewLine = false;
#else
                constexpr bool kNewLine = true;
#endif
                rightMenu.addButton<false, kNewLine>(
                    "",
                    [](MenuButton& button) {
                        TouchHandler<>::diagnostics = !TouchHandler<>::diagnostics;
                        button.font                 = TouchHandler<>::diagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig;
                        button.toolTip              = TouchHandler<>::diagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics";
                    },
                    TouchHandler<>::diagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig, TouchHandler<>::diagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics", palette.devMenuButton, palette.popupMenuButtonIcon);
            }

            if (LookAndFeel::instance().kIsDesktop) {
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
        ImGui::SetCursorPos(posBeneathClock);
    }
};

} // namespace gr::od::components

#endif
