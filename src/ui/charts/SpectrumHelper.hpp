#ifndef OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP
#define OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gnuradio-4.0/DataSet.hpp>

#include "../common/LookAndFeel.hpp"
#include "../utils/ShaderHelper.hpp"
#include "Chart.hpp"
#include <implot.h>

namespace gr::od::charts {

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
    if (scaleMax <= scaleMin || !std::isfinite(value)) {
        return lut[0];
    }
    double norm = (value - scaleMin) / (scaleMax - scaleMin);
    auto   idx  = static_cast<std::size_t>(std::clamp(norm, 0.0, 1.0) * static_cast<double>(kColormapSize - 1));
    return lut[idx];
}

inline constexpr float kEmptyDensityFraction = 1.f / static_cast<float>(kColormapSize - 1UZ);
static_assert(kColormapSize == 256UZ, "the colormap shader's empty-density threshold is 1/255");

inline uint32_t densityColour(float density, float maxDensity, std::span<const uint32_t, kColormapSize> lut) {
    if (density < maxDensity * kEmptyDensityFraction) {
        return 0U;
    }
    return colormapLookup(static_cast<double>(density), 0.0, static_cast<double>(maxDensity), lut);
}

struct SpectrumFrame {
    std::span<const float>              xValues;
    std::span<const float>              yValues;
    std::size_t                         nBins;
    int64_t                             timestamp;
    std::span<const gr::DataSet<float>> history;
};

[[nodiscard]] inline std::optional<SpectrumFrame> toSpectrumFrame(const gr::DataSet<float>& ds) {
    if (ds.axis_values.empty() || ds.axis_values[0].empty()) {
        return std::nullopt;
    }
    auto xV = ds.axisValues(0);
    auto yV = ds.signalValues(0);
    auto n  = std::min(xV.size(), yV.size());
    if (n == 0) {
        return std::nullopt;
    }
    return SpectrumFrame{xV, yV, n, ds.timestamp, {}};
}

template<typename Sinks, typename Fn>
void forEachValidSpectrum(const Sinks& sinks, Fn&& fn) {
    for (const auto& sink : sinks) {
        auto dataLock = sink->dataGuard();
        if (!sink->hasDataSets()) {
            continue;
        }
        auto allDataSets = sink->dataSets();
        if (allDataSets.empty()) {
            continue;
        }
        auto frame = toSpectrumFrame(allDataSets.back());
        if (!frame) {
            continue;
        }
        frame->history = allDataSets;
        if (!fn(*sink, *frame)) {
            return;
        }
    }
}

template<typename Fn>
void forEachNewSpectrum(const SpectrumFrame& newest, std::size_t count, Fn&& fn) {
    for (const auto& ds : newest.history.last(std::min(count, newest.history.size()))) {
        if (auto frame = toSpectrumFrame(ds)) {
            fn(*frame);
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

/// max-hold, min-hold and exponential-average traces over successive spectrum frames;
/// the holds decay with a time constant in frames, tau = 0 holds for ever
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

inline void plotTrace(const char* label, std::span<const float> xValues, std::span<const float> yValues, std::size_t count, const ImVec4& color, float lineWidth = IMPLOT_AUTO) {
    TracePlotContext ctx{xValues, yValues};
    ImPlot::SetNextLineStyle(color, lineWidth);
    ImPlot::PlotLineG(
        label,
        [](int idx, void* userData) -> ImPlotPoint {
            auto* c = static_cast<TracePlotContext*>(userData);
            return ImPlotPoint(static_cast<double>(c->xValues[static_cast<std::size_t>(idx)]), static_cast<double>(c->yValues[static_cast<std::size_t>(idx)]));
        },
        &ctx, static_cast<int>(count));
}

/// gates per-frame accumulation (hold/decay, waterfall/surface rows) to the data rate, not the UI redraw rate;
/// the DataSet timestamp cannot serve this because gr::blocks::fft::FFT hardcodes it to 0
[[nodiscard]] inline std::size_t consumeNewData(std::size_t& lastCount, std::size_t currentCount) noexcept {
    const std::size_t nNew = currentCount >= lastCount ? currentCount - lastCount : currentCount;
    lastCount              = currentCount;
    return nNew;
}

/// fixed number of log-spaced display columns for a log-frequency spectrum (waterfall/density/surface).
inline constexpr std::size_t kLogSpectrumColumns = 2048;

/// re-samples a linear spectrum (`freqs`/`mags`, `nBins`) onto `out.size()` log-spaced columns over [fMin, fMax]:
/// a peak-preserving maximum per column, then columns finer than the bin spacing are filled.
/// This keeps low-frequency detail on a log axis.
inline void buildLogBinnedRow(std::span<const float> freqs, std::span<const float> mags, std::size_t nBins, double fMin, double fMax, std::span<float> out) {
    std::ranges::fill(out, -std::numeric_limits<float>::infinity());
    const std::size_t nCols  = out.size();
    const double      logMin = std::log(fMin);
    const double      span   = std::log(fMax) - logMin;
    for (std::size_t k = 0; k < nBins; ++k) {
        const double freq = static_cast<double>(freqs[k]);
        if (freq < fMin || freq > fMax) {
            continue;
        }
        const auto col = std::min(static_cast<std::size_t>((std::log(freq) - logMin) / span * static_cast<double>(nCols)), nCols - 1);
        out[col]       = std::max(out[col], mags[k]);
    }
    float carry     = 0.0f;
    bool  seen      = false;
    float firstVal  = 0.0f;
    bool  haveFirst = false;
    for (float& v : out) { // forward-fill: propagate the last occupied column into the gaps above it
        if (std::isfinite(v)) {
            carry = v;
            seen  = true;
            if (!haveFirst) {
                firstVal  = v;
                haveFirst = true;
            }
        } else if (seen) {
            v = carry;
        }
    }
    for (float& v : out) { // back-fill leading columns (below the first occupied bin) with the first value
        if (std::isfinite(v)) {
            break;
        }
        v = haveFirst ? firstVal : 0.0f;
    }
}

inline void drawTraceOverlays(TraceAccumulator& traces, const SpectrumFrame& newest, std::size_t nNew, double decayTau, const ImVec4& baseColor, bool showMaxHold, bool showMinHold, bool showAverage) {
    const bool anyEnabled = showMaxHold || showMinHold || showAverage;
    forEachNewSpectrum(newest, nNew, [&](const SpectrumFrame& f) { traces.update(f.yValues, f.nBins, decayTau, anyEnabled); });

    if (traces.empty()) {
        return;
    }

    if (showMaxHold) {
        plotTrace("##maxHold", newest.xValues, traces.maxHold(), newest.nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, LookAndFeel::instance().chartStyle.maxHoldTraceAlpha));
    }
    if (showMinHold) {
        plotTrace("##minHold", newest.xValues, traces.minHold(), newest.nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, LookAndFeel::instance().chartStyle.minHoldTraceAlpha));
    }
    if (showAverage) {
        plotTrace("##average", newest.xValues, traces.average(), newest.nBins, ImVec4(baseColor.x, baseColor.y, baseColor.z, LookAndFeel::instance().chartStyle.averageTraceAlpha));
    }
}

template<typename TChart>
void drawSpectrumSignals(TChart& chart, std::unordered_map<std::string, std::size_t>& sampleCountPerSink) {
    forEachValidSpectrum(chart._signalSinks, [&](const auto& sink, const SpectrumFrame& f) {
        if (sink.drawEnabled()) {
            plotTrace(plotLabel(sink).c_str(), f.xValues, f.yValues, f.nBins, sinkColor(sink.color()), seriesLineWidth(sink));
        }
        const std::string sinkKey = std::string(sink.uniqueName());
        const std::size_t nNew    = consumeNewData(sampleCountPerSink[sinkKey], sink.totalSampleCount());
        drawTraceOverlays(chart._tracesPerSink[sinkKey], f, nNew, static_cast<double>(chart.decay_tau_frames), sinkColor(chart.trace_color), chart.show_max_hold, chart.show_min_hold, chart.show_average);
        return true;
    });
}

[[nodiscard]] inline std::pair<double, double> timeAxisBounds(double tOldest, double tNewest, AxisScale scale) {
    double duration = tNewest - tOldest;
    if (scale == AxisScale::Time) {
        return {tOldest, tNewest};
    }
    if (scale == AxisScale::LinearReverse) {
        return {-duration, 0.0};
    }
    return {0.0, duration}; // Linear, Log10, SymLog — positive offsets
}

inline int formatTimeOfDay(double value, char* buff, int size, void* /*userData*/) {
    using Clock    = std::chrono::system_clock;
    auto timePoint = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(value)));
    auto secs      = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
    auto ms        = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - secs).count();
    auto result    = std::format_to_n(buff, static_cast<std::ptrdiff_t>(size) - 1, "{:%H:%M:%S}.{:02}", secs, ms / 10);
    *result.out    = '\0';
    return static_cast<int>(result.out - buff);
}

// ImPlotScale_Time is X-axis-only: time is Linear with a custom formatter on either axis
inline void setupTimeAxis(ImAxis timeAxis, AxisScale scale, bool showGrid, std::string& unitStorage) {
    ImPlotAxisFlags tFlags = (showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines) | ImPlotAxisFlags_Foreground;
    ImPlot::SetupAxis(timeAxis, "", tFlags);

    if (scale == AxisScale::Time) {
        ImPlot::SetupAxisFormat(timeAxis, formatTimeOfDay, nullptr);
        ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Linear);
        return;
    }
    unitStorage = "s";
    ImPlot::SetupAxisFormat(timeAxis, axis::formatMetric, const_cast<void*>(static_cast<const void*>(unitStorage.c_str())));
    switch (scale) {
    case AxisScale::Log10: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Log10); break;
    case AxisScale::SymLog: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_SymLog); break;
    default: ImPlot::SetupAxisScale(timeAxis, ImPlotScale_Linear); break;
    }
}

template<typename T>
[[nodiscard]] std::vector<T> resizeRing(const std::vector<T>& ring, std::size_t rowWidth, std::size_t height, std::size_t writeRow, std::size_t keptRows, std::size_t newHeight) {
    std::vector<T> resized(rowWidth * newHeight, T{});
    for (std::size_t i = 0; i < keptRows; ++i) {
        const std::size_t srcRow = (writeRow + height - keptRows + i) % height;
        const std::size_t dstRow = (newHeight - keptRows + i) % newHeight;
        std::copy_n(ring.data() + srcRow * rowWidth, rowWidth, resized.data() + dstRow * rowWidth);
    }
    return resized;
}

inline void followColourScale(double& scaleMin, double& scaleMax, double frameMin, double frameMax, bool first) {
    if (first) {
        scaleMin = frameMin;
        scaleMax = frameMax;
        return;
    }
    constexpr double kAlpha = 0.05;
    scaleMin += (frameMin - scaleMin) * kAlpha;
    scaleMax += (frameMax - scaleMax) * kAlpha;
}

namespace gl = shader;

template<typename TexId = ImTextureID>
[[nodiscard]] TexId toTextureId(GLuint id) {
    if constexpr (std::is_pointer_v<TexId>) {
        return reinterpret_cast<TexId>(static_cast<std::uintptr_t>(id));
    } else {
        return static_cast<TexId>(id);
    }
}

template<typename TFn>
void withTextureBound(GLuint texture, TFn&& fn) {
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    fn();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}

/// a frequency-vs-amplitude histogram of successive spectrum lines with exponential decay, colour-mapped into an RGBA8 texture for ImPlot::PlotImage.
/// The GPU path (GLES3 / WebGL2) draws two fullscreen-triangle passes:
/// pass 1 decays the previous histogram and accumulates the new spectrum (ping-pong R32F FBOs),
/// pass 2 normalises by the peak density and samples a 256-entry colormap LUT into RGBA8.
/// Without EXT_color_buffer_float or on a shader compile failure, an equivalent CPU path runs.
struct DensityHistogram {
    std::size_t _specBins      = 0;
    std::size_t _ampBins       = 0;
    double      _binningYMin   = 0.0;
    double      _binningYMax   = 0.0;
    bool        _preferGpu     = true;
    bool        _initAttempted = false;
    bool        _gpuAvailable  = false;

    std::array<GLuint, 2UZ> _histogramTextures{};
    std::array<GLuint, 2UZ> _histogramFBOs{};
    int                     _pingPongIndex      = 0;
    GLuint                  _colormapTexture    = 0;
    GLuint                  _colormapFBO        = 0;
    GLuint                  _spectrumTexture    = 0;
    GLuint                  _colormapLutTexture = 0;
    ImPlotColormap          _activeColormap     = -1;
    GLuint                  _emptyVAO           = 0;
    GLuint                  _accumulateProgram  = 0;
    GLuint                  _colormapProgram    = 0;
    GLint                   _locAccPrevHist     = -1;
    GLint                   _locAccSpecLine     = -1;
    GLint                   _locAccDecay        = -1;
    GLint                   _locAccAmpBins      = -1;
    GLint                   _locCmHist          = -1;
    GLint                   _locCmLut           = -1;
    GLint                   _locCmMaxDensity    = -1;
    float                   _peakDensity        = 0.f;

    std::vector<float> _scratchBuffer;
    std::vector<float> _logRow; // linear spectrum re-binned onto log-spaced columns

    std::vector<float>                  _cpuHistogram;
    std::vector<uint32_t>               _cpuPixels;
    GLuint                              _cpuTexture        = 0;
    ImPlotColormap                      _cpuActiveColormap = -1;
    std::array<uint32_t, kColormapSize> _cpuColormapLut{};
    bool                                _cpuNeedsRecolour = false;

    DensityHistogram() = default;

    ~DensityHistogram() { destroyAllResources(); }

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
        using namespace shader;

        // fullscreen triangle from gl_VertexID, with an empty VAO and no VBO:
        // the vertices (-1,-1), (3,-1), (-1,3) are clipped by the GPU to the viewport
        static constexpr auto* kVertBody = R"glsl(
out vec2 v_uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_uv = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

        // the spectrum line (R32F, specBins×1) arrives normalised to [0,1]; (1-specVal) puts high amplitudes at the top of the texture
        // a fragment accumulates when its amplitude bin lies within ±0.5 bins of the new spectrum value
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

        static constexpr auto* kColormapFragBody = R"glsl(
in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_histogram;    // accumulated R32F density from the accumulate pass
uniform sampler2D u_colormapLut;  // RGBA8, 256×1, built from ImPlot colormap on CPU
uniform float     u_maxDensity;   // CPU-tracked peak density (converges to tau)

void main() {
    float density = texture(u_histogram, v_uv).r;
    float norm    = clamp(density / max(u_maxDensity, 1.0), 0.0, 1.0);
    fragColor     = norm < 1.0 / 255.0 ? vec4(0.0) : texture(u_colormapLut, vec2(norm, 0.5)); // empty: as densityColour()
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
        // before any GL call: resize, reset and upload all change bindings
        GLint                  prevFBO = 0, prevProgram = 0, prevActiveTexture = 0, prevVAO = 0;
        GLint                  prevTexture0 = 0, prevTexture1 = 0;
        std::array<GLint, 4UZ> prevViewport{};
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

        _peakDensity = _peakDensity * decayFactor + 1.f;

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
        _cpuNeedsRecolour = true;
    }

    void cpuRecolour() {
        const float maxDensity = std::max(*std::ranges::max_element(_cpuHistogram), 1.f);
        std::ranges::transform(_cpuHistogram, _cpuPixels.begin(), [&](float density) { return densityColour(density, maxDensity, _cpuColormapLut); });

        glBindTexture(GL_TEXTURE_2D, _cpuTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(_specBins), static_cast<GLsizei>(_ampBins), GL_RGBA, GL_UNSIGNED_BYTE, _cpuPixels.data());
        _cpuNeedsRecolour = false;
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

    void plot(std::span<const float> xValues, double yMin, double yMax) { plot(static_cast<double>(xValues.front()), static_cast<double>(xValues.back()), yMin, yMax); }

    void accumulateAndPlot(const SpectrumFrame& newest, std::size_t nNew, const std::optional<LogFreqRange>& logRange, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap, bool preferGpu) {
        if (logRange) {
            forEachNewSpectrum(newest, nNew, [&](const SpectrumFrame& s) {
                _logRow.resize(kLogSpectrumColumns);
                buildLogBinnedRow(s.xValues, s.yValues, s.nBins, logRange->min, logRange->max, _logRow);
                update(_logRow, kLogSpectrumColumns, ampBins, decayTau, yMin, yMax, colormap, preferGpu);
            });
            plot(logRange->min, logRange->max, yMin, yMax);
        } else {
            forEachNewSpectrum(newest, nNew, [&](const SpectrumFrame& s) { update(s.yValues, s.nBins, ampBins, decayTau, yMin, yMax, colormap, preferGpu); });
            plot(newest.xValues, yMin, yMax);
        }
    }

    void plot(double freqMin, double freqMax, double yMin, double yMax) {
        GLuint tex = _gpuAvailable ? _colormapTexture : _cpuTexture;
        if (!tex) {
            return;
        }
        if (!_gpuAvailable && _cpuNeedsRecolour) {
            cpuRecolour();
        }
        ImPlot::PlotImage("##density", toTextureId(tex), ImPlotPoint(freqMin, yMin), ImPlotPoint(freqMax, yMax));
    }
};

struct FrequencySpan {
    double freqMin;
    double freqMax;
};

/// ring-buffer RGBA8 texture of a scrolling spectrogram:
/// GL_REPEAT on the time axis scrolls by moving the write row and the UVs, without moving data;
/// each new spectrum row is colour-mapped on the CPU and uploaded by a single-row glTexSubImage2D
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
    std::vector<float>         _logRow; // linear spectrum re-binned onto log-spaced columns

    WaterfallBuffer() = default;

    ~WaterfallBuffer() { destroy(); }

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

        // always the GPU texture: PlotImage maps it in screen space, so it renders correctly on log-frequency axes,
        // whereas PlotHeatmap places cells in linear data space and mis-positions log-binned columns.
        // The renderCpu/PlotHeatmap path below is only the fallback for when no GL texture can be allocated.
        glGenTextures(1, &_texture);
        if (_texture != 0U) {
            _pixels.assign(_width * _height, 0U);
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

    template<typename TColourRange>
    [[nodiscard]] std::optional<FrequencySpan> pushNewSpectra(const std::vector<std::shared_ptr<SignalSink>>& sinks, std::size_t& lastPushedSampleCount, const std::optional<LogFreqRange>& logRange, std::size_t nHistory, bool preferGpu, ImPlotColormap colormap, TColourRange&& colourRange) {
        std::optional<FrequencySpan> pushed;
        auto                         pushSpectrum = [&](const SpectrumFrame& f) {
            const std::size_t width = logRange ? kLogSpectrumColumns : f.nBins;
            if (_width != width) {
                init(width, nHistory, preferGpu);
            }

            updateAutoScale(f.yValues, f.nBins);
            const auto [cMin, cMax] = colourRange();
            if (logRange) {
                _logRow.resize(kLogSpectrumColumns);
                buildLogBinnedRow(f.xValues, f.yValues, f.nBins, logRange->min, logRange->max, _logRow);
                pushRow(_logRow, kLogSpectrumColumns, cMin, cMax, timestampFromNanos(f.timestamp), colormap);
                pushed = FrequencySpan{.freqMin = logRange->min, .freqMax = logRange->max};
            } else {
                pushRow(f.yValues, f.nBins, cMin, cMax, timestampFromNanos(f.timestamp), colormap);
                pushed = FrequencySpan{.freqMin = static_cast<double>(f.xValues.front()), .freqMax = static_cast<double>(f.xValues.back())};
            }
        };
        forEachValidSpectrum(sinks, [&](const auto& sink, const SpectrumFrame& newest) {
            forEachNewSpectrum(newest, consumeNewData(lastPushedSampleCount, sink.totalSampleCount()), pushSpectrum);
            return false;
        });
        return pushed;
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

        if (_texture) {
            if (_activeColormap != colormap || (_colormapLut[0] == 0 && _colormapLut[1] == 0)) {
                _colormapLut = buildColormapLut(colormap);
            }

            uint32_t* row = _pixels.data() + _writeRow * _width;
            for (std::size_t i = 0; i < n; ++i) {
                row[i] = colormapLookup(static_cast<double>(magnitudes[i]), scaleMin, scaleMax, _colormapLut);
            }
            std::fill_n(row + n, _width - n, uint32_t(0));

            withTextureBound(_texture, [&] { glTexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(_writeRow), static_cast<GLsizei>(_width), 1, GL_RGBA, GL_UNSIGNED_BYTE, row); });
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

    // `horizontal` transposes the display: frequency on Y, time on X (default: frequency on X, time on Y).
    void render(double freqMin, double freqMax, double timeLo, double timeHi, bool newestLeading = true, bool horizontal = false) const {
        if (_filledRows == 0) {
            return;
        }
        if (!_texture) {
            renderCpu(freqMin, freqMax, timeLo, timeHi, newestLeading, horizontal);
            return;
        }

        // centre-of-texel UVs avoid GL_NEAREST boundary ambiguity at the write-head seam;
        // the time axis (texture V) wraps via GL_REPEAT, so vNewest/vOldest may fall outside [0,1]
        const auto  fHeight = static_cast<float>(_height);
        const float vNewest = (static_cast<float>(_writeRow) - 0.5f) / fHeight;
        const float vOldest = (static_cast<float>(_writeRow) - static_cast<float>(_filledRows) + 0.5f) / fHeight;

        if (horizontal) {
            // PlotImage cannot transpose, so the quad maps texture U (frequency) to Y and V (time) to X:
            // U 0 is freqMin (bottom), 1 freqMax (top); V newest is at the high-time end (right) when newestLeading
            const float  vRight   = newestLeading ? vNewest : vOldest;
            const float  vLeft    = newestLeading ? vOldest : vNewest;
            ImDrawList*  drawList = ImPlot::GetPlotDrawList();
            const ImVec2 topLeft  = ImPlot::PlotToPixels(timeLo, freqMax);
            const ImVec2 topRight = ImPlot::PlotToPixels(timeHi, freqMax);
            const ImVec2 botRight = ImPlot::PlotToPixels(timeHi, freqMin);
            const ImVec2 botLeft  = ImPlot::PlotToPixels(timeLo, freqMin);
            ImPlot::PushPlotClipRect();
            drawList->AddImageQuad(toTextureId(_texture), topLeft, topRight, botRight, botLeft, ImVec2(1.f, vLeft), ImVec2(1.f, vRight), ImVec2(0.f, vRight), ImVec2(0.f, vLeft));
            ImPlot::PopPlotClipRect();
        } else {
            // uv0 maps to (bmin.x, bmax.y) = screen top-left = plot timeHi; newest at top when newestLeading
            const float vTop    = newestLeading ? vNewest : vOldest;
            const float vBottom = newestLeading ? vOldest : vNewest;
            ImPlot::PlotImage("##waterfall", toTextureId(_texture), ImPlotPoint(freqMin, timeLo), ImPlotPoint(freqMax, timeHi), ImVec2(0.0f, vTop), ImVec2(1.0f, vBottom));
        }
    }

    void renderCpu(double freqMin, double freqMax, double timeLo, double timeHi, bool newestLeading, bool horizontal = false) const {
        if (horizontal) {
            renderCpuHorizontal(freqMin, freqMax, timeLo, timeHi, newestLeading);
            return;
        }
        _linearized.resize(_filledRows * _width);
        for (std::size_t i = 0; i < _filledRows; ++i) {
            std::size_t srcRow = newestLeading ? (_writeRow + _height - 1 - i) % _height : (_writeRow + _height - _filledRows + i) % _height;
            std::copy_n(_rawMagnitudes.data() + srcRow * _width, _width, _linearized.data() + i * _width);
        }

        ImPlot::PushColormap(_activeColormap);
        ImPlot::PlotHeatmap("##waterfall", _linearized.data(), static_cast<int>(_filledRows), static_cast<int>(_width), _effectiveScaleMin, _effectiveScaleMax, nullptr, ImPlotPoint(freqMin, timeLo), ImPlotPoint(freqMax, timeHi));
        ImPlot::PopColormap();
    }

    // transposed CPU fallback: rows = frequency (Y), cols = time (X)
    void renderCpuHorizontal(double freqMin, double freqMax, double timeLo, double timeHi, bool newestLeading) const {
        _linearized.resize(_width * _filledRows);
        for (std::size_t r = 0; r < _width; ++r) {
            const std::size_t freqCol = _width - 1 - r;
            for (std::size_t c = 0; c < _filledRows; ++c) {
                const std::size_t order          = newestLeading ? c : (_filledRows - 1 - c); // col 0 = left = oldest when newest trails right
                const std::size_t srcRow         = (_writeRow + _height - _filledRows + order) % _height;
                _linearized[r * _filledRows + c] = _rawMagnitudes[srcRow * _width + freqCol];
            }
        }
        ImPlot::PushColormap(_activeColormap);
        ImPlot::PlotHeatmap("##waterfall", _linearized.data(), static_cast<int>(_width), static_cast<int>(_filledRows), _effectiveScaleMin, _effectiveScaleMax, nullptr, ImPlotPoint(timeLo, freqMin), ImPlotPoint(timeHi, freqMax));
        ImPlot::PopColormap();
    }

    void resizeHistory(std::size_t newHeight) {
        if (newHeight == _height || _width == 0) {
            return;
        }

        const std::size_t rowsToCopy = std::min(_filledRows, newHeight);
        if (_texture) {
            _pixels = resizeRing(_pixels, _width, _height, _writeRow, rowsToCopy, newHeight);
            withTextureBound(_texture, [&] { glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(_width), static_cast<GLsizei>(newHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data()); });
        } else {
            _rawMagnitudes = resizeRing(_rawMagnitudes, _width, _height, _writeRow, rowsToCopy, newHeight);
        }
        _timestamps = resizeRing(_timestamps, 1UZ, _height, _writeRow, rowsToCopy, newHeight);
        _height     = newHeight;
        _filledRows = rowsToCopy;
        _writeRow   = 0;
    }

    void clear() {
        if (_texture) {
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
            withTextureBound(_texture, [&] { glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(_width), static_cast<GLsizei>(_height), GL_RGBA, GL_UNSIGNED_BYTE, _pixels.data()); });
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
        // non-finite bins ignored: a dB spectrum has -inf where the magnitude is ~0 (e.g. empty bands of a wideband FFT),
        // which would drag scaleMin to -inf and collapse the whole colour mapping
        double fMin = std::numeric_limits<double>::infinity();
        double fMax = -std::numeric_limits<double>::infinity();
        for (const float y : yValues | std::views::take(static_cast<std::ptrdiff_t>(nBins))) {
            if (!std::isfinite(y)) {
                continue;
            }
            fMin = std::min(fMin, static_cast<double>(y));
            fMax = std::max(fMax, static_cast<double>(y));
        }
        if (fMax <= fMin) {
            return; // no finite range this frame -> keep the previous colour scale
        }
        followColourScale(_scaleMin, _scaleMax, fMin, fMax, _filledRows == 0);
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

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_SPECTRUMHELPER_HPP
