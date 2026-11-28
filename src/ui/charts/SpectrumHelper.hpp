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

[[nodiscard]] constexpr double timestampFromNanos(int64_t ns) { return static_cast<double>(ns) * 1e-9; }

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

inline constexpr std::size_t kLogSpectrumColumns = 2048;

/// peak-preserving maximum per log-spaced column over [fMin, fMax], columns finer than the bin spacing filled: keeps low-frequency detail on a log axis
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
    for (float& v : out) {
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
    for (float& v : out) {
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

/// a dB spectrum has -inf where the magnitude is 0, which would drag a colour scale with it
[[nodiscard]] inline std::optional<std::pair<double, double>> finiteRange(std::ranges::forward_range auto&& values) {
    auto finite = values | std::views::transform([](auto value) { return static_cast<double>(value); }) | std::views::filter([](double value) { return std::isfinite(value); });
    if (std::ranges::empty(finite)) {
        return std::nullopt;
    }
    const auto [lowest, highest] = std::ranges::minmax(finite);
    return std::pair{lowest, highest};
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

enum class DensityNormalisation : std::uint8_t {
    peak,   // a cell every spectrum hits is at full scale
    maximum // the densest cell is at full scale
};

/// a frequency-vs-amplitude density of successive spectra, row 0 at yMax:
/// each spectrum decays every cell by (1 − 1/τ) (τ = 0: no decay) and adds 1 to the cell its value lies in, per column, clamped to the edge rows;
/// NaN adds nothing.
/// `peak` is the density of a cell every spectrum hits, p ← p (1 − 1/τ) + 1, which converges to τ
struct DensityGrid {
    std::size_t        columns = 0UZ;
    std::size_t        rows    = 0UZ;
    std::vector<float> cells; // [rows, columns]
    float              peak = 0.f;

    void resize(std::size_t nColumns, std::size_t nRows) {
        columns = nColumns;
        rows    = nRows;
        cells.assign(columns * rows, 0.f);
        peak = 0.f;
    }

    void clear() {
        std::ranges::fill(cells, 0.f);
        peak = 0.f;
    }

    void accumulate(std::span<const float> spectrum, double yMin, double yMax, double decayTau) {
        const float decay = decayTau > 0.0 ? static_cast<float>(1.0 - 1.0 / decayTau) : 1.f;
        std::ranges::transform(cells, cells.begin(), [decay](float cell) { return cell * decay; });
        peak = peak * decay + 1.f;
        if (yMax <= yMin || rows == 0UZ) {
            return;
        }
        const double rowsPerUnit = static_cast<double>(rows) / (yMax - yMin);
        const double lastRow     = static_cast<double>(rows - 1UZ);
        for (std::size_t column = 0UZ; column < std::min(columns, spectrum.size()); ++column) {
            const auto value = static_cast<double>(spectrum[column]);
            if (std::isnan(value)) {
                continue;
            }
            const auto row = static_cast<std::size_t>(std::clamp((yMax - value) * rowsPerUnit, 0.0, lastRow));
            cells[row * columns + column] += 1.f;
        }
    }

    /// the density drawn at full scale, at least 1
    [[nodiscard]] float fullScale(DensityNormalisation normalisation) const {
        const float densest = cells.empty() ? 0.f : *std::ranges::max_element(cells);
        return std::max(normalisation == DensityNormalisation::peak ? peak : densest, 1.f);
    }
};

/// a DensityGrid drawn with ImPlot::PlotImage: its cells go up as an R32F texture,
/// and one GLSL pass divides them by the full scale and looks the result up in the colormap, into an RGBA8 image;
/// a cell below 1/255 of full scale is empty
struct DensityHistogram {
    DensityGrid        _grid;
    double             _binningYMin = 0.0;
    double             _binningYMax = 0.0;
    std::vector<float> _logBinnedRow;
    gl::Texture2D      _cells{{.format = gl::Texture2D::Format::r32f}};
    gl::Texture2D      _colormapLut{{.format = gl::Texture2D::Format::rgba8, .minFilter = GL_LINEAR, .magFilter = GL_LINEAR}};
    gl::Texture2D      _image;
    GLuint             _imageFBO       = 0;
    GLuint             _program        = 0;
    GLuint             _emptyVAO       = 0;
    GLint              _locCells       = -1;
    GLint              _locLut         = -1;
    GLint              _locFullScale   = -1;
    bool               _initAttempted  = false;
    bool               _needsColouring = false;
    ImPlotColormap     _activeColormap = -1;

    DensityHistogram()                                   = default;
    DensityHistogram(const DensityHistogram&)            = delete;
    DensityHistogram& operator=(const DensityHistogram&) = delete;

    ~DensityHistogram() {
        if (gl::hasCurrentContext()) {
            glDeleteFramebuffers(1, &_imageFBO);
            glDeleteProgram(_program);
            glDeleteVertexArrays(1, &_emptyVAO);
        }
    }

    void reset() {
        _grid.clear();
        _needsColouring = true;
    }

    void update(std::span<const float> spectrum, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap) {
        if (_grid.columns != spectrum.size() || _grid.rows != ampBins) {
            _grid.resize(spectrum.size(), ampBins);
        } else if (_binningYMin != yMin || _binningYMax != yMax) {
            _grid.clear(); // the cells mean other amplitudes now
        }
        _binningYMin = yMin;
        _binningYMax = yMax;
        _grid.accumulate(spectrum, yMin, yMax, decayTau);
        if (_activeColormap != colormap) {
            _activeColormap = colormap;
            const auto lut  = buildColormapLut(colormap);
            std::ignore     = _colormapLut.upload(kColormapSize, 1UZ, lut.data());
        }
        _needsColouring = true;
    }

    void accumulateAndPlot(const SpectrumFrame& newest, std::size_t nNew, const std::optional<LogFreqRange>& logRange, std::size_t ampBins, double decayTau, double yMin, double yMax, ImPlotColormap colormap, DensityNormalisation normalisation) {
        if (logRange) {
            forEachNewSpectrum(newest, nNew, [&](const SpectrumFrame& s) {
                _logBinnedRow.resize(kLogSpectrumColumns);
                buildLogBinnedRow(s.xValues, s.yValues, s.nBins, logRange->min, logRange->max, _logBinnedRow);
                update(_logBinnedRow, ampBins, decayTau, yMin, yMax, colormap);
            });
            plot(logRange->min, logRange->max, yMin, yMax, normalisation);
        } else {
            forEachNewSpectrum(newest, nNew, [&](const SpectrumFrame& s) { update(s.yValues.first(s.nBins), ampBins, decayTau, yMin, yMax, colormap); });
            plot(static_cast<double>(newest.xValues.front()), static_cast<double>(newest.xValues.back()), yMin, yMax, normalisation);
        }
    }

    void plot(double freqMin, double freqMax, double yMin, double yMax, DensityNormalisation normalisation) {
        if (_grid.cells.empty() || !initProgram()) {
            return;
        }
        if (_needsColouring) {
            colour(_grid.fullScale(normalisation));
            _needsColouring = false;
        }
        ImPlot::PlotImage("##density", _image.id(), ImPlotPoint(freqMin, yMin), ImPlotPoint(freqMax, yMax));
    }

    [[nodiscard]] bool initProgram() {
        if (!_initAttempted) {
            _initAttempted = true;
            // a fullscreen triangle from gl_VertexID, with an empty VAO: (-1,-1), (3,-1), (-1,3), clipped to the viewport
            static constexpr auto* kVertBody   = R"glsl(
out vec2 v_uv;
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    v_uv = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";
            static constexpr auto* kColourBody = R"glsl(
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_cells;       // R32F, the DensityGrid's cells
uniform sampler2D u_colormapLut; // RGBA8, 256 x 1
uniform float     u_fullScale;
void main() {
    float norm = clamp(texture(u_cells, v_uv).r / u_fullScale, 0.0, 1.0);
    fragColor  = norm < 1.0 / 255.0 ? vec4(0.0) : texture(u_colormapLut, vec2(norm, 0.5));
}
)glsl";
            const GLuint           vs          = gl::compileShader(GL_VERTEX_SHADER, gl::kGlslPrefix, kVertBody);
            const GLuint           fs          = gl::compileShader(GL_FRAGMENT_SHADER, gl::kGlslPrefix, kColourBody);
            _program                           = gl::linkProgram(vs, fs);
            glDeleteShader(vs);
            glDeleteShader(fs);
            if (_program != 0U) {
                _locCells     = glGetUniformLocation(_program, "u_cells");
                _locLut       = glGetUniformLocation(_program, "u_colormapLut");
                _locFullScale = glGetUniformLocation(_program, "u_fullScale");
                glGenVertexArrays(1, &_emptyVAO);
            } else {
                std::println(stderr, "[DensityHistogram] the colour-map shader did not build: no density image");
            }
        }
        return _program != 0U;
    }

    void colour(float fullScale) {
        const bool resized = _image.width() != _grid.columns || _image.height() != _grid.rows;
        if (!_cells.upload(_grid.columns, _grid.rows, _grid.cells.data()) || (resized && !_image.upload(_grid.columns, _grid.rows, nullptr))) {
            return;
        }
        if (resized) {
            glDeleteFramebuffers(1, &_imageFBO);
            _imageFBO = gl::attachFBO(_image.handle());
        }
        GLint                  previousFBO = 0, previousProgram = 0, previousActiveTexture = 0, previousVAO = 0, previousTexture0 = 0, previousTexture1 = 0;
        std::array<GLint, 4UZ> previousViewport{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
        glBindTexture(GL_TEXTURE_2D, _cells.handle());
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture1);
        glBindTexture(GL_TEXTURE_2D, _colormapLut.handle());

        glBindFramebuffer(GL_FRAMEBUFFER, _imageFBO);
        glViewport(0, 0, static_cast<GLsizei>(_grid.columns), static_cast<GLsizei>(_grid.rows));
        glUseProgram(_program);
        glUniform1i(_locCells, 0);
        glUniform1i(_locLut, 1);
        glUniform1f(_locFullScale, fullScale);
        glBindVertexArray(_emptyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture1));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
        glActiveTexture(static_cast<GLenum>(previousActiveTexture));
        glBindVertexArray(static_cast<GLuint>(previousVAO));
        glUseProgram(static_cast<GLuint>(previousProgram));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFBO));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
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
    std::vector<double>   _timestamps;                                          // UTC seconds per row (parallel ring buffer)
    gl::Texture2D         _texture{gl::Texture2D::Options{.wrapT = GL_REPEAT}}; // time wraps: the write row scrolls

    ImPlotColormap                      _activeColormap = -1;
    std::array<uint32_t, kColormapSize> _colormapLut{};

    double _scaleMin = 0.0;
    double _scaleMax = 0.0;

    std::vector<float> _logBinnedRow;

    WaterfallBuffer() = default;

    ~WaterfallBuffer() { destroy(); }

    WaterfallBuffer(const WaterfallBuffer&)            = delete;
    WaterfallBuffer& operator=(const WaterfallBuffer&) = delete;

    void init(std::size_t width, std::size_t height) {
        destroy();
        _width      = width;
        _height     = height;
        _writeRow   = 0;
        _filledRows = 0;
        _scaleMin   = 0.0;
        _scaleMax   = 0.0;

        _timestamps.assign(_height, 0.0);

        // PlotImage maps the texture in screen space; PlotHeatmap's linear cells misplace log-binned columns
        _pixels.assign(_width * _height, 0U);
        if (!_texture.upload(_width, _height, _pixels.data())) {
            _pixels.clear(); // no GL context: nothing is drawn
        }
    }

    template<typename TColourRange>
    [[nodiscard]] std::optional<FrequencySpan> pushNewSpectra(const std::vector<std::shared_ptr<SignalSink>>& sinks, std::size_t& lastPushedSampleCount, const std::optional<LogFreqRange>& logRange, std::size_t nHistory, ImPlotColormap colormap, TColourRange&& colourRange) {
        std::optional<FrequencySpan> pushed;
        auto                         pushSpectrum = [&](const SpectrumFrame& f) {
            const std::size_t width = logRange ? kLogSpectrumColumns : f.nBins;
            if (_width != width) {
                init(width, nHistory);
            }

            updateAutoScale(f.yValues, f.nBins);
            const auto [cMin, cMax] = colourRange();
            if (logRange) {
                _logBinnedRow.resize(kLogSpectrumColumns);
                buildLogBinnedRow(f.xValues, f.yValues, f.nBins, logRange->min, logRange->max, _logBinnedRow);
                pushRow(_logBinnedRow, kLogSpectrumColumns, cMin, cMax, timestampFromNanos(f.timestamp), colormap);
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

    void pushRow(std::span<const float> magnitudes, std::size_t count, double scaleMin, double scaleMax, double timestampSec, ImPlotColormap colormap) {
        if (_width == 0 || _height == 0 || !_texture) {
            return;
        }

        const auto n = std::min(count, _width);
        if (_activeColormap != colormap || (_colormapLut[0] == 0 && _colormapLut[1] == 0)) {
            _colormapLut = buildColormapLut(colormap);
        }

        uint32_t* row = _pixels.data() + _writeRow * _width;
        for (std::size_t i = 0; i < n; ++i) {
            row[i] = colormapLookup(static_cast<double>(magnitudes[i]), scaleMin, scaleMax, _colormapLut);
        }
        std::fill_n(row + n, _width - n, uint32_t(0));

        _texture.uploadRows(_writeRow, 1UZ, row);

        _activeColormap        = colormap;
        _timestamps[_writeRow] = timestampSec;
        _writeRow              = (_writeRow + 1) % _height;
        if (_filledRows < _height) {
            ++_filledRows;
        }
    }

    // `horizontal` transposes the display: frequency on Y, time on X (default: frequency on X, time on Y).
    void render(double freqMin, double freqMax, double timeLo, double timeHi, bool newestLeading = true, bool horizontal = false) const {
        if (_filledRows == 0 || !_texture) {
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
            drawList->AddImageQuad(_texture.id(), topLeft, topRight, botRight, botLeft, ImVec2(1.f, vLeft), ImVec2(1.f, vRight), ImVec2(0.f, vRight), ImVec2(0.f, vLeft));
            ImPlot::PopPlotClipRect();
        } else {
            // uv0 maps to (bmin.x, bmax.y) = screen top-left = plot timeHi; newest at top when newestLeading
            const float vTop    = newestLeading ? vNewest : vOldest;
            const float vBottom = newestLeading ? vOldest : vNewest;
            ImPlot::PlotImage("##waterfall", _texture.id(), ImPlotPoint(freqMin, timeLo), ImPlotPoint(freqMax, timeHi), ImVec2(0.0f, vTop), ImVec2(1.0f, vBottom));
        }
    }

    void resizeHistory(std::size_t newHeight) {
        if (newHeight == _height || _width == 0) {
            return;
        }

        const std::size_t rowsToCopy = std::min(_filledRows, newHeight);
        if (_texture) {
            _pixels     = resizeRing(_pixels, _width, _height, _writeRow, rowsToCopy, newHeight);
            std::ignore = _texture.upload(_width, newHeight, _pixels.data());
        }
        _timestamps = resizeRing(_timestamps, 1UZ, _height, _writeRow, rowsToCopy, newHeight);
        _height     = newHeight;
        _filledRows = rowsToCopy;
        _writeRow   = 0;
    }

    void clear() {
        std::ranges::fill(_pixels, uint32_t(0));
        std::ranges::fill(_timestamps, 0.0);
        _writeRow   = 0;
        _filledRows = 0;
        _scaleMin   = 0.0;
        _scaleMax   = 0.0;

        _texture.uploadRows(0UZ, _height, _pixels.data());
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
        const auto range = finiteRange(yValues.first(std::min(nBins, yValues.size())));
        if (!range || range->second <= range->first) {
            return; // no finite range this frame: the previous colour scale stays
        }
        followColourScale(_scaleMin, _scaleMax, range->first, range->second, _filledRows == 0);
    }

    void destroy() {
        _texture.reset();
        _pixels.clear();
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
