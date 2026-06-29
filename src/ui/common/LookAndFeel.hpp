#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>
#include <cstdint>

#include <imgui.h>

enum class WindowMode { FULLSCREEN, MAXIMISED, MINIMISED, RESTORED };

/**
 * @brief Convert RGB color (0xRRGGBB) to ImGui's ABGR format (0xAABBGGRR).
 * @param rgb Color in RGB format (0xRRGGBB)
 * @param alpha Alpha value (default 0xFF for fully opaque)
 * @return Color in ImGui's ABGR format
 */
constexpr std::uint32_t rgbToImGuiABGR(std::uint32_t rgb, std::uint8_t alpha = 0xFF) {
    const std::uint32_t r = (rgb >> 16) & 0xFF;
    const std::uint32_t g = (rgb >> 8) & 0xFF;
    const std::uint32_t b = (rgb >> 0) & 0xFF;
    return (static_cast<std::uint32_t>(alpha) << 24) | (b << 16) | (g << 8) | r;
}

constexpr std::uint32_t float4ToRGBA(ImVec4 float4) {
    const auto saturate = [](float f) { return (f < 0.0f) ? 0.0f : (f > 1.0f) ? 1.0f : f; };
    auto       r        = static_cast<std::uint32_t>(saturate(float4.x) * 255.0f + 0.5f);
    auto       g        = static_cast<std::uint32_t>(saturate(float4.y) * 255.0f + 0.5f);
    auto       b        = static_cast<std::uint32_t>(saturate(float4.z) * 255.0f + 0.5f);
    auto       a        = static_cast<std::uint32_t>(saturate(float4.w) * 255.0f + 0.5f);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

struct ImFont;

namespace DigitizerUi {

/// Colors that are not obviously included in the regular ImGuiStyle
/// TODO: maybe include popup menu colors (light grey hamburger icon, green buttons
/// on hamburger popup, red/violet buttons on radial menu) here, or make those use
/// colors from a style
struct Palette {
    ImVec4 gridLines;

    // main window buttons, shown on top of windowBg color, bg colors can be transparent
    ImVec4 mainWindowButtonIcon;
    ImVec4 mainWindowButtonBgInactive;
    ImVec4 mainWindowButtonBgHovered;
    ImVec4 mainWindowButtonBgActive;

    ImVec4 notificationWindowBg;

    ImVec4 toolbarLineColor;

    ImVec4 flowgraphBg;
    ImVec4 flowgraphNodeBg;
    ImVec4 flowgraphNodeBorder;
    ImVec4 flowgraphSubgraphBorder;
    ImVec4 flowgraphSubgraphBorderText;

    ImVec4 flowgraphBoundingBoxExteriorSelection;
    ImVec4 flowgraphBoundingBoxExteriorSelectionOutline;
    ImVec4 flowgraphBoundingBoxExteriorSelectionHovered;
    ImVec4 flowgraphBoundingBoxExteriorSelectionOutlineHovered;

    ImVec4 rowBgAlt;

    ImVec4 highlightedSearchResultsBg;
};

struct LookAndFeel {
    enum class Style { Light, Dark };

    struct Flowgraph {
        float  pinWidth  = 10;
        float  pinHeight = 10;
        ImVec2 minimumBlockSize{80.0f, 0.0f};

        float flowgraphBoundingBoxExteriorSelectionOutlineThickness        = 1.f;
        float flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered = 3.f;
    };

#ifdef __EMSCRIPTEN__
    const bool isDesktop = false;
#else
    const bool isDesktop = true;
#endif
    bool                      prototypeMode    = false;
    bool                      touchDiagnostics = false;
    std::chrono::milliseconds execTime; /// time it took to handle events and draw one frame
    float                     defaultDPI  = 76.2f;
    float                     verticalDPI = defaultDPI;
    std::array<ImFont*, 2>    fontTiny    = {nullptr, nullptr}; /// default font [0] production [1] prototype use
    std::array<ImFont*, 2>    fontSmall   = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontNormal  = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontBig     = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontBigger  = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontLarge   = {nullptr, nullptr}; /// 0: production 1: prototype use
    ImFont*                   fontIcons;
    ImFont*                   fontIconsBig;
    ImFont*                   fontIconsLarge;
    ImFont*                   fontIconsSolid;
    ImFont*                   fontIconsSolidBig;
    ImFont*                   fontIconsSolidLarge;
    std::chrono::seconds      editPaneCloseDelay{15};

    [[nodiscard]] const Palette& palette() const noexcept;
    [[nodiscard]] float          mainWindowIconButtonSize() const noexcept;

    Style      style      = Style::Light;
    WindowMode windowMode = WindowMode::RESTORED;

    static LookAndFeel&       mutableInstance();
    static const LookAndFeel& instance();

    static std::uint8_t  getColorAlphaU8(ImVec4 Palette::*color) { return std::clamp(static_cast<std::uint8_t>((instance().palette().*color).w * 255.f), std::uint8_t{0x00}, std::uint8_t{0xFF}); }
    static std::uint32_t getColorU32(ImVec4 Palette::*color) { return float4ToRGBA(instance().palette().*color); }
    static std::uint32_t getColorU32Opaque(ImVec4 Palette::*color) {
        const auto vec4 = instance().palette().*color;
        return float4ToRGBA({vec4.x, vec4.y, vec4.z, 1.f}) >> 8;
    }

    void loadFonts();

private:
    LookAndFeel() {}
};

} // namespace DigitizerUi

#endif
