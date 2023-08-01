#ifndef IMPLOT_VISUALIZATION_FAIR_HEADER_H
#define IMPLOT_VISUALIZATION_FAIR_HEADER_H
#include <imgui.h>
#define STB_IMAGE_IMPLEMENTATION
#include <chrono>
#include <cmrc/cmrc.hpp>
#include <fmt/chrono.h>
#include <stb_image.h>
#include <string_view>

#include "../app.h"
#include <PopupMenu.hpp>

CMRC_DECLARE(ui_assets);

namespace app_header {
namespace detail {

void TextCentered(const std::string_view text) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("%s", text.data());
}

void TextRight(const std::string_view text) {
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
    ImGui::SetCursorPosX(windowWidth - textWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::Text("%s", text.data());
}

ImVec2 logoSize{ 0.f, 0.f };
GLuint imgFairLogo     = 0;
GLuint imgFairLogoDark = 0;

bool   LoadTextureFromFile(const char *filename, GLuint *out_texture, ImVec2 &textureSize) {
    // Load from file
    int            image_width  = 0;
    int            image_height = 0;

    auto           fs           = cmrc::ui_assets::get_filesystem();
    auto           file         = fs.open(filename);

    unsigned char *image_data   = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(file.begin()), file.size(), &image_width, &image_height, nullptr, 4);
    if (image_data == nullptr) {
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

enum class Style {
    Light,
    Dark
};

void load_header_assets() {
    [[maybe_unused]] bool ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi.png",
            &detail::imgFairLogo, detail::logoSize);
    IM_ASSERT(ret);

    ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png",
            &detail::imgFairLogoDark, detail::logoSize);
    IM_ASSERT(ret);
}

void draw_header_bar(std::string_view title, ImFont *title_font, Style style) {
    using namespace detail;
    // localtime
    const auto clock         = std::chrono::system_clock::now();
    const auto utcClock      = fmt::format("{:%Y-%m-%d %H:%M:%S (LOC)}", std::chrono::round<std::chrono::seconds>(clock));
    const auto utcStringSize = ImGui::CalcTextSize(utcClock.c_str());

    const auto topLeft       = ImGui::GetCursorPos();
    // draw title
    ImGui::PushFont(title_font);
    const auto titleSize     = ImGui::CalcTextSize(title.data());
    const auto scale         = titleSize.y / logoSize.y;
    const auto localLogoSize = ImVec2(scale * logoSize.x, scale * logoSize.y);
    // suppress title if it doesn't fit or is likely to overlap
    if (0.5f * ImGui::GetIO().DisplaySize.x > (0.5f * titleSize.x + utcStringSize.x)) {
        TextCentered(title);
    }
    ImGui::PopFont();

    ImGui::SameLine();
    auto pos = ImGui::GetCursorPos();
    TextRight(utcClock); // %Z should print abbreviated timezone but doesnt
    // utc (using c-style timedate functions because of missing stdlib support)
    // const auto utc_clock = std::chrono::utc_clock::now(); // c++20 timezone is not implemented in gcc or clang yet
    // date + tz library unfortunately doesn't play too well with emscripten/fetchcontent
    // const auto localtime = fmt::format("{:%H:%M:%S (%Z)}", date::make_zoned("utc", clock).get_sys_time());
    std::string utctime; // assume maximum of 32 characters for datetime length
    utctime.resize(32);
    pos.y += ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetCursorPos(pos);
    const auto utc = std::chrono::system_clock::to_time_t(clock);
    const auto len = strftime(utctime.data(), utctime.size(), "%H:%M:%S (UTC)", gmtime(&utc));
    TextRight(std::string_view(utctime.data(), len));
    auto posBeneathClock = ImGui::GetCursorPos();

    // left menu
    auto &app = DigitizerUi::App::instance();
    ImGui::SetCursorPos(topLeft);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.8f, .8f, .8f, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    fair::VerticalPopupMenu<1> leftMenu;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.8f, .8f, .8f, 0.6f));
    ImGui::PushFont(app.fontIconsSolidLarge);
    bool menuButtonPushed = ImGui::Button(app.prototypeMode ? "" : "");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 6));
    if (menuButtonPushed || ImGui::IsItemHovered()) {
        using fair::MenuButton;
        const bool wasAlreadyOpen = leftMenu.isOpen();

        ImGui::PushStyleColor(ImGuiCol_Button, { 126.f / 255.f, 188.f / 255.f, 137.f / 255.f, 1.f }); // green
        leftMenu.addButton(
                "\uF201", [&app]() {
                    app.mainViewMode = "View";
                },
                app.fontIconsSolidLarge, "switch to view mode");

        leftMenu.addButton(
                "\uF248", [&app]() {
                    app.mainViewMode = "Layout";
                },
                app.fontIconsSolidLarge, "switch to layout mode");
        leftMenu.addButton(
                "\uF542", [&app]() {
                    app.mainViewMode = "FlowGraph";
                },
                app.fontIconsSolidLarge, "click to edit flow-graph");
        leftMenu.addButton(
                "", [&app]() {
                    app.mainViewMode = "OpenSaveDashboard";
                },
                app.fontIconsSolidLarge, "click to open/save new dashboards");
        ImGui::PopStyleColor();

        if (wasAlreadyOpen && !ImGui::IsItemHovered()) {
            fmt::print("was already open -> closing\n");
            leftMenu.forceClose();
        }
    }

    // draw fair logo
    ImGui::SameLine(0.f, 0.f);
    const ImTextureID imgLogo = (void *) (intptr_t) (style == Style::Light ? imgFairLogo : imgFairLogoDark);
    if (ImGui::ImageButton(imgLogo, localLogoSize)) {
        // call url to project site
    }

    // right menu
    fair::RadialCircularMenu<2> rightMenu(localLogoSize, 75.f, 195.f);
    ImGui::SetCursorPos(ImVec2(ImGui::GetIO().DisplaySize.x - localLogoSize.x, 0));

    const bool   mouseMoved    = ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0;
    static float buttonTimeOut = 0;
    buttonTimeOut -= std::max(ImGui::GetIO().DeltaTime, 0.f);
    if (mouseMoved) {
        buttonTimeOut = 2.f;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.8f, .8f, .8f, (mouseMoved || buttonTimeOut > 0.f) ? 0.9f : 0.f));
    ImGui::PushFont(app.fontIconsSolidLarge);
    bool devMenuButtonPushed = ImGui::Button("");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    if (devMenuButtonPushed || ImGui::IsItemHovered()) {
        using fair::MenuButton;
        using enum DigitizerUi::WindowMode;

        ImGui::PushStyleColor(ImGuiCol_Button, { .3f, .3f, 1.0f, 1.f }); // blue
        rightMenu.addButton(
                app.windowMode == FULLSCREEN ? "\uF066" : "\uF065", [&app, &rightMenu](MenuButton &button) {
                    app.windowMode = app.windowMode == FULLSCREEN ? RESTORED : FULLSCREEN;
                    button.label   = app.windowMode == FULLSCREEN ? "\uF066" : "\uF065";
                    rightMenu.forceClose();
                },
                app.fontIconsSolidLarge, "toggle between fullscreen and windowed mode");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Button, { .3f, .3f, 1.0f, 1.f }); // blue
        using enum DigitizerUi::Style;
        rightMenu.addButton((app.style() == Light) ? "" : "", [&app](MenuButton &button) {
            const bool isDarkMode = app.style() == Dark;
            app.setStyle(isDarkMode ? Light : Dark);
            button.label   = isDarkMode ? "" : "";
            button.toolTip = isDarkMode ? "switch to dark mode" : "switch to light mode";
        },
                app.fontIconsSolidBig, app.style() == Dark ? "switch to light mode" : "switch to dark mode");

        rightMenu.addButton(
                app.prototypeMode ? "" : "", [&app](MenuButton &button) {
                    app.prototypeMode          = !app.prototypeMode;
                    button.label               = app.prototypeMode ? "" : "";
                    ImGui::GetIO().FontDefault = app.fontNormal[app.prototypeMode];
                },
                app.fontIconsSolidBig, "switch between prototype and production mode");
        ImGui::PopStyleColor();

        if (app.isDesktop) {
            ImGui::PushStyleColor(ImGuiCol_Button, { .3f, .3f, 1.0f, 1.f }); // blue
            rightMenu.addButton(
                    "", [&app]() { app.windowMode = MINIMISED; }, app.fontIconsSolidBig, "minimise window");
            rightMenu.addButton(
                    app.windowMode == RESTORED ? "" : "", [&app, &rightMenu](MenuButton &button) {
                        app.windowMode = app.windowMode == MAXIMISED ? RESTORED : MAXIMISED;
                        button.label   = app.windowMode == RESTORED ? "" : "";
                        button.toolTip = app.windowMode == MAXIMISED ? "restore window" : "maximise window";
                        rightMenu.forceClose();
                    },
                    app.fontIconsSolidBig, app.windowMode == MAXIMISED ? "restore window" : "maximise window");
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Button, { 1.f, .0f, 0.f, 1.f }); // red
            rightMenu.addButton<false, true>(
                    "", [&app, &rightMenu]() { fmt::print("request exit: {} \n", app.running); app.running = false; rightMenu.forceClose(); }, app.fontIconsBig, "close app");
            ImGui::PopStyleColor();
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    posBeneathClock.x = 0.f;
    ImGui::SetCursorPos(posBeneathClock); // set to end of image
}

} // namespace app_header
#endif // IMPLOT_VISUALIZATION_FAIR_HEADER_H
