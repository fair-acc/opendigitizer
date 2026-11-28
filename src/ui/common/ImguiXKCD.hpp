#ifndef IMGUI_XKCD_HPP
#define IMGUI_XKCD_HPP

// xkcd-style hand-drawn rendering for ImGui/ImPlot: the vertices of untextured geometry (lines, fills, grids) are displaced by value noise on the CPU before the renderer runs, so it works with any ImGui renderer backend.
// Text keeps its shape: glyph vertices use font-atlas UVs below the atlas's utility rows (white pixel, baked lines).

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace gr::od::xkcd {

inline bool         enabled      = true;
inline float        amplitude    = 7.f;    // pixel displacement strength
inline float        frequency    = 0.002f; // noise frequency (lower = smoother wobble)
inline float        seed         = 42.0f;
inline unsigned int minElemCount = 50; // skip draw commands below this index count (filters grid/axis lines)

namespace detail {

[[nodiscard]] inline float fract(float x) noexcept { return x - std::floor(x); }

/// D. Hoskins, "Hash without Sine" (hash12), shadertoy.com/view/4djSRW, MIT licence
[[nodiscard]] inline float hash(float px, float py) noexcept {
    float       x = fract(px * 0.1031f);
    float       y = fract(py * 0.1031f);
    float       z = fract(px * 0.1031f);
    const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
    x += d;
    y += d;
    z += d;
    return fract((x + y) * z);
}

[[nodiscard]] inline float valueNoise(float px, float py) noexcept {
    const float ix     = std::floor(px);
    const float iy     = std::floor(py);
    float       fx     = px - ix;
    float       fy     = py - iy;
    fx                 = fx * fx * (3.f - 2.f * fx);
    fy                 = fy * fy * (3.f - 2.f * fy);
    const float bottom = std::lerp(hash(ix, iy), hash(ix + 1.f, iy), fx);
    const float top    = std::lerp(hash(ix, iy + 1.f), hash(ix + 1.f, iy + 1.f), fx);
    return std::lerp(bottom, top, fy);
}

/// the highest atlas V of ImGui's utility data (white pixel, baked line textures): glyph UVs lie below it
[[nodiscard]] inline float utilityMaxV() {
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    float              maxV  = ImGui::GetFontTexUvWhitePixel().y;
    for (int n = 0; n < IM_DRAWLIST_TEX_LINES_WIDTH_MAX + 1; ++n) {
        maxV = std::max({maxV, atlas->TexUvLines[n].y, atlas->TexUvLines[n].w});
    }
    return maxV + 0.03f;
}

} // namespace detail

/// displaces the untextured vertices (uv.y < utilityMaxV) that draw commands with at least `minElemCount` indices use;
/// each vertex moves once, by noise along its index (travels with a polyline) plus noise of its position (separates the few vertices of an axis line)
inline void apply(ImDrawData* drawData, float utilityMaxV) {
    if (drawData == nullptr || !enabled) {
        return;
    }
    std::vector<bool> moved;
    for (ImDrawList* list : drawData->CmdLists) {
        moved.assign(static_cast<std::size_t>(list->VtxBuffer.Size), false);
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback != nullptr || cmd.ElemCount < minElemCount) {
                continue;
            }
            for (unsigned int e = 0U; e < cmd.ElemCount; ++e) {
                const auto vertex = static_cast<std::size_t>(cmd.VtxOffset) + static_cast<std::size_t>(list->IdxBuffer[static_cast<int>(cmd.IdxOffset + e)]);
                if (vertex >= moved.size() || moved[vertex]) {
                    continue;
                }
                moved[vertex] = true;
                ImDrawVert& v = list->VtxBuffer[static_cast<int>(vertex)];
                if (v.uv.y >= utilityMaxV) {
                    continue;
                }
                const ImVec4 colour   = ImGui::ColorConvertU32ToFloat4(v.col);
                const float  lineSeed = colour.x * 7.13f + colour.y * 157.7f + colour.z * 1117.3f;
                const float  t        = static_cast<float>(vertex) * frequency + lineSeed;
                const float  nx       = detail::valueNoise(t, seed) - 0.5f;
                const float  ny       = detail::valueNoise(t + 57.f, seed + 113.f) - 0.5f;
                const float  cx       = v.pos.x * frequency * 0.4f + seed;
                const float  cy       = v.pos.y * frequency * 0.4f + seed;
                const float  px       = detail::valueNoise(cx, cy) - 0.5f;
                const float  py       = detail::valueNoise(cx + 57.f, cy + 113.f) - 0.5f;
                v.pos.x += (nx + px) * amplitude;
                v.pos.y += (ny + py) * amplitude;
            }
        }
    }
}

inline void apply(ImDrawData* drawData) { apply(drawData, detail::utilityMaxV()); }

} // namespace gr::od::xkcd

#endif // IMGUI_XKCD_HPP
