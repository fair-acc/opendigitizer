#ifndef IMPLOT_VISUALIZATION_FAIR_HEADER_H
#define IMPLOT_VISUALIZATION_FAIR_HEADER_H
#include <imgui.h>
#define STB_IMAGE_IMPLEMENTATION
#include <chrono>
#include <cmrc/cmrc.hpp>
#include <fmt/chrono.h>
#include <stb_image.h>
#include <string_view>

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
    ImGui::SetCursorPosX(windowWidth - textWidth - ImGui::GetStyle().ItemSpacing.x - 20.0f);
    ImGui::Text("%s", text.data());
}

int    img_fair_w        = 0;
int    img_fair_h        = 0;
GLuint img_fair_tex      = 0;
GLuint img_fair_tex_dark = 0;

bool   LoadTextureFromFile(const char *filename, GLuint *out_texture, int *out_width, int *out_height) {
    // Load from file
    int            image_width  = 0;
    int            image_height = 0;

    auto           fs           = cmrc::ui_assets::get_filesystem();
    auto           file         = fs.open(filename);

    unsigned char *image_data   = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(file.begin()), file.size(), &image_width, &image_height, nullptr, 4);
    if (image_data == NULL)
        return false;

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

    *out_texture = image_texture;
    *out_width   = image_width;
    *out_height  = image_height;

    return true;
}
} // namespace detail

enum class Style {
    Light,
    Dark
};

void load_header_assets() {
    [[maybe_unused]] bool ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi.png",
            &detail::img_fair_tex, &detail::img_fair_w, &detail::img_fair_h);
    IM_ASSERT(ret);

    ret = detail::LoadTextureFromFile("assets/fair-logo/FAIR_Logo_rgb_72dpi_dark.png",
            &detail::img_fair_tex_dark, &detail::img_fair_w, &detail::img_fair_h);
    IM_ASSERT(ret);
}

void draw_header_bar(std::string_view title, ImFont *title_font, Style style) {
    using namespace detail;
    // localtime
    const auto clock         = std::chrono::system_clock::now();
    const auto utcClock      = fmt::format("{:%Y-%m-%d %H:%M:%S (LOC)}", clock);
    const auto utcStringSize = ImGui::CalcTextSize(utcClock.c_str());

    const auto topLeft       = ImGui::GetCursorPos();
    // draw title
    ImGui::PushFont(title_font);
    const auto titleSize = ImGui::CalcTextSize(title.data());
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
    std::time_t utc = std::chrono::time_point_cast<std::chrono::seconds>(clock).time_since_epoch().count();
    std::string utctime; // assume maximum of 32 characters for datetime length
    utctime.resize(32);
    pos.y += ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetCursorPos(pos);
    const auto len = strftime(utctime.data(), utctime.size(), "%H:%M:%S (UTC)", gmtime(&utc));
    TextRight(std::string_view(utctime.data(), len));

    // draw fair logo
    ImGui::SetCursorPos(topLeft);
    const auto scale = titleSize.y/img_fair_h;
    ImGui::Image((void *) (intptr_t) (style == Style::Light ? img_fair_tex : img_fair_tex_dark), ImVec2(scale * img_fair_w, scale * img_fair_h));
}

} // namespace app_header
#endif // IMPLOT_VISUALIZATION_FAIR_HEADER_H
