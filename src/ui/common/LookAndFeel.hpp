#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>
#include <cstdint>

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

struct ImFont;

namespace DigitizerUi {

struct LookAndFeel {
    enum class Style { Light, Dark };

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

    Style      style      = Style::Light;
    WindowMode windowMode = WindowMode::RESTORED;

    static LookAndFeel&       mutableInstance();
    static const LookAndFeel& instance();

    void loadFonts();

private:
    LookAndFeel() {}
};

} // namespace DigitizerUi

#endif
