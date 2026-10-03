#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

#include <imgui.h>
#include <implot.h>

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

inline ImVec4 lightenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    s = std::max(0.0f, s * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}

inline ImVec4 darkenColor(const ImVec4& color, float percent) {
    float h;
    float s;
    float v;
    ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);
    v = std::max(0.0f, v * percent);
    float r;
    float g;
    float b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return {r, g, b, color.w};
}

inline ImVec4 darkenOrLighten(ImVec4 color, float percentage);

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
    ImVec4 errorColor;
    ImVec4 currentDashboardPanelBg;
    ImVec4 contentSeparator; // subtle divider lines between page content areas
};

/// Chart look a host may set process-wide (`LookAndFeel::mutableInstance().chartStyle`); an unset field keeps ImPlot's
/// current style, so the default reproduces the App's charts. Series colours are set by `ColourManager`'s palette.
struct ChartStyle {
    std::optional<ImVec4>         plotBackground;            // alpha 0: transparent
    std::optional<float>          gridAlpha;                 // opacity of the grid lines, replaces ImPlot's
    std::optional<float>          axisAlpha;                 // opacity of the tick marks and labels
    std::optional<float>          lineWidth;                 // px, for every series
    bool                          colourAxesBySignal = true; // with several axes, each axis takes its signals' colour
    std::optional<ImPlotLocation> legendLocation;
    std::optional<float>          legendAlpha;         // opacity of the legend panel
    ImFont*                       labelFont = nullptr; // not owned: ticks, axis titles, legend, tags and tooltips
    std::optional<float>          labelFontSize;       // unscaled base size, as ImGui::PushFont takes it
    std::optional<ImVec4>         axisColour;          // tick labels, axis titles, ticks, plot border; several y axes keep colourAxesBySignal
    std::optional<ImVec4>         gridColour;          // gridAlpha, if set, replaces its alpha
};

/// Dashboard look a host may set process-wide (`LookAndFeel::mutableInstance().dashboardStyle`), for every DashboardView
struct DashboardStyle {
    enum class LegendPosition { Bottom, Top, Left, Right, None }; // None: no shared bar, charts may show their own legend
    LegendPosition legend     = LegendPosition::Bottom;
    bool           background = true; // false: chart windows, plots area and bar draw no background or border
};

struct LookAndFeel {
    enum class Style { Light, Dark };

    struct Flowgraph {
        float  pinWidth  = 10;
        float  pinHeight = 10;
        ImVec2 minimumBlockSize{80.0f, 0.0f};

        float flowgraphBoundingBoxExteriorSelectionOutlineThickness        = 1.f;
        float flowgraphBoundingBoxExteriorSelectionOutlineThicknessHovered = 3.f;

        float exportedTabOverlap  = 6.0f;
        float exportedTabPaddingH = 4.0f;
        float exportedTabPaddingV = 2.0f;

        bool canvasBackground = true; // a host drawing the editor over its own background turns these off; background and
        bool canvasGrid       = true; // grid take effect with FlowgraphPage::updateStyle()
        bool canvasBorder     = true;
    };

#ifdef __EMSCRIPTEN__
    static constexpr bool isDesktop = false;
#else
    static constexpr bool isDesktop = true;
#endif
    bool                      prototypeMode = false;
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
    Flowgraph                 flowgraph;
    ChartStyle                chartStyle;
    DashboardStyle            dashboardStyle;

    [[nodiscard]] const Palette& palette() const noexcept;
    [[nodiscard]] float          mainWindowIconButtonSize() const noexcept;

    /// size for `faces[prototypeMode]` that keeps its load-time ratio to `fontNormal` relative to the current font size; 0 (keep current size) without loaded fonts
    [[nodiscard]] float relativeFontSize(const std::array<ImFont*, 2>& faces) const noexcept {
        const ImFont* face   = faces[prototypeMode];
        const ImFont* normal = fontNormal[prototypeMode];
        return face && normal ? ImGui::GetStyle().FontSizeBase * face->LegacySize / normal->LegacySize : 0.f;
    }

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

inline ImVec4 darkenOrLighten(ImVec4 color, float percentage) { //
    return DigitizerUi::LookAndFeel::instance().style == DigitizerUi::LookAndFeel::Style::Dark ? lightenColor(color, percentage) : darkenColor(color, percentage);
};

#endif
