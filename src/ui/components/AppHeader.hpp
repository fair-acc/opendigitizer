#ifndef OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_APP_HEADER_HPP_
// #define STB_IMAGE_IMPLEMENTATION

#include <chrono>
#include <string_view>

#include <cmrc/cmrc.hpp>
#include <format>

#include "../common/AppDefinitions.hpp"
#include "../common/ImguiWrap.hpp"
#include "../components/ImGuiNotify.hpp"

#include <SDL3/SDL_opengl.h>
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

inline void TextRight(const std::string_view text) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX(windowWidth - textWidth - ImGui::GetStyle().ItemSpacing.x);
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
    std::function<void(LookAndFeel::Style)> requestApplicationSwitchTheme;

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

        const auto topLeft = ImGui::GetCursorPos();
        // draw title
        ImVec2 localLogoSize;
        {
            IMW::Font  font(title_font);
            const auto titleSize = ImGui::CalcTextSize(title.data());
            const auto scale     = titleSize.y / logoSize.y;
            localLogoSize        = ImVec2(scale * logoSize.x, scale * logoSize.y);
            // suppress title if it doesn't fit or is likely to overlap
            if (0.5f * ImGui::GetIO().DisplaySize.x > (0.5f * titleSize.x + utcStringSize.x)) {
                TextCentered(title);
            }
        }

        ImGui::SameLine();
        auto pos = ImGui::GetCursorPos();
        TextRight(localClock);
        pos.y += ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetCursorPos(pos);
        TextRight(utcClock);
        auto posBeneathClock = ImGui::GetCursorPos();

        // left menu
        ImGui::SetCursorPos(topLeft);

        IMW::StyleColor      normalStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        IMW::StyleColor      hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(.8f, .8f, .8f, 0.4f));
        IMW::StyleColor      activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        VerticalPopupMenu<1> leftMenu;

        const auto& settings         = Digitizer::Settings::instance();
        const auto  menuButtonPushed = settings.editableMode && [&] {
            IMW::StyleColor mainButtonStyle(ImGuiCol_Text, ImVec4(.8f, .8f, .8f, 0.6f));
            IMW::Font       font(LookAndFeel::instance().fontIconsSolidLarge);
            return ImGui::Button(LookAndFeel::instance().prototypeMode ? "" : "");
        }();

        IMW::StyleVar framePaddingStyle(ImGuiStyleVar_FramePadding, ImVec2(4, 6));

        if ((menuButtonPushed || ImGui::IsItemHovered()) && settings.editableMode) {
            using DigitizerUi::MenuButton;
            const bool wasAlreadyOpen = leftMenu.isOpen();

            {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{126.f / 255.f, 188.f / 255.f, 137.f / 255.f, 1.f}); // green
                leftMenu.addButton("\uF201", [this]() { requestApplicationSwitchMode(ViewMode::VIEW); }, LookAndFeel::instance().fontIconsSolidLarge, "switch to view mode");

                leftMenu.addButton("\uF248", [this]() { requestApplicationSwitchMode(ViewMode::LAYOUT); }, LookAndFeel::instance().fontIconsSolidLarge, "switch to layout mode");
                leftMenu.addButton("\uF542", [this]() { requestApplicationSwitchMode(ViewMode::FLOWGRAPH); }, LookAndFeel::instance().fontIconsSolidLarge, "click to edit flow-graph");
                leftMenu.addButton("", [this]() { requestApplicationSwitchMode(ViewMode::OPEN_SAVE_DASHBOARD); }, LookAndFeel::instance().fontIconsSolidLarge, "click to open/save new dashboards");
            }

            if (wasAlreadyOpen && !ImGui::IsItemHovered()) {
                std::print("was already open -> closing\n");
                leftMenu.forceClose();
            }
        }

        // draw fair logo
        if (settings.editableMode) {
            ImGui::SameLine(0.f, 0.f);
        }
        const auto imgLogo = static_cast<ImTextureID>(style_ == LookAndFeel::Style::Light ? imgFairLogo : imgFairLogoDark);
        if (ImGui::ImageButton("logo", imgLogo, localLogoSize)) {
            // call url to project site
        }

        // right menu
        RadialCircularMenu<2> rightMenu(localLogoSize, 75.f, 195.f);
        ImGui::SetCursorPos(ImVec2(ImGui::GetIO().DisplaySize.x - localLogoSize.x, 0));

        const bool   mouseMoved    = ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0;
        static float buttonTimeOut = 0;
        buttonTimeOut -= std::max(ImGui::GetIO().DeltaTime, 0.f);
        if (mouseMoved) {
            buttonTimeOut = 2.f;
        }

        const bool devMenuButtonPushed = settings.editableMode && [&] {
            IMW::StyleColor textStyle(ImGuiCol_Text, ImVec4(.8f, .8f, .8f, (mouseMoved || buttonTimeOut > 0.f) ? 0.9f : 0.f));
            IMW::Font       font(LookAndFeel::instance().fontIconsSolidLarge);
            return ImGui::Button("");
        }();

        if ((devMenuButtonPushed || ImGui::IsItemHovered()) && settings.editableMode) {
            using enum WindowMode;

            {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{.3f, .3f, 1.0f, 1.f}); // blue
                rightMenu.addButton(
                    LookAndFeel::instance().windowMode == FULLSCREEN ? "\uF066" : "\uF065",
                    [&rightMenu](MenuButton& button) {
                        LookAndFeel::mutableInstance().windowMode = LookAndFeel::instance().windowMode == FULLSCREEN ? RESTORED : FULLSCREEN;
                        button.label                              = LookAndFeel::instance().windowMode == FULLSCREEN ? "\uF066" : "\uF065";
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsSolidLarge, "toggle between fullscreen and windowed mode");
            }

            {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{.3f, .3f, 1.0f, 1.f}); // blue
                using enum LookAndFeel::Style;
                rightMenu.addButton((LookAndFeel::instance().style == Light) ? "" : "",
                    [this](MenuButton& button) {
                        const bool isDarkMode = LookAndFeel::instance().style == Dark;
                        requestApplicationSwitchTheme(isDarkMode ? Light : Dark);
                        button.label   = isDarkMode ? "" : "";
                        button.toolTip = isDarkMode ? "switch to dark mode" : "switch to light mode";
                    },
                    LookAndFeel::instance().fontIconsSolidBig, LookAndFeel::instance().style == Dark ? "switch to light mode" : "switch to dark mode");

                rightMenu.addButton(
                    LookAndFeel::instance().prototypeMode ? "" : "",
                    [](MenuButton& button) {
                        LookAndFeel::mutableInstance().prototypeMode = !LookAndFeel::instance().prototypeMode;
                        button.label                                 = LookAndFeel::instance().prototypeMode ? "" : "";
                        ImGui::GetIO().FontDefault                   = LookAndFeel::instance().fontNormal[LookAndFeel::instance().prototypeMode];
                    },
                    LookAndFeel::instance().fontIconsSolidBig, "switch between prototype and production mode");
            }

            if (LookAndFeel::instance().isDesktop) {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{.3f, .3f, 1.0f, 1.f}); // blue
                rightMenu.addButton("", []() { LookAndFeel::mutableInstance().windowMode = MINIMISED; }, LookAndFeel::instance().fontIconsSolidBig, "minimise window");
                rightMenu.addButton(
                    LookAndFeel::instance().windowMode == RESTORED ? "" : "",
                    [&rightMenu](MenuButton& button) {
                        LookAndFeel::mutableInstance().windowMode = LookAndFeel::instance().windowMode == MAXIMISED ? RESTORED : MAXIMISED;
                        button.label                              = LookAndFeel::instance().windowMode == RESTORED ? "" : "";
                        button.toolTip                            = LookAndFeel::instance().windowMode == MAXIMISED ? "restore window" : "maximise window";
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsSolidBig, LookAndFeel::instance().windowMode == MAXIMISED ? "restore window" : "maximise window");
            }

            {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{.3f, .3f, 1.0f, 1.f}); // blue
#ifdef __EMSCRIPTEN__
                constexpr bool newLine = false;
#else
                constexpr bool newLine = true;
#endif
                rightMenu.addButton<false, newLine>(
                    "",
                    [](MenuButton& button) {
                        LookAndFeel::mutableInstance().touchDiagnostics = !LookAndFeel::instance().touchDiagnostics;
                        button.font                                     = LookAndFeel::instance().touchDiagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig;
                        button.toolTip                                  = LookAndFeel::instance().touchDiagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics";
                    },
                    LookAndFeel::instance().touchDiagnostics ? LookAndFeel::instance().fontIconsBig : LookAndFeel::instance().fontIconsSolidBig, LookAndFeel::instance().touchDiagnostics ? "disable extra touch diagnostics" : "enable extra touch diagnostics");
            }

            if (LookAndFeel::instance().isDesktop) {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4{1.0f, 0.0f, 0.0f, 1.0f}); // red
                rightMenu.addButton(
                    "",
                    [this, &rightMenu]() {
                        std::print("requesting exit\n");
                        requestApplicationStop();
                        rightMenu.forceClose();
                    },
                    LookAndFeel::instance().fontIconsBig, "close app");
            }
        }

        posBeneathClock.x = 0.f;
        ImGui::SetCursorPos(posBeneathClock); // set to end of image
    }
};

} // namespace DigitizerUi::components

#endif
