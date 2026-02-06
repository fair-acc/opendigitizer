#ifndef OPENDIGITIZER_UTILS_SHADERHELPER_HPP
#define OPENDIGITIZER_UTILS_SHADERHELPER_HPP

#include <array>
#include <optional>
#include <print>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL3/SDL_opengl.h>
#endif

namespace opendigitizer::shader {

#ifdef __EMSCRIPTEN__
static constexpr auto* kGlslPrefix = "#version 300 es\nprecision highp float;\n";
#else
static constexpr auto* kGlslPrefix = "#version 330 core\n";
#endif

inline GLuint compileShader(GLenum type, const char* prefix, const char* body) {
    GLuint shader = glCreateShader(type);
    if (!shader) {
        std::println(stderr, "[ShaderHelper] glCreateShader failed");
        return 0;
    }
    std::array<const char*, 2> sources = {prefix, body};
    glShaderSource(shader, static_cast<GLsizei>(sources.size()), sources.data(), nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 1024> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "[ShaderHelper] {} shader compile error:\n{}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

inline GLuint linkProgram(GLuint vs, GLuint fs) {
    if (!vs || !fs) {
        return 0;
    }
    GLuint program = glCreateProgram();
    if (!program) {
        std::println(stderr, "[ShaderHelper] glCreateProgram failed");
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::array<char, 1024> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::println(stderr, "[ShaderHelper] program link error:\n{}", log.data());
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

inline void createR32FTexture(GLuint& tex, GLsizei w, GLsizei h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
}

inline void createRGBA8Texture(GLuint& tex, GLsizei w, GLsizei h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

inline GLuint attachFBO(GLuint tex) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::println(stderr, "[ShaderHelper] FBO incomplete for texture {}", tex);
    }
    return fbo;
}

// probes whether the driver supports rendering into R32F textures (requires EXT_color_buffer_float on WebGL2)
// result is cached after the first probe
[[nodiscard]] inline bool supportsR32FFBO() {
    static std::optional<bool> cached;
    if (cached.has_value()) {
        return *cached;
    }
    GLuint tex = 0, fbo = 0;
    createR32FTexture(tex, 1, 1);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    bool complete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    if (!complete) {
        std::println(stderr, "[ShaderHelper] R32F FBO not supported (EXT_color_buffer_float missing?)");
    }
    cached = complete;
    return complete;
}

} // namespace opendigitizer::shader

#endif // OPENDIGITIZER_UTILS_SHADERHELPER_HPP
