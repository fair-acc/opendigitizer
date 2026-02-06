#ifndef OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP
#define OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include "../utils/ShaderHelper.hpp"
#include <implot.h>

namespace opendigitizer::charts {

static constexpr std::size_t kColormapSize = 256;

inline std::array<uint32_t, kColormapSize> buildColormapLut(ImPlotColormap cmap) {
    std::array<uint32_t, kColormapSize> lut{};
    for (std::size_t i = 0; i < kColormapSize; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kColormapSize - 1);
        ImVec4      c = ImPlot::SampleColormap(t, cmap);
        lut[i]        = uint32_t(c.x * 255.f) | (uint32_t(c.y * 255.f) << 8) | (uint32_t(c.z * 255.f) << 16) | (uint32_t(c.w * 255.f) << 24);
    }
    return lut;
}

inline ImVec4 contrastingGridColor(ImPlotColormap cmap, float alpha = 0.3f) {
    float         luminance = 0.f;
    constexpr int kSamples  = 16;
    for (int i = 0; i < kSamples; ++i) {
        ImVec4 c = ImPlot::SampleColormap(static_cast<float>(i) / static_cast<float>(kSamples - 1), cmap);
        luminance += 0.299f * c.x + 0.587f * c.y + 0.114f * c.z; // ITU-R BT.601
    }
    luminance /= static_cast<float>(kSamples);
    float grid = (luminance > 0.5f) ? 0.f : 1.f;
    return ImVec4(grid, grid, grid, alpha);
}

inline uint32_t colormapLookup(double value, double scaleMin, double scaleMax, std::span<const uint32_t, kColormapSize> lut) {
    if (scaleMax <= scaleMin) {
        return lut[0];
    }
    double norm = (value - scaleMin) / (scaleMax - scaleMin);
    auto   idx  = static_cast<std::size_t>(std::clamp(norm, 0.0, 1.0) * static_cast<double>(kColormapSize - 1));
    return lut[idx];
}

struct SpectrumFrame {
    std::span<const float> xValues;
    std::span<const float> yValues;
    std::size_t            nBins;
    int64_t                timestamp;
};

template<typename Sinks, typename Fn>
void forEachValidSpectrum(const Sinks& sinks, Fn&& fn) {
    for (const auto& sink : sinks) {
        if (!sink->drawEnabled()) {
            continue;
        }
        auto dataLock = sink->dataGuard();
        if (!sink->hasDataSets()) {
            continue;
        }
        auto allDataSets = sink->dataSets();
        if (allDataSets.empty()) {
            continue;
        }
        const auto& ds = allDataSets.back();
        if (ds.axis_values.empty() || ds.axis_values[0].empty()) {
            continue;
        }
        auto xV = ds.axisValues(0);
        auto yV = ds.signalValues(0);
        auto n  = std::min(xV.size(), yV.size());
        if (n == 0) {
            continue;
        }
        if (!fn(*sink, SpectrumFrame{xV, yV, n, ds.timestamp})) {
            return;
        }
    }
}

[[nodiscard]] inline double timestampFromNanos(int64_t ns) {
    double sec = static_cast<double>(ns) * 1e-9;
    if (sec <= 0.0) {
        sec = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) * 1e-9;
    }
    return sec;
}

/**
 * @brief Accumulates max-hold, min-hold, and exponential-average traces over successive spectrum frames.
 *
 * Max/min-hold tracks the per-bin extremes with optional exponential decay
 * controlled by a time constant in frames (tau = 0 means infinite hold).
 */
struct TraceAccumulator {
    std::vector<float> _maxHold;
    std::vector<float> _minHold;
    std::vector<float> _average;
    std::size_t        _frameCount = 0;

    void update(std::span<const float> current, std::size_t nBins, double tau, bool enabled) {
        if (!enabled) {
            return;
        }

        if (_maxHold.size() != nBins) {
            _maxHold.assign(nBins, -std::numeric_limits<float>::infinity());
            _minHold.assign(nBins, std::numeric_limits<float>::infinity());
            _average.assign(nBins, 0.0f);
            _frameCount = 0;
        }

        const bool   decay = tau > 0.0;
        const double alpha = decay ? 1.0 / tau : 0.0; // IIR coefficient

        for (std::size_t i = 0; i < nBins; ++i) {
            const auto val = static_cast<double>(current[i]);

            if (val > static_cast<double>(_maxHold[i])) {
                _maxHold[i] = current[i];
            } else if (decay) {
                _maxHold[i] = static_cast<float>(static_cast<double>(_maxHold[i]) + (val - static_cast<double>(_maxHold[i])) * alpha);
            }

            if (val < static_cast<double>(_minHold[i])) {
                _minHold[i] = current[i];
            } else if (decay) {
                _minHold[i] = static_cast<float>(static_cast<double>(_minHold[i]) + (val - static_cast<double>(_minHold[i])) * alpha);
            }

            if (_frameCount == 0) {
                _average[i] = current[i];
            } else {
                const double effectiveAlpha = decay ? alpha : (1.0 / static_cast<double>(_frameCount + 1));
                _average[i]                 = static_cast<float>(static_cast<double>(_average[i]) + (val - static_cast<double>(_average[i])) * effectiveAlpha);
            }
        }

        ++_frameCount;
    }

    void reset() {
        _maxHold.clear();
        _minHold.clear();
        _average.clear();
        _frameCount = 0;
    }

    [[nodiscard]] std::span<const float> maxHold() const noexcept { return _maxHold; }
    [[nodiscard]] std::span<const float> minHold() const noexcept { return _minHold; }
    [[nodiscard]] std::span<const float> average() const noexcept { return _average; }
    [[nodiscard]] bool                   empty() const noexcept { return _maxHold.empty(); }
};

struct TracePlotContext {
    std::span<const float> xValues;
    std::span<const float> yValues;
};

inline void plotTrace(const char* label, std::span<const float> xValues, std::span<const float> yValues, std::size_t count, const ImVec4& color) {
    TracePlotContext ctx{xValues, yValues};
    ImPlot::SetNextLineStyle(color);
    ImPlot::PlotLineG(
        label,
        [](int idx, void* userData) -> ImPlotPoint {
            auto* c = static_cast<TracePlotContext*>(userData);
            return ImPlotPoint(static_cast<double>(c->xValues[static_cast<std::size_t>(idx)]), static_cast<double>(c->yValues[static_cast<std::size_t>(idx)]));
        },
        &ctx, static_cast<int>(count));
}

inline void drawTraceOverlays(TraceAccumulator& traces, std::span<const float> xValues, std::span<const float> yValues, std::size_t nBins, double decayTau, const ImVec4& baseColor, bool showMaxHold, bool showMinHold, bool showAverage) {
    const bool anyEnabled = showMaxHold || showMinHold || showAverage;
    traces.update(yValues, nBins, decayTau, anyEnabled);

    if (traces.empty()) {
        return;
    }

    if (showMaxHold) {
        plotTrace("##maxHold", xValues, traces.maxHold(), nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, 0.9f));
    }
    if (showMinHold) {
        plotTrace("##minHold", xValues, traces.minHold(), nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, 0.5f));
    }
    if (showAverage) {
        plotTrace("##average", xValues, traces.average(), nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, 0.7f));
    }
}

namespace gl = opendigitizer::shader;

/**
 * @brief GPU-accelerated 2D density histogram with automatic CPU fallback.
 *
 * Accumulates incoming 1D spectrum lines into a 2D frequency-vs-amplitude
 * histogram with exponential decay, then colormaps the result into an RGBA8
 * texture for display via ImPlot::PlotImage.
 *
 * The GPU path uses a two-pass fullscreen-triangle pipeline (GLES3 / WebGL2):
 *   pass 1 — decay previous histogram + accumulate new spectrum (ping-pong R32F FBOs)
 *   pass 2 — normalise by peak density and sample a 256-entry colormap LUT → RGBA8
 *
 * If the driver lacks EXT_color_buffer_float or shader compilation fails,
 * the implementation transparently falls back to an equivalent CPU path.
 */
struct DensityHistogram {
    std::size_t _specBins      = 0;
    std::size_t _ampBins       = 0;
    double      _binningYMin   = 0.0;
    double      _binningYMax   = 0.0;
    bool        _preferGpu     = true;
    bool        _initAttempted = false;
    bool        _gpuAvailable  = false;

    // GPU path — ping-pong R32F histogram + RGBA8 output
    std::array<GLuint, 2> _histogramTextures{};
    std::array<GLuint, 2> _histogramFBOs{};
    int                   _pingPongIndex      = 0;
    GLuint                _colormapTexture    = 0;
    GLuint                _colormapFBO        = 0;
    GLuint                _spectrumTexture    = 0;
    GLuint                _colormapLutTexture = 0;
    ImPlotColormap        _activeColormap     = -1;
    GLuint                _emptyVAO           = 0;
    GLuint                _accumulateProgram  = 0;
    GLuint                _colormapProgram    = 0;
    GLint                 _locAccPrevHist     = -1;
    GLint                 _locAccSpecLine     = -1;
    GLint                 _locAccDecay        = -1;
    GLint                 _locAccAmpBins      = -1;
    GLint                 _locCmHist          = -1;
    GLint                 _locCmLut           = -1;
    GLint                 _locCmMaxDensity    = -1;
    float                 _peakDensity        = 0.f;

    // reusable scratch buffers (avoid per-frame heap allocations)
    std::vector<float> _scratchBuffer;

    // CPU fallback — full histogram in system memory, RGBA8 texture upload per frame
    std::vector<float>                  _cpuHistogram;
    std::vector<uint32_t>               _cpuPixels;
    GLuint                              _cpuTexture        = 0;
    ImPlotColormap                      _cpuActiveColormap = -1;
    std::array<uint32_t, kColormapSize> _cpuColormapLut{};

    DensityHistogram() = default;

    ~DensityHistogram() { destroyAllResources(); }

    DensityHistogram(DensityHistogram&& o) noexcept { swap(o); }

    DensityHistogram& operator=(DensityHistogram&& o) noexcept {
        if (this != &o) {
            destroyAllResources();
            swap(o);
        }
        return *this;
    }

    void swap(DensityHistogram& o) noexcept {
        using std::swap;
        swap(_specBins, o._specBins);
        swap(_ampBins, o._ampBins);
        swap(_binningYMin, o._binningYMin);
        swap(_binningYMax, o._binningYMax);
        swap(_preferGpu, o._preferGpu);
        swap(_initAttempted, o._initAttempted);
        swap(_gpuAvailable, o._gpuAvailable);
        swap(_histogramTextures, o._histogramTextures);
        swap(_histogramFBOs, o._histogramFBOs);
        swap(_pingPongIndex, o._pingPongIndex);
        swap(_colormapTexture, o._colormapTexture);
        swap(_colormapFBO, o._colormapFBO);
        swap(_spectrumTexture, o._spectrumTexture);
        swap(_colormapLutTexture, o._colormapLutTexture);
        swap(_activeColormap, o._activeColormap);
        swap(_emptyVAO, o._emptyVAO);
        swap(_accumulateProgram, o._accumulateProgram);
        swap(_colormapProgram, o._colormapProgram);
        swap(_locAccPrevHist, o._locAccPrevHist);
        swap(_locAccSpecLine, o._locAccSpecLine);
        swap(_locAccDecay, o._locAccDecay);
        swap(_locAccAmpBins, o._locAccAmpBins);
        swap(_locCmHist, o._locCmHist);
        swap(_locCmLut, o._locCmLut);
        swap(_locCmMaxDensity, o._locCmMaxDensity);
        swap(_peakDensity, o._peakDensity);
        swap(_scratchBuffer, o._scratchBuffer);
        swap(_cpuHistogram, o._cpuHistogram);
        swap(_cpuPixels, o._cpuPixels);
        swap(_cpuTexture, o._cpuTexture);
        swap(_cpuActiveColormap, o._cpuActiveColormap);
        swap(_cpuColormapLut, o._cpuColormapLut);
    }

    DensityHistogram(const DensityHistogram&)            = delete;
    DensityHistogram& operator=(const DensityHistogram&) = delete;

    void init() {
        if (_initAttempted) {
            return;
        }
        _initAttempted = true;
        _gpuAvailable  = _preferGpu && tryInitGpu();
    }

    [[nodiscard]] bool tryInitGpu() {
        using namespace opendigitizer::shader;

        // fullscreen triangle from gl_VertexID — no VBO needed, just an empty VAO + glDrawArrays(GL_TRIANGLES, 0, 3)
        // vertices: (-1,-1), (3,-1), (-1,3) — the GPU clips the oversized triangle to the viewport
        static constexpr auto* kVertBody = R"glsl(
out vec2 v_uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_uv = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

        // decay + accumulate: reads the previous R32F histogram (ping-pong) and the new 1D spectrum line (R32F, specBins×1)
        // spectrum values are pre-normalised to [0,1] on CPU; (1-specVal) maps high amplitudes to the top of the texture
        // each fragment checks whether its amplitude bin matches the new spectrum value (±0.5 bin tolerance)
        static constexpr auto* kAccumulateFragBody = R"glsl(
in vec2 v_uv;
out float fragDensity;

uniform sampler2D u_prevHistogram;  // ping-pong R32F, specBins × ampBins
uniform sampler2D u_spectrumLine;   // R32F, specBins × 1, values in [0,1]
uniform float     u_decayFactor;    // (1 - 1/tau), applied to previous density
uniform float     u_ampBins;        // number of amplitude bins (float for GPU arithmetic)

void main() {
    float prev    = texture(u_prevHistogram, v_uv).r * u_decayFactor;
    float specVal = texture(u_spectrumLine, vec2(v_uv.x, 0.5)).r;
    float thisBin = v_uv.y * u_ampBins;
    float hitBin  = (1.0 - specVal) * u_ampBins;
    float hit     = step(abs(thisBin - hitBin), 0.5) * step(0.0, specVal);
    fragDensity   = prev + hit;
}
)glsl";

        // colormap: normalises accumulated density by peak and samples a 256×1 RGBA8 LUT texture
        static constexpr auto* kColormapFragBody = R"glsl(
in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_histogram;    // accumulated R32F density from the accumulate pass
uniform sampler2D u_colormapLut;  // RGBA8, 256×1, built from ImPlot colormap on CPU
uniform float     u_maxDensity;   // CPU-tracked peak density (converges to tau)

void main() {
    float density = texture(u_histogram, v_uv).r;
    float norm    = clamp(density / max(u_maxDensity, 1.0), 0.0, 1.0);
    fragColor     = texture(u_colormapLut, vec2(norm, 0.5));
}
)glsl";

        GLuint vs  = compileShader(GL_VERTEX_SHADER, kGlslPrefix, kVertBody);
        GLuint fsA = compileShader(GL_FRAGMENT_SHADER, kGlslPrefix, kAccumulateFragBody);
        GLuint fsC = compileShader(GL_FRAGMENT_SHADER, kGlslPrefix, kColormapFragBody);

        _accumulateProgram = linkProgram(vs, fsA);
        _colormapProgram   = linkProgram(vs, fsC);

        glDeleteShader(vs);
        glDeleteShader(fsA);
        glDeleteShader(fsC);

        if (!_accumulateProgram || !_colormapProgram) {
            destroyGpuResources();
            return false;
        }

        _locAccPrevHist = glGetUniformLocation(_accumulateProgram, "u_prevHistogram");
        _locAccSpecLine = glGetUniformLocation(_accumulateProgram, "u_spectrumLine");
        _locAccDecay    = glGetUniformLocation(_accumulateProgram, "u_decayFactor");
        _locAccAmpBins  = glGetUniformLocation(_accumulateProgram, "u_ampBins");

        _locCmHist       = glGetUniformLocation(_colormapProgram, "u_histogram");
        _locCmLut        = glGetUniformLocation(_colormapProgram, "u_colormapLut");
        _locCmMaxDensity = glGetUniformLocation(_colormapProgram, "u_maxDensity");

        glGenVertexArrays(1, &_emptyVAO);

        if (!supportsR32FFBO()) {
            destroyGpuResources();
            return false;
        }

        return true;
    }

    void destroyGpuTextures() {
        glDeleteTextures(static_cast<GLsizei>(_histogramTextures.size()), _histogramTextures.data());
        glDeleteFramebuffers(static_cast<GLsizei>(_histogramFBOs.size()), _histogramFBOs.data());
        _histogramTextures = {};
        _histogramFBOs     = {};

        if (_colormapTexture) {
            glDeleteTextures(1, &_colormapTexture);
            _colormapTexture = 0;
        }
        if (_colormapFBO) {
            glDeleteFramebuffers(1, &_colormapFBO);
            _colormapFBO = 0;
        }
        if (_spectrumTexture) {
            glDeleteTextures(1, &_spectrumTexture);
            _spectrumTexture = 0;
        }
        if (_colormapLutTexture) {
            glDeleteTextures(1, &_colormapLutTexture);
            _colormapLutTexture = 0;
        }
        _activeColormap = -1;
    }

    void destroyGpuResources() {
        destroyGpuTextures();
        if (_accumulateProgram) {
            glDeleteProgram(_accumulateProgram);
            _accumulateProgram = 0;
        }
        if (_colormapProgram) {
            glDeleteProgram(_colormapProgram);
            _colormapProgram = 0;
        }
        if (_emptyVAO) {
            glDeleteVertexArrays(1, &_emptyVAO);
            _emptyVAO = 0;
        }
    }

    void destroyCpuResources() {
        _cpuHistogram.clear();
        _cpuPixels.clear();
        _cpuActiveColormap = -1;
        if (_cpuTexture) {
            glDeleteTextures(1, &_cpuTexture);
            _cpuTexture = 0;
        }
    }

    void destroyAllResources() {
        destroyGpuResources();
        destroyCpuResources();
        _initAttempted = false;
        _gpuAvailable  = false;
    }

    void gpuResize(std::size_t specBins, std::size_t ampBins) {
        destroyGpuTextures();
        _specBins      = specBins;
        _ampBins       = ampBins;
        _pingPongIndex = 0;
        _peakDensity   = 0.f;

        const auto w = static_cast<GLsizei>(_specBins);
        const auto h = static_cast<GLsizei>(_ampBins);

        for (int i = 0; i < 2; ++i) {
            gl::createR32FTexture(_histogramTextures[static_cast<std::size_t>(i)], w, h);
            _histogramFBOs[static_cast<std::size_t>(i)] = gl::attachFBO(_histogramTextures[static_cast<std::size_t>(i)]);
        }

        gl::createRGBA8Texture(_colormapTexture, w, h);
        _colormapFBO = gl::attachFBO(_colormapTexture);

        gl::createR32FTexture(_spectrumTexture, w, 1);

        const auto nTexels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        _scratchBuffer.resize(nTexels);
        std::ranges::fill(_scratchBuffer, 0.f);
        for (auto tex : _histogramTextures) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_FLOAT, _scratchBuffer.data());
        }
    }

    void gpuReset() {
        if (_specBins == 0 || _ampBins == 0) {
            return;
        }
        const auto w       = static_cast<GLsizei>(_specBins);
        const auto h       = static_cast<GLsizei>(_ampBins);
        const auto nTexels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        _scratchBuffer.resize(nTexels);
        std::ranges::fill(_scratchBuffer, 0.f);
        for (auto tex : _histogramTextures) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_FLOAT, _scratchBuffer.data());
        }
        _peakDensity   = 0.f;
        _pingPongIndex = 0;
    }

    void gpuUpdateColormapLut(ImPlotColormap colormap) {
        if (_activeColormap == colormap && _colormapLutTexture != 0) {
            return;
        }
        _activeColormap = colormap;
        auto lut        = buildColormapLut(colormap);

        if (!_colormapLutTexture) {
            gl::createRGBA8Texture(_colormapLutTexture, static_cast<GLsizei>(kColormapSize), 1);
        }
        glBindTexture(GL_TEXTURE_2D, _colormapLutTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(kColormapSize), 1, GL_RGBA, GL_UNSIGNED_BYTE, lut.data());
    }

    void gpuUpdate(std::span<const float> yValues, std::size_t nBins, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap) {
        // save GL state before any GL calls (resize/reset/upload all modify bindings)
        GLint                prevFBO = 0, prevProgram = 0, prevActiveTexture = 0, prevVAO = 0;
        GLint                prevTexture0 = 0, prevTexture1 = 0;
        std::array<GLint, 4> prevViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VIEWPORT, prevViewport.data());
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture1);

        if (_specBins != nBins || _ampBins != ampBins) {
            gpuResize(nBins, ampBins);
            _binningYMin = yMin;
            _binningYMax = yMax;
        }

        if (_binningYMin != yMin || _binningYMax != yMax) {
            gpuReset();
            _binningYMin = yMin;
            _binningYMax = yMax;
        }

        gpuUpdateColormapLut(colormap);

        // normalise spectrum to [0,1] range for the shader (reuses _scratchBuffer)
        _scratchBuffer.resize(nBins);
        const double ampRange = yMax - yMin;
        if (ampRange > 0.0) {
            const double invRange = 1.0 / ampRange;
            for (std::size_t i = 0; i < nBins; ++i) {
                _scratchBuffer[i] = static_cast<float>(std::clamp((static_cast<double>(yValues[i]) - yMin) * invRange, 0.0, 1.0));
            }
        } else {
            std::ranges::fill(_scratchBuffer, 0.f);
        }

        glBindTexture(GL_TEXTURE_2D, _spectrumTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(nBins), 1, GL_RED, GL_FLOAT, _scratchBuffer.data());

        const auto w = static_cast<GLsizei>(_specBins);
        const auto h = static_cast<GLsizei>(_ampBins);

        glBindVertexArray(_emptyVAO);

        // pass 1: accumulate — read from ping, write to pong
        const auto src = static_cast<std::size_t>(_pingPongIndex);
        const auto dst = static_cast<std::size_t>(1 - _pingPongIndex);

        glBindFramebuffer(GL_FRAMEBUFFER, _histogramFBOs[dst]);
        glViewport(0, 0, w, h);
        glUseProgram(_accumulateProgram);

        const float decayFactor = (decayTau > 0.0) ? static_cast<float>(1.0 - 1.0 / decayTau) : 1.f;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _histogramTextures[src]);
        glUniform1i(_locAccPrevHist, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, _spectrumTexture);
        glUniform1i(_locAccSpecLine, 1);

        glUniform1f(_locAccDecay, decayFactor);
        glUniform1f(_locAccAmpBins, static_cast<float>(_ampBins));

        glDrawArrays(GL_TRIANGLES, 0, 3);

        _pingPongIndex = 1 - _pingPongIndex;

        // track peak density on CPU
        _peakDensity = _peakDensity * decayFactor + 1.f;

        // pass 2: colormap — read histogram, write RGBA8
        glBindFramebuffer(GL_FRAMEBUFFER, _colormapFBO);
        glViewport(0, 0, w, h);
        glUseProgram(_colormapProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, _histogramTextures[static_cast<std::size_t>(_pingPongIndex)]);
        glUniform1i(_locCmHist, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, _colormapLutTexture);
        glUniform1i(_locCmLut, 1);

        glUniform1f(_locCmMaxDensity, std::max(_peakDensity, 1.f));

        glDrawArrays(GL_TRIANGLES, 0, 3);

        // restore GL state
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture1));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
        glBindVertexArray(static_cast<GLuint>(prevVAO));
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glUseProgram(static_cast<GLuint>(prevProgram));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    void cpuResize(std::size_t specBins, std::size_t ampBins) {
        _specBins         = specBins;
        _ampBins          = ampBins;
        const auto nCells = _ampBins * _specBins;
        _cpuHistogram.assign(nCells, 0.f);
        _cpuPixels.assign(nCells, 0U);

        if (_cpuTexture) {
            glDeleteTextures(1, &_cpuTexture);
        }
        glGenTextures(1, &_cpuTexture);
        glBindTexture(GL_TEXTURE_2D, _cpuTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(_specBins), static_cast<GLsizei>(_ampBins), 0, GL_RGBA, GL_UNSIGNED_BYTE, _cpuPixels.data());
    }

    void cpuReset() {
        std::ranges::fill(_cpuHistogram, 0.f);
        std::ranges::fill(_cpuPixels, uint32_t(0));
        if (_cpuTexture && _specBins > 0 && _ampBins > 0) {
            glBindTexture(GL_TEXTURE_2D, _cpuTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(_specBins), static_cast<GLsizei>(_ampBins), GL_RGBA, GL_UNSIGNED_BYTE, _cpuPixels.data());
        }
    }

    void cpuUpdate(std::span<const float> yValues, std::size_t nBins, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap) {
        if (_specBins != nBins || _ampBins != ampBins) {
            cpuResize(nBins, ampBins);
            _binningYMin = yMin;
            _binningYMax = yMax;
        }

        if (_binningYMin != yMin || _binningYMax != yMax) {
            cpuReset();
            _binningYMin = yMin;
            _binningYMax = yMax;
        }

        const double decayFactor = (decayTau > 0.0) ? (1.0 - 1.0 / decayTau) : 1.0;
        for (auto& cell : _cpuHistogram) {
            cell *= static_cast<float>(decayFactor);
        }

        const double ampRange = yMax - yMin;
        if (ampRange > 0.0) {
            const double invRange = static_cast<double>(_ampBins) / ampRange;
            for (std::size_t i = 0; i < nBins; ++i) {
                const auto val = static_cast<double>(yValues[i]);
                const auto bin = static_cast<std::size_t>(std::clamp((yMax - val) * invRange, 0.0, static_cast<double>(_ampBins - 1)));
                _cpuHistogram[bin * _specBins + i] += 1.f;
            }
        }

        if (_cpuActiveColormap != colormap) {
            _cpuActiveColormap = colormap;
            _cpuColormapLut    = buildColormapLut(colormap);
        }

        const float maxDensity = std::max(*std::ranges::max_element(_cpuHistogram), 1.f);
        std::ranges::transform(_cpuHistogram, _cpuPixels.begin(), [&](float density) { return colormapLookup(static_cast<double>(density), 0.0, static_cast<double>(maxDensity), _cpuColormapLut); });

        glBindTexture(GL_TEXTURE_2D, _cpuTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(_specBins), static_cast<GLsizei>(_ampBins), GL_RGBA, GL_UNSIGNED_BYTE, _cpuPixels.data());
    }

    void resize(std::size_t specBins, std::size_t ampBins) {
        if (_gpuAvailable) {
            gpuResize(specBins, ampBins);
        } else {
            cpuResize(specBins, ampBins);
        }
    }

    void reset() {
        if (_gpuAvailable) {
            gpuReset();
        } else {
            cpuReset();
        }
    }

    void update(std::span<const float> yValues, std::size_t nBins, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap, bool preferGpu = true) {
        if (_preferGpu != preferGpu) {
            destroyAllResources();
            _specBins  = 0;
            _ampBins   = 0;
            _preferGpu = preferGpu;
        }
        init();
        if (_gpuAvailable) {
            gpuUpdate(yValues, nBins, ampBins, decayTau, yMin, yMax, colormap);
        } else {
            cpuUpdate(yValues, nBins, ampBins, decayTau, yMin, yMax, colormap);
        }
    }

    void plot(std::span<const float> xValues, double yMin, double yMax) {
        GLuint tex = _gpuAvailable ? _colormapTexture : _cpuTexture;
        if (!tex) {
            return;
        }
        const double freqMin     = static_cast<double>(xValues.front());
        const double freqMax     = static_cast<double>(xValues.back());
        auto         toTextureId = []<typename TexId = ImTextureID>(GLuint id) -> TexId {
            if constexpr (std::is_pointer_v<TexId>) {
                return reinterpret_cast<TexId>(static_cast<std::uintptr_t>(id));
            } else {
                return static_cast<TexId>(id);
            }
        };
        ImPlot::PlotImage("##density", toTextureId(tex), ImPlotPoint(freqMin, yMin), ImPlotPoint(freqMax, yMax));
    }
};

/**
 * @brief GPU ring-buffer texture for scrolling spectrogram (waterfall) display.
 *
 * Manages an RGBA8 texture with GL_REPEAT wrapping on the T-axis so that
 * advancing the write row and adjusting UV coordinates produces a scrolling
 * effect without any data movement. Each new spectrum row is colour-mapped
 * on the CPU and uploaded via a single-row glTexSubImage2D call.
 */
struct WaterfallBuffer {
    std::size_t _width      = 0;
    std::size_t _height     = 0;
    std::size_t _writeRow   = 0;
    std::size_t _filledRows = 0;

    std::vector<uint32_t> _pixels;
    std::vector<double>   _timestamps; // UTC seconds per row (parallel ring buffer)
    GLuint                _texture = 0;

    ImPlotColormap                      _activeColormap = -1;
    std::array<uint32_t, kColormapSize> _colormapLut{};

    double _scaleMin = 0.0;
    double _scaleMax = 0.0;

    bool                       _preferGpu = true;
    std::vector<float>         _rawMagnitudes; // CPU path: raw magnitude ring buffer
    mutable std::vector<float> _linearized;    // CPU path: scratch for rendering
    double                     _effectiveScaleMin = 0.0;
    double                     _effectiveScaleMax = 0.0;

    WaterfallBuffer() = default;

    ~WaterfallBuffer() { destroy(); }

    WaterfallBuffer(WaterfallBuffer&& o) noexcept { swap(o); }

    WaterfallBuffer& operator=(WaterfallBuffer&& o) noexcept {
        if (this != &o) {
            destroy();
            swap(o);
        }
        return *this;
    }

    void swap(WaterfallBuffer& o) noexcept {
        using std::swap;
        swap(_width, o._width);
        swap(_height, o._height);
        swap(_writeRow, o._writeRow);
        swap(_filledRows, o._filledRows);
        swap(_pixels, o._pixels);
        swap(_timestamps, o._timestamps);
        swap(_texture, o._texture);
        swap(_activeColormap, o._activeColormap);
        swap(_colormapLut, o._colormapLut);
        swap(_scaleMin, o._scaleMin);
        swap(_scaleMax, o._scaleMax);
        swap(_preferGpu, o._preferGpu);
        swap(_rawMagnitudes, o._rawMagnitudes);
        swap(_linearized, o._linearized);
        swap(_effectiveScaleMin, o._effectiveScaleMin);
        swap(_effectiveScaleMax, o._effectiveScaleMax);
    }

    WaterfallBuffer(const WaterfallBuffer&)            = delete;
    WaterfallBuffer& operator=(const WaterfallBuffer&) = delete;

    void init(std::size_t width, std::size_t height, bool preferGpu = true) {
        destroy();
        _width      = width;
        _height     = height;
        _writeRow   = 0;
        _filledRows = 0;
        _scaleMin   = 0.0;
        _scaleMax   = 0.0;
        _preferGpu  = preferGpu;

        _timestamps.assign(_height, 0.0);

        if (_preferGpu) {
            _pixels.assign(_width * _height, 0U);
            glGenTextures(1, &_texture);
            glBindTexture(GL_TEXTURE_2D, _texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(_width), static_cast<GLsizei>(_height), 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data());
        } else {
            _rawMagnitudes.assign(_width * _height, 0.0f);
        }
    }

    void setPreferGpu(bool preferGpu) {
        if (_preferGpu == preferGpu) {
            return;
        }
        auto w = _width;
        auto h = _height;
        init(w, h, preferGpu);
    }

    void pushRow(std::span<const float> magnitudes, std::size_t count, double scaleMin, double scaleMax, double timestampSec, ImPlotColormap colormap) {
        if (_width == 0 || _height == 0) {
            return;
        }

        _effectiveScaleMin = scaleMin;
        _effectiveScaleMax = scaleMax;

        auto n = std::min(count, _width);

        if (_preferGpu && _texture) {
            if (_activeColormap != colormap || (_colormapLut[0] == 0 && _colormapLut[1] == 0)) {
                _colormapLut = buildColormapLut(colormap);
            }

            uint32_t* row = _pixels.data() + _writeRow * _width;
            for (std::size_t i = 0; i < n; ++i) {
                row[i] = colormapLookup(static_cast<double>(magnitudes[i]), scaleMin, scaleMax, _colormapLut);
            }
            std::fill_n(row + n, _width - n, uint32_t(0));

            GLint prevTexture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
            glBindTexture(GL_TEXTURE_2D, _texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(_writeRow), static_cast<GLsizei>(_width), 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));
        } else {
            float* row = _rawMagnitudes.data() + _writeRow * _width;
            for (std::size_t i = 0; i < n; ++i) {
                row[i] = magnitudes[i];
            }
            std::fill_n(row + n, _width - n, 0.0f);
        }

        _activeColormap        = colormap;
        _timestamps[_writeRow] = timestampSec;
        _writeRow              = (_writeRow + 1) % _height;
        if (_filledRows < _height) {
            ++_filledRows;
        }
    }

    void render(double freqMin, double freqMax, double yMin, double yMax, bool newestAtTop = true) const {
        if (_filledRows == 0) {
            return;
        }

        if (_texture) {
            // center-of-texel UVs avoid GL_NEAREST boundary ambiguity at the write-head seam
            const auto fHeight = static_cast<float>(_height);
            float      vNewest = (static_cast<float>(_writeRow) - 0.5f) / fHeight;
            float      vOldest = (static_cast<float>(_writeRow) - static_cast<float>(_filledRows) + 0.5f) / fHeight;

            // uv0 maps to (bmin.x, bmax.y) = screen top-left = plot yMax
            // uv1 maps to (bmax.x, bmin.y) = screen bottom-right = plot yMin
            float vTop        = newestAtTop ? vNewest : vOldest;
            float vBottom     = newestAtTop ? vOldest : vNewest;
            auto  toTextureId = []<typename TexId = ImTextureID>(GLuint id) -> TexId {
                if constexpr (std::is_pointer_v<TexId>) {
                    return reinterpret_cast<TexId>(static_cast<std::uintptr_t>(id));
                } else {
                    return static_cast<TexId>(id);
                }
            };
            ImPlot::PlotImage("##waterfall", toTextureId(_texture), ImPlotPoint(freqMin, yMin), ImPlotPoint(freqMax, yMax), ImVec2(0.0f, vTop), ImVec2(1.0f, vBottom));
        } else {
            renderCpu(freqMin, freqMax, yMin, yMax, newestAtTop);
        }
    }

    void renderCpu(double freqMin, double freqMax, double yMin, double yMax, bool newestAtTop) const {
        _linearized.resize(_filledRows * _width);
        for (std::size_t i = 0; i < _filledRows; ++i) {
            // PlotHeatmap maps row 0 to the top of the bounding box
            std::size_t srcRow = newestAtTop ? (_writeRow + _height - 1 - i) % _height : (_writeRow + _height - _filledRows + i) % _height;
            std::copy_n(_rawMagnitudes.data() + srcRow * _width, _width, _linearized.data() + i * _width);
        }

        ImPlot::PushColormap(_activeColormap);
        ImPlot::PlotHeatmap("##waterfall", _linearized.data(), static_cast<int>(_filledRows), static_cast<int>(_width), _effectiveScaleMin, _effectiveScaleMax, nullptr, ImPlotPoint(freqMin, yMin), ImPlotPoint(freqMax, yMax));
        ImPlot::PopColormap();
    }

    void resizeHistory(std::size_t newHeight) {
        if (newHeight == _height || _width == 0) {
            return;
        }

        std::vector<double> newTimestamps(newHeight, 0.0);
        std::size_t         rowsToCopy = std::min(_filledRows, newHeight);

        if (_preferGpu) {
            std::vector<uint32_t> newPixels(_width * newHeight, 0U);
            for (std::size_t i = 0; i < rowsToCopy; ++i) {
                std::size_t srcRow = (_writeRow + _height - rowsToCopy + i) % _height;
                std::size_t dstRow = (newHeight - rowsToCopy + i) % newHeight;
                std::copy_n(_pixels.data() + srcRow * _width, _width, newPixels.data() + dstRow * _width);
                newTimestamps[dstRow] = _timestamps[srcRow];
            }
            _pixels = std::move(newPixels);

            GLint prevTexture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
            glBindTexture(GL_TEXTURE_2D, _texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(_width), static_cast<GLsizei>(newHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data());
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));
        } else {
            std::vector<float> newMagnitudes(_width * newHeight, 0.0f);
            for (std::size_t i = 0; i < rowsToCopy; ++i) {
                std::size_t srcRow = (_writeRow + _height - rowsToCopy + i) % _height;
                std::size_t dstRow = (newHeight - rowsToCopy + i) % newHeight;
                std::copy_n(_rawMagnitudes.data() + srcRow * _width, _width, newMagnitudes.data() + dstRow * _width);
                newTimestamps[dstRow] = _timestamps[srcRow];
            }
            _rawMagnitudes = std::move(newMagnitudes);
        }

        _timestamps = std::move(newTimestamps);
        _height     = newHeight;
        _filledRows = rowsToCopy;
        _writeRow   = 0;
    }

    void clear() {
        if (_preferGpu) {
            std::ranges::fill(_pixels, uint32_t(0));
        } else {
            std::ranges::fill(_rawMagnitudes, 0.0f);
        }
        std::ranges::fill(_timestamps, 0.0);
        _writeRow   = 0;
        _filledRows = 0;
        _scaleMin   = 0.0;
        _scaleMax   = 0.0;

        if (_texture && _width > 0 && _height > 0) {
            GLint prevTexture = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
            glBindTexture(GL_TEXTURE_2D, _texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(_width), static_cast<GLsizei>(_height), GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data());
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture));
        }
    }

    [[nodiscard]] std::pair<double, double> rawTimeBounds() const {
        if (_filledRows == 0) {
            return {0.0, 0.0};
        }
        double tOldest = _timestamps[(_writeRow + _height - _filledRows) % _height];
        double tNewest = _timestamps[(_writeRow + _height - 1) % _height];
        if (tNewest <= tOldest) {
            tNewest = tOldest + static_cast<double>(_filledRows);
        }
        return {tOldest, tNewest};
    }

    void updateAutoScale(std::span<const float> yValues, std::size_t nBins) {
        auto [minIt, maxIt] = std::ranges::minmax_element(yValues | std::views::take(static_cast<std::ptrdiff_t>(nBins)));
        double fMin         = static_cast<double>(*minIt);
        double fMax         = static_cast<double>(*maxIt);
        if (_filledRows == 0) {
            _scaleMin = fMin;
            _scaleMax = fMax;
        } else {
            constexpr double kAlpha = 0.05;
            _scaleMin += (fMin - _scaleMin) * kAlpha;
            _scaleMax += (fMax - _scaleMax) * kAlpha;
        }
    }

    void destroy() {
        if (_texture) {
            glDeleteTextures(1, &_texture);
            _texture = 0;
        }
        _pixels.clear();
        _rawMagnitudes.clear();
        _linearized.clear();
        _timestamps.clear();
        _width      = 0;
        _height     = 0;
        _writeRow   = 0;
        _filledRows = 0;
    }

    [[nodiscard]] std::size_t width() const noexcept { return _width; }
    [[nodiscard]] std::size_t filledRows() const noexcept { return _filledRows; }
    [[nodiscard]] double      scaleMin() const noexcept { return _scaleMin; }
    [[nodiscard]] double      scaleMax() const noexcept { return _scaleMax; }
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP
