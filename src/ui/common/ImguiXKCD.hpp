#ifndef IMGUI_XKCD_HPP
#define IMGUI_XKCD_HPP

// xkcd-style hand-drawn rendering for ImGui/ImPlot.
// Single-header library. Injects a custom vertex shader via ImDrawList callbacks.
// Requires: OpenGL 3.x+ or OpenGL ES 3.0+ (WebGL2).
//
// Only untextured geometry (lines, fills, grids) is wobbled — text is preserved
// because text vertices use font atlas UVs, not the white-pixel UV near (0,0).

#include "imgui.h"

#ifndef GLuint
using GLuint = unsigned int;
using GLint  = int;
#endif

namespace ImXkcd {

void Init();
void Apply(ImDrawData* drawData);
void Shutdown();

inline bool  enabled   = true;
inline float amplitude = 7.f;    // pixel displacement strength
inline float frequency = 0.002f; // noise frequency (lower = smoother wobble)
inline float seed      = 42.0f;  // deterministic noise seed

} // namespace ImXkcd

#ifdef IMGUI_XKCD_IMPLEMENTATION

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL3/SDL_opengl.h>
#endif

#include <array>
#include <print>

namespace ImXkcd {
namespace detail {

inline GLuint g_program = 0;
inline GLint  g_locProj = -1;
inline GLint  g_locAmp  = -1;
inline GLint  g_locFreq = -1;
inline GLint  g_locSeed = -1;
inline GLint  g_locTex  = -1;

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

    // only wobble untextured geometry — ImGui's white pixel at UV near (0,0)
    // is used for all solid-color primitives (lines, fills, grid).
    // text vertices have font atlas UVs well above this threshold.
    if (UV.x < 0.01 && UV.y < 0.01) {
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

inline GLuint compileShader(GLenum type, const char* prefix, const char* body) {
    GLuint                     shader  = glCreateShader(type);
    std::array<const char*, 2> sources = {prefix, body};
    glShaderSource(shader, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 1024> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "[ImXkcd] {} shader compile error:\n{}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
    }
    return shader;
}

inline void enableCallback(const ImDrawList*, const ImDrawCmd*) {
    GLint currentProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProg);

    std::array<float, 16> proj{};
    GLint                 projLoc = glGetUniformLocation(static_cast<GLuint>(currentProg), "ProjMtx");
    if (projLoc >= 0) {
        glGetUniformfv(static_cast<GLuint>(currentProg), projLoc, proj.data());
    }

    glUseProgram(g_program);

    // ImGui's GLSL 130 variant has no layout qualifiers — locations are driver-assigned
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));

    glUniformMatrix4fv(g_locProj, 1, GL_FALSE, proj.data());
    glUniform1f(g_locAmp, amplitude);
    glUniform1f(g_locFreq, frequency);
    glUniform1f(g_locSeed, seed);
    glUniform1i(g_locTex, 0);
}

} // namespace detail

inline void Init() {
    using namespace detail;

    GLuint vs = compileShader(GL_VERTEX_SHADER, kGlslPrefix, kVertBody);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kGlslPrefix, kFragBody);

    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);

    // must match ImGui's vertex layout
    glBindAttribLocation(g_program, 0, "Position");
    glBindAttribLocation(g_program, 1, "UV");
    glBindAttribLocation(g_program, 2, "Color");

    glLinkProgram(g_program);

    GLint ok = 0;
    glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::array<char, 1024> log{};
        glGetProgramInfoLog(g_program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "[ImXkcd] program link error:\n{}", log.data());
        glDeleteProgram(g_program);
        g_program = 0;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return;
    }

    g_locProj = glGetUniformLocation(g_program, "ProjMtx");
    g_locAmp  = glGetUniformLocation(g_program, "u_amplitude");
    g_locFreq = glGetUniformLocation(g_program, "u_frequency");
    g_locSeed = glGetUniformLocation(g_program, "u_seed");
    g_locTex  = glGetUniformLocation(g_program, "Texture");

    glDeleteShader(vs);
    glDeleteShader(fs);

    std::println("[ImXkcd] initialised (program={}, uniforms: proj={} amp={} freq={} seed={} tex={})", g_program, g_locProj, g_locAmp, g_locFreq, g_locSeed, g_locTex);
}

inline void Apply(ImDrawData* drawData) {
    using namespace detail;
    if (!g_program || !drawData || !enabled) {
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
            } else {
                ImDrawCmd enableCmd    = {};
                enableCmd.UserCallback = enableCallback;
                patched.push_back(enableCmd);

                patched.push_back(cmd);

                ImDrawCmd resetCmd    = {};
                resetCmd.UserCallback = ImDrawCallback_ResetRenderState;
                patched.push_back(resetCmd);
            }
        }

        dl->CmdBuffer.swap(patched);
    }
}

inline void Shutdown() {
    if (detail::g_program) {
        glDeleteProgram(detail::g_program);
        detail::g_program = 0;
    }
}

} // namespace ImXkcd

#endif // IMGUI_XKCD_IMPLEMENTATION
#endif // IMGUI_XKCD_HPP
