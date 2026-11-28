#ifndef OPENDIGITIZER_UTILS_SHADERHELPER_HPP
#define OPENDIGITIZER_UTILS_SHADERHELPER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <type_traits>
#include <utility>

#include <imgui.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>
#endif

namespace gr::od::shader {

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

/// GL objects die with their context; one freed without a current context would be a call into no context
[[nodiscard]] inline bool hasCurrentContext() noexcept {
#ifdef __EMSCRIPTEN__
    return emscripten_webgl_get_current_context() != 0;
#else
    return SDL_GL_GetCurrentContext() != nullptr;
#endif
}

template<typename TexId = ImTextureID>
[[nodiscard]] TexId toTextureId(GLuint id) {
    if constexpr (std::is_pointer_v<TexId>) {
        return reinterpret_cast<TexId>(static_cast<std::uintptr_t>(id));
    } else {
        return static_cast<TexId>(id);
    }
}

/// restores the previous binding (the renderer's)
template<typename TFn>
void withTextureBound(GLuint texture, TFn&& fn) {
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    fn();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

/// an owned GL_TEXTURE_2D of RGBA8 texels (bytes R,G,B,A: 0xAABBGGRR as a little-endian uint32) or R32F texels;
/// allocated by the first upload and again whenever the size changes, freed with the object
struct Texture2D {
    enum class Format : std::uint8_t { rgba8, r32f };

    struct Options {
        Format format    = Format::rgba8;
        GLint  minFilter = GL_NEAREST;
        GLint  magFilter = GL_NEAREST;
        GLint  wrapS     = GL_CLAMP_TO_EDGE; // WebGL requires clamping for non-power-of-two textures
        GLint  wrapT     = GL_CLAMP_TO_EDGE;
    };

    Texture2D() = default;
    explicit Texture2D(Options options) noexcept : _options(options) {}
    Texture2D(const Texture2D&)            = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept : _handle(std::exchange(other._handle, 0U)), _width(std::exchange(other._width, 0UZ)), _height(std::exchange(other._height, 0UZ)), _options(other._options), _failed(std::exchange(other._failed, false)) {}
    Texture2D& operator=(Texture2D&& other) noexcept {
        if (this != &other) {
            reset();
            _handle  = std::exchange(other._handle, 0U);
            _width   = std::exchange(other._width, 0UZ);
            _height  = std::exchange(other._height, 0UZ);
            _options = other._options;
            _failed  = std::exchange(other._failed, false);
        }
        return *this;
    }
    ~Texture2D() { reset(); }

    /// writes width x height texels, (re)allocating first when the size differs; nullptr allocates without data.
    /// False when GL hands out no texture (no context), which is not retried until reset()
    [[nodiscard]] bool upload(std::size_t width, std::size_t height, const void* pixels) {
        if (_handle == 0U && !_failed) {
            glGenTextures(1, &_handle);
            _failed = _handle == 0U;
        }
        if (_handle == 0U) {
            return false;
        }
        withTextureBound(_handle, [&] {
            resetRowLength();
            if (width != _width || height != _height) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, _options.minFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, _options.magFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _options.wrapS);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _options.wrapT);
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat(), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, pixelFormat(), pixelType(), pixels);
                _width  = width;
                _height = height;
            } else if (pixels != nullptr) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), pixelFormat(), pixelType(), pixels);
            }
        });
        return true;
    }

    void uploadRows(std::size_t firstRow, std::size_t nRows, const void* pixels) {
        if (_handle == 0U || nRows == 0UZ || firstRow + nRows > _height) {
            return;
        }
        withTextureBound(_handle, [&] {
            resetRowLength();
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(firstRow), static_cast<GLsizei>(_width), static_cast<GLsizei>(nRows), pixelFormat(), pixelType(), pixels);
        });
    }

    void reset() noexcept {
        if (_handle != 0U && hasCurrentContext()) { // e.g. App's header outlives teardownSDL()
            glDeleteTextures(1, &_handle);
        }
        _handle = 0U;
        _width  = 0UZ;
        _height = 0UZ;
        _failed = false;
    }

    [[nodiscard]] explicit    operator bool() const noexcept { return _handle != 0U; }
    [[nodiscard]] GLuint      handle() const noexcept { return _handle; }
    [[nodiscard]] ImTextureID id() const { return toTextureId(_handle); }
    [[nodiscard]] std::size_t width() const noexcept { return _width; }
    [[nodiscard]] std::size_t height() const noexcept { return _height; }

private:
    GLuint      _handle = 0U;
    std::size_t _width  = 0UZ;
    std::size_t _height = 0UZ;
    Options     _options{};
    bool        _failed = false;

    [[nodiscard]] GLint  internalFormat() const noexcept { return _options.format == Format::r32f ? GL_R32F : GL_RGBA; }
    [[nodiscard]] GLenum pixelFormat() const noexcept { return _options.format == Format::r32f ? GL_RED : GL_RGBA; }
    [[nodiscard]] GLenum pixelType() const noexcept { return _options.format == Format::r32f ? GL_FLOAT : GL_UNSIGNED_BYTE; }

    static void resetRowLength() noexcept { // rows are tightly packed, whatever the renderer left set
#ifdef GL_UNPACK_ROW_LENGTH
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    }
};

} // namespace gr::od::shader

#endif // OPENDIGITIZER_UTILS_SHADERHELPER_HPP
