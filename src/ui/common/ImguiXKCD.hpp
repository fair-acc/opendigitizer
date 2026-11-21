#ifndef IMGUI_XKCD_HPP
#define IMGUI_XKCD_HPP

// xkcd-style hand-drawn rendering for ImGui/ImPlot: a custom vertex shader injected via ImDrawList callbacks, for OpenGL 3.x+ or OpenGL ES 3.0+ (WebGL2).
// Only untextured geometry (lines, fills, grids) wobbles: text vertices use font atlas UVs, not the white-pixel UV near (0,0).

#include "imgui.h"

namespace gr::od::xkcd {

void init();
void apply(ImDrawData* drawData);
void shutdown();

inline bool         enabled      = true;
inline float        amplitude    = 7.f;    // pixel displacement strength
inline float        frequency    = 0.002f; // noise frequency (lower = smoother wobble)
inline float        seed         = 42.0f;
inline unsigned int minElemCount = 50; // skip draw commands below this index count (filters grid/axis lines)

} // namespace gr::od::xkcd

#ifdef IMGUI_XKCD_IMPLEMENTATION

#include "../utils/ShaderHelper.hpp"

#include <algorithm>
#include <array>
#include <print>

namespace gr::od::xkcd {
namespace detail {

inline GLuint gProgram     = 0;
inline GLint  gLocProj     = -1;
inline GLint  gLocAmp      = -1;
inline GLint  gLocFreq     = -1;
inline GLint  gLocSeed     = -1;
inline GLint  gLocTex      = -1;
inline GLint  gLocUtilMaxY = -1;

#ifdef __EMSCRIPTEN__
static constexpr auto* kGlslPrefix = "#version 300 es\nprecision mediump float;\n";
#else
static constexpr auto* kGlslPrefix = "#version 330 core\n";
#endif

static constexpr auto* kVertBody = R"glsl(
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 UV;
layout(location = 2) in vec4 Color;

uniform mat4  ProjMtx;
uniform float u_amplitude;
uniform float u_frequency;
uniform float u_seed;
uniform float u_utilMaxY;

out vec2 Frag_UV;
out vec4 Frag_Color;

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i),              hash(i + vec2(1, 0)), f.x),
        mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x),
        f.y
    );
}

void main() {
    Frag_UV    = UV;
    Frag_Color = Color;
    vec2 pos   = Position;

    // wobble untextured geometry: white pixel, baked line textures, and cursor
    // data all live in the top rows of the atlas (low V). Glyph UVs are below.
    // u_utilMaxY is computed at runtime to stay correct across ImGui versions.
    if (UV.y < u_utilMaxY) {
        // vertex-ID noise: travels with the line (dominant for dense polylines)
        float lineSeed = dot(Color.rgb, vec3(7.13, 157.7, 1117.3));
        float t = float(gl_VertexID) * u_frequency + lineSeed;
        float vtxNx = vnoise(vec2(t, u_seed)) - 0.5;
        float vtxNy = vnoise(vec2(t + 57.0, u_seed + 113.0)) - 0.5;

        // position noise: axis/grid lines have only 2-4 vertices with adjacent
        // IDs — position noise gives their endpoints different offsets
        vec2  nc    = pos * u_frequency * 0.4 + u_seed;
        float posNx = vnoise(nc) - 0.5;
        float posNy = vnoise(nc + vec2(57.0, 113.0)) - 0.5;

        pos.x += (vtxNx + posNx) * u_amplitude;
        pos.y += (vtxNy + posNy) * u_amplitude;
    }

    gl_Position = ProjMtx * vec4(pos, 0.0, 1.0);
}
)glsl";

static constexpr auto* kFragBody = R"glsl(
in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
layout(location = 0) out vec4 Out_Color;
void main() {
    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);
}
)glsl";

inline void enableCallback(const ImDrawList*, const ImDrawCmd*) {
    GLint currentProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);

    std::array<float, 16> proj{};
    GLint                 projLoc = glGetUniformLocation(static_cast<GLuint>(currentProg), "ProjMtx");
    if (projLoc >= 0) {
        glGetUniformfv(static_cast<GLuint>(currentProg), projLoc, proj.data());
    }

    glUseProgram(gProgram);

    // ImGui's GLSL 130 variant has no layout qualifiers — locations are driver-assigned
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));

    glUniformMatrix4fv(gLocProj, 1, GL_FALSE, proj.data());
    glUniform1f(gLocAmp, amplitude);
    glUniform1f(gLocFreq, frequency);
    glUniform1f(gLocSeed, seed);
    glUniform1i(gLocTex, 0);

    // Y threshold covering all atlas utility data (white pixel + baked line textures)
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    float        maxY  = ImGui::GetFontTexUvWhitePixel().y;
    for (int n = 0; n < IM_DRAWLIST_TEX_LINES_WIDTH_MAX + 1; ++n) {
        maxY = std::max(maxY, atlas->TexUvLines[n].y);
        maxY = std::max(maxY, atlas->TexUvLines[n].w);
    }
    glUniform1f(gLocUtilMaxY, maxY + 0.03f);
}

} // namespace detail

inline void init() {
    using namespace detail;

    const GLuint vs = shader::compileShader(GL_VERTEX_SHADER, detail::kGlslPrefix, kVertBody);
    const GLuint fs = shader::compileShader(GL_FRAGMENT_SHADER, detail::kGlslPrefix, kFragBody);
    if (!vs || !fs) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    gProgram = glCreateProgram();
    glAttachShader(gProgram, vs);
    glAttachShader(gProgram, fs);

    // must match ImGui's vertex layout
    glBindAttribLocation(gProgram, 0, "Position");
    glBindAttribLocation(gProgram, 1, "UV");
    glBindAttribLocation(gProgram, 2, "Color");

    glLinkProgram(gProgram);

    GLint ok = 0;
    glGetProgramiv(gProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::array<char, 1024> log{};
        glGetProgramInfoLog(gProgram, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "[ImXkcd] program link error:\n{}", log.data());
        glDeleteProgram(gProgram);
        gProgram = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    gLocProj     = glGetUniformLocation(gProgram, "ProjMtx");
    gLocAmp      = glGetUniformLocation(gProgram, "u_amplitude");
    gLocFreq     = glGetUniformLocation(gProgram, "u_frequency");
    gLocSeed     = glGetUniformLocation(gProgram, "u_seed");
    gLocTex      = glGetUniformLocation(gProgram, "Texture");
    gLocUtilMaxY = glGetUniformLocation(gProgram, "u_utilMaxY");

    glDeleteShader(vs);
    glDeleteShader(fs);

    std::println("[ImXkcd] initialised (program={}, uniforms: proj={} amp={} freq={} seed={} tex={} utilMaxY={})", gProgram, gLocProj, gLocAmp, gLocFreq, gLocSeed, gLocTex, gLocUtilMaxY);
}

inline void apply(ImDrawData* drawData) {
    using namespace detail;
    if (!gProgram || !drawData || !enabled) {
        return;
    }

    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        ImDrawList* dl    = drawData->CmdLists[i];
        const int   nCmds = dl->CmdBuffer.Size;

        // each draw cmd becomes: [enable_callback] [original draw] [reset]
        ImVector<ImDrawCmd> patched;
        patched.reserve(nCmds * 3);

        for (int j = 0; j < nCmds; ++j) {
            const ImDrawCmd& cmd = dl->CmdBuffer[j];

            if (cmd.UserCallback) {
                patched.push_back(cmd);
            } else if (cmd.ElemCount >= minElemCount) {
                ImDrawCmd enableCmd    = {};
                enableCmd.UserCallback = enableCallback;
                patched.push_back(enableCmd);

                patched.push_back(cmd);

                ImDrawCmd resetCmd    = {};
                resetCmd.UserCallback = ImDrawCallback_ResetRenderState;
                patched.push_back(resetCmd);
            } else {
                patched.push_back(cmd);
            }
        }

        dl->CmdBuffer.swap(patched);
    }
}

inline void shutdown() {
    if (detail::gProgram) {
        glDeleteProgram(detail::gProgram);
        detail::gProgram = 0;
    }
}

} // namespace gr::od::xkcd

#endif // IMGUI_XKCD_IMPLEMENTATION
#endif // IMGUI_XKCD_HPP
