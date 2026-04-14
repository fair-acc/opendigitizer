#ifndef OPENDIGITIZER_CHARTS_SURFACEPLOT_HPP
#define OPENDIGITIZER_CHARTS_SURFACEPLOT_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include <imgui.h>
#include <implot3d.h>
#include <implot3d_internal.h>

namespace opendigitizer::charts {

struct SurfaceBuffer {
    std::size_t _spectrumWidth = 0;
    std::size_t _historyDepth  = 0;
    std::size_t _writeRow      = 0;
    std::size_t _filledRows    = 0;
    bool        _dirty         = false;

    std::vector<float>  _magnitudes; // ring buffer: _historyDepth rows of _spectrumWidth values
    std::vector<double> _timestamps; // UTC seconds per row
    std::vector<float>  _freqAxis;   // frequency axis from last push (shared across rows)

    double _scaleMin = 0.0;
    double _scaleMax = 0.0;

    // linearised arrays for PlotSurface CPU fallback (rebuilt each frame from ring buffer)
    mutable std::vector<double> _xCoords;
    mutable std::vector<double> _yCoords;
    mutable std::vector<double> _zCoords;

    void init(std::size_t width, std::size_t depth) {
        _spectrumWidth = width;
        _historyDepth  = depth;
        _writeRow      = 0;
        _filledRows    = 0;
        _scaleMin      = 0.0;
        _scaleMax      = 0.0;
        _dirty         = true;
        _magnitudes.assign(width * depth, 0.0f);
        _timestamps.assign(depth, 0.0);
        _freqAxis.clear();
    }

    void pushRow(std::span<const float> xValues, std::span<const float> yValues, std::size_t count, double timestampSec) {
        if (_spectrumWidth == 0 || _historyDepth == 0) {
            return;
        }

        auto n = std::min(count, _spectrumWidth);

        float* row = _magnitudes.data() + _writeRow * _spectrumWidth;
        std::copy_n(yValues.data(), n, row);
        std::fill_n(row + n, _spectrumWidth - n, 0.0f);

        _timestamps[_writeRow] = timestampSec;
        _writeRow              = (_writeRow + 1) % _historyDepth;
        if (_filledRows < _historyDepth) {
            ++_filledRows;
        }

        if (_freqAxis.size() != n) {
            _freqAxis.assign(xValues.begin(), xValues.begin() + static_cast<std::ptrdiff_t>(n));
        }

        _dirty = true;
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

    void buildSurfaceArrays() const {
        if (_filledRows == 0 || _freqAxis.empty()) {
            return;
        }

        const auto nX     = _freqAxis.size();
        const auto nY     = _filledRows;
        const auto nTotal = nX * nY;

        _xCoords.resize(nTotal);
        _yCoords.resize(nTotal);
        _zCoords.resize(nTotal);

        for (std::size_t row = 0; row < nY; ++row) {
            std::size_t srcRow = (_writeRow + _historyDepth - nY + row) % _historyDepth;
            double      rowY   = static_cast<double>(relativeTime(srcRow));

            const float* mag = _magnitudes.data() + srcRow * _spectrumWidth;
            for (std::size_t col = 0; col < nX; ++col) {
                std::size_t idx = row * nX + col;
                _xCoords[idx]   = static_cast<double>(_freqAxis[col]);
                _yCoords[idx]   = rowY;
                _zCoords[idx]   = static_cast<double>(mag[col]);
            }
        }
    }

    void resizeHistory(std::size_t newDepth) {
        if (newDepth == _historyDepth || _spectrumWidth == 0) {
            return;
        }

        std::vector<float>  newMag(newDepth * _spectrumWidth, 0.0f);
        std::vector<double> newTs(newDepth, 0.0);
        std::size_t         rowsToCopy = std::min(_filledRows, newDepth);

        for (std::size_t i = 0; i < rowsToCopy; ++i) {
            std::size_t srcRow = (_writeRow + _historyDepth - rowsToCopy + i) % _historyDepth;
            std::size_t dstRow = (newDepth - rowsToCopy + i) % newDepth;
            std::copy_n(_magnitudes.data() + srcRow * _spectrumWidth, _spectrumWidth, newMag.data() + dstRow * _spectrumWidth);
            newTs[dstRow] = _timestamps[srcRow];
        }

        _magnitudes   = std::move(newMag);
        _timestamps   = std::move(newTs);
        _historyDepth = newDepth;
        _filledRows   = rowsToCopy;
        _writeRow     = 0;
        _dirty        = true;
    }

    void clear() {
        std::ranges::fill(_magnitudes, 0.0f);
        std::ranges::fill(_timestamps, 0.0);
        _writeRow   = 0;
        _filledRows = 0;
        _scaleMin   = 0.0;
        _scaleMax   = 0.0;
        _dirty      = true;
    }

    [[nodiscard]] std::size_t width() const noexcept { return _spectrumWidth; }
    [[nodiscard]] std::size_t filledRows() const noexcept { return _filledRows; }
    [[nodiscard]] double      scaleMin() const noexcept { return _scaleMin; }
    [[nodiscard]] double      scaleMax() const noexcept { return _scaleMax; }

    [[nodiscard]] std::pair<double, double> timeBounds() const {
        if (_filledRows < 2) {
            return {0.0, 0.0};
        }
        std::size_t newestIdx = (_writeRow + _historyDepth - 1) % _historyDepth;
        std::size_t oldestIdx = (_writeRow + _historyDepth - _filledRows) % _historyDepth;
        double      tNewest   = _timestamps[newestIdx];
        double      tOldest   = _timestamps[oldestIdx];
        return {tOldest - tNewest, 0.0}; // relative: negative offset for oldest, 0 for newest
    }

    [[nodiscard]] float relativeTime(std::size_t ringRow) const {
        if (_filledRows < 2) {
            return 0.0f;
        }
        std::size_t newestIdx = (_writeRow + _historyDepth - 1) % _historyDepth;
        double      tNewest   = _timestamps[newestIdx];
        return static_cast<float>(_timestamps[ringRow] - tNewest); // negative for older rows
    }
};

namespace gl = opendigitizer::shader;

struct SurfaceGpuRenderer {
    static constexpr std::size_t kMaxDisplayDim = 512UZ;

    GLuint         _program            = 0;
    GLuint         _vao                = 0;
    GLuint         _vbo                = 0;
    GLuint         _ibo                = 0;
    GLuint         _colormapLutTexture = 0;
    ImPlotColormap _activeColormap     = -1;

    GLint _locAxisMin          = -1;
    GLint _locAxisMax          = -1;
    GLint _locAxisInvRange     = -1;
    GLint _locNdcScale         = -1;
    GLint _locRotation         = -1;
    GLint _locViewScale        = -1;
    GLint _locScreenCenter     = -1;
    GLint _locInvScreenSize    = -1;
    GLint _locColormapRange    = -1;
    GLint _locColormapLut      = -1;
    GLint _locGridOrigin       = -1;
    GLint _locGridSpacing      = -1;
    GLint _locGridMinorSpacing = -1;
    GLint _locGridEnabled      = -1;
    GLint _locGridColor        = -1;

    std::size_t _indexCount = 0;

    bool _initAttempted = false;
    bool _gpuAvailable  = false;

    struct FrameState {
        float axisMin[3]{};
        float axisMax[3]{};
        float axisInvRange[3]{};
        float ndcScale[3]{};
        float rotation[4]{0, 0, 0, 1};
        float viewScale = 0;
        float screenCenter[2]{};
        float invScreenSize[2]{};
        float colormapRange[2]{};
        float plotRectMin[2]{};
        float plotRectMax[2]{};
        float fbHeight = 0;
        float gridOrigin[2]{};       // X,Y origin for grid lines (first major tick)
        float gridSpacing[2]{};      // X,Y major tick interval
        float gridMinorSpacing[2]{}; // X,Y minor tick interval
        int   gridEnabled = 0;       // 0=off, 1=major only, 2=major+minor
        float gridColor[4]{1.f, 1.f, 1.f, 0.3f};
    };
    FrameState _frame{};

    SurfaceGpuRenderer() = default;

    ~SurfaceGpuRenderer() { destroy(); }

    SurfaceGpuRenderer(SurfaceGpuRenderer&& o) noexcept { swap(o); }

    SurfaceGpuRenderer& operator=(SurfaceGpuRenderer&& o) noexcept {
        if (this != &o) {
            destroy();
            swap(o);
        }
        return *this;
    }

    SurfaceGpuRenderer(const SurfaceGpuRenderer&)            = delete;
    SurfaceGpuRenderer& operator=(const SurfaceGpuRenderer&) = delete;

    void swap(SurfaceGpuRenderer& o) noexcept {
        using std::swap;
        swap(_program, o._program);
        swap(_vao, o._vao);
        swap(_vbo, o._vbo);
        swap(_ibo, o._ibo);
        swap(_colormapLutTexture, o._colormapLutTexture);
        swap(_activeColormap, o._activeColormap);
        swap(_locAxisMin, o._locAxisMin);
        swap(_locAxisMax, o._locAxisMax);
        swap(_locAxisInvRange, o._locAxisInvRange);
        swap(_locNdcScale, o._locNdcScale);
        swap(_locRotation, o._locRotation);
        swap(_locViewScale, o._locViewScale);
        swap(_locScreenCenter, o._locScreenCenter);
        swap(_locInvScreenSize, o._locInvScreenSize);
        swap(_locColormapRange, o._locColormapRange);
        swap(_locColormapLut, o._locColormapLut);
        swap(_locGridOrigin, o._locGridOrigin);
        swap(_locGridSpacing, o._locGridSpacing);
        swap(_locGridEnabled, o._locGridEnabled);
        swap(_indexCount, o._indexCount);
        swap(_initAttempted, o._initAttempted);
        swap(_gpuAvailable, o._gpuAvailable);
        swap(_frame, o._frame);
    }

    void init() {
        if (_initAttempted) {
            return;
        }
        _initAttempted = true;
        _gpuAvailable  = tryInitGpu();
        if (_gpuAvailable) {
            std::println("[SurfacePlot] GPU mesh renderer initialised");
        } else {
            std::println("[SurfacePlot] GPU init failed, using CPU fallback");
        }
    }

    [[nodiscard]] bool tryInitGpu() {
        static constexpr auto* kVertBody = R"glsl(
in vec3 a_position;
out float v_zNorm;
out vec3  v_worldPos;

uniform vec3  u_axisMin;
uniform vec3  u_axisInvRange;
uniform vec3  u_ndcScale;
uniform vec4  u_rotation;
uniform float u_viewScale;
uniform vec2  u_screenCenter;
uniform vec2  u_invScreenSize;
uniform vec2  u_colormapRange;

vec3 quatRotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

void main() {
    vec3 t   = (a_position - u_axisMin) * u_axisInvRange;
    vec3 ndc = (t - 0.5) * u_ndcScale;

    vec3 rotated = quatRotate(u_rotation, ndc);

    vec2 pix;
    pix.x = u_viewScale * rotated.x + u_screenCenter.x;
    pix.y = u_viewScale * (-rotated.y) + u_screenCenter.y;

    float depth = clamp(-rotated.z * 2.0, -1.0, 1.0);
    gl_Position = vec4(pix.x * 2.0 * u_invScreenSize.x - 1.0,
                       1.0 - pix.y * 2.0 * u_invScreenSize.y,
                       depth, 1.0);

    float r = u_colormapRange.y - u_colormapRange.x;
    v_zNorm    = r > 0.0 ? clamp((a_position.z - u_colormapRange.x) / r, 0.0, 1.0) : 0.5;
    v_worldPos = a_position;
}
)glsl";

        static constexpr auto* kFragBody = R"glsl(
in float v_zNorm;
in vec3  v_worldPos;
out vec4 fragColor;

uniform vec3      u_axisMin;
uniform vec3      u_axisMax;
uniform sampler2D u_colormapLut;
uniform vec2      u_gridOrigin;
uniform vec2      u_gridSpacing;
uniform vec2      u_gridMinorSpacing;
uniform int       u_gridEnabled;
uniform vec4      u_gridColor;

void main() {
    if (any(lessThan(v_worldPos, u_axisMin)) || any(greaterThan(v_worldPos, u_axisMax)))
        discard;

    vec4 color = texture(u_colormapLut, vec2(v_zNorm, 0.5));

    if (u_gridEnabled != 0) {
        float wx = fwidth(v_worldPos.x);
        float wy = fwidth(v_worldPos.y);

        // major grid lines
        if (u_gridSpacing.x > 0.0 && u_gridSpacing.y > 0.0) {
            float dx = abs(mod(v_worldPos.x - u_gridOrigin.x + u_gridSpacing.x * 0.5, u_gridSpacing.x) - u_gridSpacing.x * 0.5);
            float dy = abs(mod(v_worldPos.y - u_gridOrigin.y + u_gridSpacing.y * 0.5, u_gridSpacing.y) - u_gridSpacing.y * 0.5);
            float lineX = 1.0 - smoothstep(0.0, wx * 1.5, dx);
            float lineY = 1.0 - smoothstep(0.0, wy * 1.5, dy);
            float major = max(lineX, lineY);
            color.rgb = mix(color.rgb, u_gridColor.rgb, major * u_gridColor.a);
        }

        // minor grid lines (thinner, fainter)
        if (u_gridEnabled >= 2 && u_gridMinorSpacing.x > 0.0 && u_gridMinorSpacing.y > 0.0) {
            float dxm = abs(mod(v_worldPos.x - u_gridOrigin.x + u_gridMinorSpacing.x * 0.5, u_gridMinorSpacing.x) - u_gridMinorSpacing.x * 0.5);
            float dym = abs(mod(v_worldPos.y - u_gridOrigin.y + u_gridMinorSpacing.y * 0.5, u_gridMinorSpacing.y) - u_gridMinorSpacing.y * 0.5);
            float mLineX = 1.0 - smoothstep(0.0, wx * 1.0, dxm);
            float mLineY = 1.0 - smoothstep(0.0, wy * 1.0, dym);
            float minor  = max(mLineX, mLineY);
            color.rgb = mix(color.rgb, u_gridColor.rgb, minor * u_gridColor.a * 0.35);
        }
    }

    fragColor = color;
}
)glsl";

        GLuint vs = gl::compileShader(GL_VERTEX_SHADER, gl::kGlslPrefix, kVertBody);
        GLuint fs = gl::compileShader(GL_FRAGMENT_SHADER, gl::kGlslPrefix, kFragBody);
        _program  = gl::linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        if (!_program) {
            return false;
        }

        _locAxisMin          = glGetUniformLocation(_program, "u_axisMin");
        _locAxisMax          = glGetUniformLocation(_program, "u_axisMax");
        _locAxisInvRange     = glGetUniformLocation(_program, "u_axisInvRange");
        _locNdcScale         = glGetUniformLocation(_program, "u_ndcScale");
        _locRotation         = glGetUniformLocation(_program, "u_rotation");
        _locViewScale        = glGetUniformLocation(_program, "u_viewScale");
        _locScreenCenter     = glGetUniformLocation(_program, "u_screenCenter");
        _locInvScreenSize    = glGetUniformLocation(_program, "u_invScreenSize");
        _locColormapRange    = glGetUniformLocation(_program, "u_colormapRange");
        _locColormapLut      = glGetUniformLocation(_program, "u_colormapLut");
        _locGridOrigin       = glGetUniformLocation(_program, "u_gridOrigin");
        _locGridSpacing      = glGetUniformLocation(_program, "u_gridSpacing");
        _locGridMinorSpacing = glGetUniformLocation(_program, "u_gridMinorSpacing");
        _locGridEnabled      = glGetUniformLocation(_program, "u_gridEnabled");
        _locGridColor        = glGetUniformLocation(_program, "u_gridColor");

        glGenVertexArrays(1, &_vao);
        glGenBuffers(1, &_vbo);
        glGenBuffers(1, &_ibo);

        glBindVertexArray(_vao);
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
        glBindVertexArray(0);

        return true;
    }

    void uploadMesh(const SurfaceBuffer& buf) {
        if (!_gpuAvailable || buf._filledRows < 2 || buf._freqAxis.empty()) {
            return;
        }

        const auto srcNX = buf._freqAxis.size();
        const auto srcNY = buf._filledRows;

        const auto strideX   = std::max(std::size_t{1}, srcNX / kMaxDisplayDim);
        const auto strideY   = std::max(std::size_t{1}, srcNY / kMaxDisplayDim);
        const auto displayNX = (srcNX + strideX - 1) / strideX;
        const auto displayNY = (srcNY + strideY - 1) / strideY;

        std::vector<float> vertices(displayNX * displayNY * 3);

        for (std::size_t dy = 0; dy < displayNY; ++dy) {
            const auto   srcY    = std::min(dy * strideY, srcNY - 1);
            const auto   ringRow = (buf._writeRow + buf._historyDepth - srcNY + srcY) % buf._historyDepth;
            const float  rowTime = buf.relativeTime(ringRow);
            const float* mag     = buf._magnitudes.data() + ringRow * buf._spectrumWidth;

            for (std::size_t dx = 0; dx < displayNX; ++dx) {
                const auto srcX  = std::min(dx * strideX, srcNX - 1);
                const auto vi    = (dy * displayNX + dx) * 3;
                vertices[vi + 0] = buf._freqAxis[srcX];
                vertices[vi + 1] = rowTime;
                vertices[vi + 2] = mag[srcX];
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);

        _indexCount = (displayNX - 1) * (displayNY - 1) * 6;
        std::vector<uint32_t> indices(_indexCount);
        std::size_t           ii = 0;
        for (std::size_t row = 0; row < displayNY - 1; ++row) {
            for (std::size_t col = 0; col < displayNX - 1; ++col) {
                auto v00      = static_cast<uint32_t>(row * displayNX + col);
                auto v10      = v00 + 1;
                auto v01      = static_cast<uint32_t>((row + 1) * displayNX + col);
                auto v11      = v01 + 1;
                indices[ii++] = v00;
                indices[ii++] = v10;
                indices[ii++] = v01;
                indices[ii++] = v10;
                indices[ii++] = v11;
                indices[ii++] = v01;
            }
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    }

    void updateColormapLut(ImPlotColormap colormap) {
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

    void captureGridFromTickers(const ImPlot3DPlot* plot, bool showGrid, bool showMinorGrid, const float gridColor[4]) {
        _frame.gridEnabled = showGrid ? (showMinorGrid ? 2 : 1) : 0;
        std::copy_n(gridColor, 4, _frame.gridColor);
        if (!plot || !showGrid) {
            return;
        }
        struct TickSpacing {
            float origin = 0.f;
            float major  = 0.f;
            float minor  = 0.f;
        };
        auto extractSpacing = [](const ImPlot3DAxis& ax) -> TickSpacing {
            TickSpacing result;
            double      prevMajor = 0.0, prevMinor = 0.0;
            bool        foundMajor = false, foundMinor = false;
            for (int t = 0; t < ax.Ticker.TickCount(); ++t) {
                const auto& tick = ax.Ticker.Ticks[t];
                if (tick.Major) {
                    if (!foundMajor) {
                        result.origin = static_cast<float>(tick.PlotPos);
                        prevMajor     = tick.PlotPos;
                        foundMajor    = true;
                    } else if (result.major == 0.f) {
                        result.major = static_cast<float>(tick.PlotPos - prevMajor);
                    }
                } else {
                    if (!foundMinor) {
                        prevMinor  = tick.PlotPos;
                        foundMinor = true;
                    } else if (result.minor == 0.f) {
                        result.minor = static_cast<float>(tick.PlotPos - prevMinor);
                    }
                }
            }
            return result;
        };
        auto xTicks                = extractSpacing(plot->Axes[0]);
        auto yTicks                = extractSpacing(plot->Axes[1]);
        _frame.gridOrigin[0]       = xTicks.origin;
        _frame.gridOrigin[1]       = yTicks.origin;
        _frame.gridSpacing[0]      = xTicks.major;
        _frame.gridSpacing[1]      = yTicks.major;
        _frame.gridMinorSpacing[0] = xTicks.minor;
        _frame.gridMinorSpacing[1] = yTicks.minor;
    }

    void captureProjection(double cMin, double cMax, ImPlotColormap colormap) {
        auto* plot = ImPlot3D::GImPlot3D->CurrentPlot;
        if (!plot) {
            return;
        }

        const float fbScale = ImGui::GetIO().DisplayFramebufferScale.x;

        for (int i = 0; i < 3; ++i) {
            const auto& axis       = plot->Axes[i];
            _frame.axisMin[i]      = static_cast<float>(axis.Range.Min);
            _frame.axisMax[i]      = static_cast<float>(axis.Range.Max);
            double range           = axis.Range.Max - axis.Range.Min;
            _frame.axisInvRange[i] = range > 0.0 ? static_cast<float>(1.0 / range) : 0.0f;
            bool invert            = ImPlot3D::ImHasFlag(axis.Flags, ImPlot3DAxisFlags_Invert);
            _frame.ndcScale[i]     = static_cast<float>(axis.NDCScale) * (invert ? -1.0f : 1.0f);
        }

        _frame.rotation[0] = static_cast<float>(plot->Rotation.x);
        _frame.rotation[1] = static_cast<float>(plot->Rotation.y);
        _frame.rotation[2] = static_cast<float>(plot->Rotation.z);
        _frame.rotation[3] = static_cast<float>(plot->Rotation.w);

        _frame.viewScale = plot->GetViewScale() * fbScale;

        ImVec2 center          = plot->PlotRect.GetCenter();
        _frame.screenCenter[0] = center.x * fbScale;
        _frame.screenCenter[1] = center.y * fbScale;

        const ImGuiIO& io       = ImGui::GetIO();
        float          fbWidth  = io.DisplaySize.x * fbScale;
        float          fbHeight = io.DisplaySize.y * fbScale;
        _frame.invScreenSize[0] = 1.0f / fbWidth;
        _frame.invScreenSize[1] = 1.0f / fbHeight;

        _frame.colormapRange[0] = static_cast<float>(cMin);
        _frame.colormapRange[1] = static_cast<float>(cMax);

        _frame.plotRectMin[0] = plot->PlotRect.Min.x * fbScale;
        _frame.plotRectMin[1] = plot->PlotRect.Min.y * fbScale;
        _frame.plotRectMax[0] = plot->PlotRect.Max.x * fbScale;
        _frame.plotRectMax[1] = plot->PlotRect.Max.y * fbScale;
        _frame.fbHeight       = fbHeight;

        updateColormapLut(colormap);
    }

    static void renderCallback(const ImDrawList* /*parentList*/, const ImDrawCmd* cmd) {
        auto* self = static_cast<SurfaceGpuRenderer*>(cmd->UserCallbackData);
        if (!self || self->_indexCount == 0 || !self->_program) {
            return;
        }

        const auto& f = self->_frame;

        // save GL state
        GLint                  prevProgram = 0, prevVAO = 0, prevActiveTexture = 0;
        GLint                  prevTexture0 = 0;
        std::array<GLint, 4UZ> prevScissorBox{};
        GLboolean              prevDepthTest = GL_FALSE, prevDepthMask = GL_TRUE;

        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
        glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox.data());
        glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

        // scissor to plot rect
        glScissor(static_cast<GLint>(f.plotRectMin[0]), static_cast<GLint>(f.fbHeight - f.plotRectMax[1]), static_cast<GLsizei>(f.plotRectMax[0] - f.plotRectMin[0]), static_cast<GLsizei>(f.plotRectMax[1] - f.plotRectMin[1]));

        // depth testing for correct mesh occlusion
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);

        // render mesh
        glUseProgram(self->_program);
        glBindVertexArray(self->_vao);

        glUniform3fv(self->_locAxisMin, 1, f.axisMin);
        glUniform3fv(self->_locAxisMax, 1, f.axisMax);
        glUniform3fv(self->_locAxisInvRange, 1, f.axisInvRange);
        glUniform3fv(self->_locNdcScale, 1, f.ndcScale);
        glUniform4fv(self->_locRotation, 1, f.rotation);
        glUniform1f(self->_locViewScale, f.viewScale);
        glUniform2fv(self->_locScreenCenter, 1, f.screenCenter);
        glUniform2fv(self->_locInvScreenSize, 1, f.invScreenSize);
        glUniform2fv(self->_locColormapRange, 1, f.colormapRange);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, self->_colormapLutTexture);
        glUniform1i(self->_locColormapLut, 0);

        glUniform2fv(self->_locGridOrigin, 1, f.gridOrigin);
        glUniform2fv(self->_locGridSpacing, 1, f.gridSpacing);
        glUniform2fv(self->_locGridMinorSpacing, 1, f.gridMinorSpacing);
        glUniform1i(self->_locGridEnabled, f.gridEnabled);
        glUniform4fv(self->_locGridColor, 1, f.gridColor);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(self->_indexCount), GL_UNSIGNED_INT, nullptr);

        // restore GL state
        if (!prevDepthTest) {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(prevDepthMask);
        glScissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glBindVertexArray(static_cast<GLuint>(prevVAO));
        glUseProgram(static_cast<GLuint>(prevProgram));
    }

    void destroy() {
        if (_program) {
            glDeleteProgram(_program);
            _program = 0;
        }
        if (_vao) {
            glDeleteVertexArrays(1, &_vao);
            _vao = 0;
        }
        if (_vbo) {
            glDeleteBuffers(1, &_vbo);
            _vbo = 0;
        }
        if (_ibo) {
            glDeleteBuffers(1, &_ibo);
            _ibo = 0;
        }
        if (_colormapLutTexture) {
            glDeleteTextures(1, &_colormapLutTexture);
            _colormapLutTexture = 0;
        }
        _indexCount     = 0;
        _initAttempted  = false;
        _gpuAvailable   = false;
        _activeColormap = -1;
    }

    [[nodiscard]] bool available() const noexcept { return _gpuAvailable; }
};

struct SurfacePlot : gr::Block<SurfacePlot, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<"3D surface plot rendering spectrum history as a mesh via ImPlot3D.">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    // identity
    A<std::string, "chart name", gr::Visible>                                                                 chart_name;
    A<std::string, "chart title">                                                                             chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                                                    data_sinks      = {};
    A<bool, "show legend", gr::Visible>                                                                       show_legend     = false;
    A<bool, "show grid", gr::Visible>                                                                         show_grid       = true;
    A<bool, "show minor grid", gr::Visible>                                                                   show_minor_grid = false;
    A<std::uint32_t, "grid color", gr::Visible, gr::Doc<"grid line colour (0xRRGGBB)">>                       grid_color      = 0xFFFFFFU;
    A<float, "grid opacity", gr::Visible, gr::Limits<0.f, 1.f>, gr::Doc<"grid line opacity (0=transparent)">> grid_opacity    = 0.3f;

    // surface
    A<gr::Size_t, "history depth", gr::Visible, gr::Limits<4U, 1024U>>                                n_history        = 250U;
    A<ImPlotColormap_, "colormap", gr::Visible>                                                       colormap         = ImPlotColormap_Viridis;
    A<bool, "GPU acceleration", gr::Doc<"use GPU mesh rendering (falls back to CPU if unavailable)">> gpu_acceleration = true;

    // axis limits
    A<bool, "X auto-scale"> x_auto_scale = true;
    A<bool, "Y auto-scale"> y_auto_scale = true;
    A<bool, "Z auto-scale"> z_auto_scale = true;
    A<double, "X-axis min"> x_min        = std::numeric_limits<double>::lowest();
    A<double, "X-axis max"> x_max        = std::numeric_limits<double>::max();
    A<double, "Y-axis min"> y_min        = std::numeric_limits<double>::lowest();
    A<double, "Y-axis max"> y_max        = std::numeric_limits<double>::max();
    A<double, "Z-axis min"> z_min        = std::numeric_limits<double>::lowest();
    A<double, "Z-axis max"> z_max        = std::numeric_limits<double>::max();

    GR_MAKE_REFLECTABLE(SurfacePlot, chart_name, chart_title, data_sinks, show_legend, show_grid, show_minor_grid, grid_color, grid_opacity, n_history, colormap, gpu_acceleration, x_auto_scale, y_auto_scale, z_auto_scale, x_min, x_max, y_min, y_max, z_min, z_max);

    SurfaceBuffer              _surface;
    SurfaceGpuRenderer         _gpuRenderer;
    std::size_t                _lastSpectrumSize    = 0;
    int64_t                    _lastPushedTimestamp = 0;
    ImPlot3DColormap           _implot3dColormap    = -1;
    std::array<std::string, 6> _unitStore{};
    bool                       _needsRefit       = true;
    std::array<int, 3>         _pendingScale     = {-1, -1, -1}; // -1 = no change
    std::array<LabelFormat, 3> _axisFormat       = {LabelFormat::MetricInline, LabelFormat::MetricInline, LabelFormat::MetricInline};
    ImPlot3DPlot*              _savedPlot        = nullptr;
    int                        _hoveredAxisIndex = -1;

    static constexpr std::string_view kChartTypeName = "SurfacePlot";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid] = prepareDrawPrologue(config);

        if (_pendingResizeTime == 0.0 && _surface.width() > 0) {
            if (_surface._historyDepth != static_cast<std::size_t>(n_history)) {
                _needsRefit = true;
            }
            _surface.resizeHistory(static_cast<std::size_t>(n_history));
        }

        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        fetchAndPushData();

        if (_surface.filledRows() < 2 || _surface._freqAxis.empty()) {
            drawEmptyPlot("Waiting for data", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }

        syncColormap();
        if (gpu_acceleration) {
            _gpuRenderer.init();
        }

        ImPlot3DFlags flags3d = ImPlot3DFlags_NoTitle | ImPlot3DFlags_NoMenus;
        if (!showLegend) {
            flags3d |= ImPlot3DFlags_NoLegend;
        }

        // compute label padding based on format — MetricInline labels are wider (include unit suffix)
        const bool anyInline              = std::ranges::any_of(_axisFormat, [](LabelFormat f) { return f == LabelFormat::MetricInline; });
        ImPlot3D::GetStyle().LabelPadding = anyInline ? ImVec2(14, 14) : ImVec2(10, 10);

        // reduce tick label font size for the 3D plot to avoid overlap with the mesh
        auto* smallFont = DigitizerUi::LookAndFeel::instance().fontSmall[DigitizerUi::LookAndFeel::instance().prototypeMode];
        if (smallFont) {
            ImGui::PushFont(smallFont);
        }

        if (!ImPlot3D::BeginPlot(chart_name.value.c_str(), plotSize, flags3d)) {
            if (smallFont) {
                ImGui::PopFont();
            }
            return gr::work::Status::OK;
        }

        auto [xQuantity, xUnit] = sinkAxisInfo(true);
        auto [zQuantity, zUnit] = sinkAxisInfo(false);

        // suppress axis title when MetricInline is active (unit is embedded in tick labels)
        auto buildLabel = [](std::string_view quantity, std::string_view unit, LabelFormat fmt) -> std::string {
            if (fmt == LabelFormat::MetricInline || fmt == LabelFormat::None) {
                return "";
            }
            if (unit.empty()) {
                return std::string(quantity);
            }
            return std::format("{} [{}]", quantity, unit);
        };
        std::string xLabel = buildLabel(xQuantity, xUnit, _axisFormat[0]);
        std::string yLabel = buildLabel("time", "s", _axisFormat[1]);
        std::string zLabel = buildLabel(zQuantity, zUnit, _axisFormat[2]);

        ImPlot3DAxisFlags gridFlag = show_grid ? ImPlot3DAxisFlags_None : ImPlot3DAxisFlags_NoGridLines;
        ImPlot3D::SetupAxes(xLabel.empty() ? "" : xLabel.c_str(), yLabel.empty() ? "" : yLabel.c_str(), zLabel.empty() ? "" : zLabel.c_str(), gridFlag, gridFlag, gridFlag);

        // apply deferred scale changes from context menu (must happen during Setup phase)
        for (int i = 0; i < 3; ++i) {
            if (_pendingScale[static_cast<std::size_t>(i)] >= 0) {
                ImPlot3D::SetupAxisScale(static_cast<ImAxis3D>(i), _pendingScale[static_cast<std::size_t>(i)]);
                _pendingScale[static_cast<std::size_t>(i)] = -1;
            }
        }

        // axis formatters — apply per-axis format setting (matching 2D chart options)
        _unitStore[0] = xUnit;
        _unitStore[1] = "s";
        _unitStore[2] = zUnit;
        for (int i = 0; i < 3; ++i) {
            auto  axisId  = static_cast<ImAxis3D>(i);
            auto  ai      = static_cast<std::size_t>(i);
            void* unitPtr = const_cast<char*>(_unitStore[ai].c_str());
            switch (_axisFormat[ai]) {
            case LabelFormat::Metric: ImPlot3D::SetupAxisFormat(axisId, static_cast<ImPlot3DFormatter>(axis::formatMetric), nullptr); break;
            case LabelFormat::MetricInline: ImPlot3D::SetupAxisFormat(axisId, static_cast<ImPlot3DFormatter>(axis::formatMetric), unitPtr); break;
            case LabelFormat::Scientific: ImPlot3D::SetupAxisFormat(axisId, static_cast<ImPlot3DFormatter>(axis::formatScientific), nullptr); break;
            case LabelFormat::None:
                ImPlot3D::SetupAxisFormat(
                    axisId,
                    [](double, char* buf, int size, void*) -> int {
                        if (size > 0) {
                            buf[0] = '\0';
                        }
                        return 0;
                    },
                    nullptr);
                break;
            case LabelFormat::Default: ImPlot3D::SetupAxisFormat(axisId, static_cast<ImPlot3DFormatter>(axis::formatDefault), nullptr); break;
            case LabelFormat::Auto:
            default: ImPlot3D::SetupAxisFormat(axisId, static_cast<ImPlot3DFormatter>(axis::formatMetric), unitPtr); break;
            }
        }

        // X/Z: only force on structural changes so user can zoom/pan freely
        // Y (time): always force — the time axis scrolls continuously
        const auto xzCond = _needsRefit ? ImPlot3DCond_Always : ImPlot3DCond_Once;

        if (!x_auto_scale.value) {
            ImPlot3D::SetupAxisLimits(ImAxis3D_X, x_min.value, x_max.value, ImPlot3DCond_Always);
        }
        if (!y_auto_scale.value) {
            ImPlot3D::SetupAxisLimits(ImAxis3D_Y, y_min.value, y_max.value, ImPlot3DCond_Always);
        }
        if (!z_auto_scale.value) {
            ImPlot3D::SetupAxisLimits(ImAxis3D_Z, z_min.value, z_max.value, ImPlot3DCond_Always);
        }

        auto [cMin, cMax] = effectiveColourRange(this->ui_constraints.value, _surface.scaleMin(), _surface.scaleMax());

        auto [yLo, yHi] = _surface.timeBounds();

        const bool useGpu = gpu_acceleration.value && _gpuRenderer.available();
        if (useGpu) {
            // GPU path: PlotSurface's ExtendFit is skipped — set axis limits from data
            if (x_auto_scale.value && !_surface._freqAxis.empty()) {
                ImPlot3D::SetupAxisLimits(ImAxis3D_X, static_cast<double>(_surface._freqAxis.front()), static_cast<double>(_surface._freqAxis.back()), xzCond);
            }
            if (y_auto_scale.value) {
                ImPlot3D::SetupAxisLimits(ImAxis3D_Y, yLo, yHi, ImPlot3DCond_Always);
            }
            if (z_auto_scale.value) {
                ImPlot3D::SetupAxisLimits(ImAxis3D_Z, _surface.scaleMin(), _surface.scaleMax(), xzCond);
            }

            if (_surface._dirty) {
                _gpuRenderer.uploadMesh(_surface);
                _surface._dirty = false;
            }
            // capture projection while ImPlot3D context is still active
            _gpuRenderer.captureProjection(cMin, cMax, colormap);
        } else {
            _surface.buildSurfaceArrays();
            const auto nX = static_cast<int>(_surface._freqAxis.size());
            const auto nY = static_cast<int>(_surface._filledRows);
            ImPlot3D::PushColormap(_implot3dColormap);
            ImPlot3D::PlotSurface("##surface", _surface._xCoords.data(), _surface._yCoords.data(), _surface._zCoords.data(), nX, nY, cMin, cMax);
            ImPlot3D::PopColormap();
        }

        // save plot pointer — Axes[i].Hovered is set during EndPlot and persists on the plot object
        _savedPlot = ImPlot3D::GImPlot3D->CurrentPlot;

        ImPlot3D::EndPlot();
        if (smallFont) {
            ImGui::PopFont();
        }
        _needsRefit = false;

        // extract grid tick spacing AFTER EndPlot (tickers are populated during EndPlot)
        if (useGpu) {
            const auto  rgb = grid_color.value;
            const float gc[4]{static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f, static_cast<float>(rgb & 0xFF) / 255.f, grid_opacity.value};
            _gpuRenderer.captureGridFromTickers(_savedPlot, show_grid.value, show_minor_grid.value, gc);
        }

        // GPU callback must be added AFTER EndPlot — ImPlot3D draws the box
        // background during EndPlot, which would cover a callback placed earlier
        if (useGpu) {
            ImGui::GetWindowDrawList()->AddCallback(SurfaceGpuRenderer::renderCallback, &_gpuRenderer);
        }

        drawSurfaceContextMenu();
        handleSurfaceDropTarget();

        return gr::work::Status::OK;
    }

    void reset() {
        _surface.clear();
        _gpuRenderer.destroy();
        _needsRefit = true;
    }

    void syncColormap() {
        static bool colormapsRegistered = false;
        if (!colormapsRegistered) {
            registerImPlotColormaps();
            colormapsRegistered = true;
        }

        static constexpr std::array kImPlotColormapNames = {"Deep", "Dark", "Pastel", "Paired", "Viridis", "Plasma", "Hot", "Cool", "Pink", "Jet", "Twilight", "RdBu", "BrBG", "PiYG", "Spectral", "Greys"};

        auto cmapIdx = static_cast<std::size_t>(colormap.value);
        if (cmapIdx < kImPlotColormapNames.size()) {
            _implot3dColormap = ImPlot3D::GetColormapIndex(kImPlotColormapNames[cmapIdx]);
        }
        if (_implot3dColormap < 0) {
            _implot3dColormap = 0;
        }
    }

    static void registerImPlotColormaps() {
        static constexpr std::array kImPlotColormapEnums = {ImPlotColormap_Deep, ImPlotColormap_Dark, ImPlotColormap_Pastel, ImPlotColormap_Paired, ImPlotColormap_Viridis, ImPlotColormap_Plasma, ImPlotColormap_Hot, ImPlotColormap_Cool, ImPlotColormap_Pink, ImPlotColormap_Jet, ImPlotColormap_Twilight, ImPlotColormap_RdBu, ImPlotColormap_BrBG, ImPlotColormap_PiYG, ImPlotColormap_Spectral, ImPlotColormap_Greys};

        static constexpr std::array kNames = {"Deep", "Dark", "Pastel", "Paired", "Viridis", "Plasma", "Hot", "Cool", "Pink", "Jet", "Twilight", "RdBu", "BrBG", "PiYG", "Spectral", "Greys"};

        constexpr std::size_t        kLutSize = 256;
        std::array<ImVec4, kLutSize> colours{};

        for (std::size_t cm = 0; cm < kImPlotColormapEnums.size(); ++cm) {
            if (ImPlot3D::GetColormapIndex(kNames[cm]) >= 0) {
                continue;
            }
            for (std::size_t i = 0; i < kLutSize; ++i) {
                float t    = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
                colours[i] = ImPlot::SampleColormap(t, kImPlotColormapEnums[cm]);
            }
            ImPlot3D::AddColormap(kNames[cm], colours.data(), static_cast<int>(kLutSize), false);
        }
    }

    void drawSurfaceContextMenu() {
        if (!_savedPlot) {
            return;
        }

        // right-click: detect axis hover and open the appropriate popup
        // ImPlot3D hover convention: 1 axis = edge, 2 axes = plane face, 3 axes = empty space
        // Only open axis menu for exactly 1 (edge) and canvas menu for exactly 2 (plane face).
        // 3 hovered axes (empty space/corners) → no popup, preserves right-click rotation.
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            _hoveredAxisIndex   = -1;
            int hoveredCount    = 0;
            int lastHoveredAxis = -1;
            for (int i = 0; i < 3; ++i) {
                if (_savedPlot->Axes[i].Hovered) {
                    ++hoveredCount;
                    lastHoveredAxis = i;
                }
            }
            if (hoveredCount == 1) {
                _hoveredAxisIndex = lastHoveredAxis;
                ImGui::OpenPopup("##Srf3DAxis");
            } else if (hoveredCount == 2) {
                ImGui::OpenPopup("##Srf3DCanvas");
            }
        }

        // double-click on axis edge: fit-once without enabling auto-scale
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int hoveredCount = 0;
            for (int i = 0; i < 3; ++i) {
                hoveredCount += _savedPlot->Axes[i].Hovered ? 1 : 0;
            }
            if (hoveredCount == 1) {
                _needsRefit = true;
            }
        }

        // axis context menu — mirrors 2D drawAxisSubmenuContent
        if (ImGui::BeginPopup("##Srf3DAxis")) {
            if (_hoveredAxisIndex >= 0 && _hoveredAxisIndex < 3) {
                draw3DAxisSubmenuContent(_hoveredAxisIndex);
            }
            ImGui::EndPopup();
        }

        // canvas context menu
        if (ImGui::BeginPopup("##Srf3DCanvas")) {
            drawCommonContextMenuItems();
            ImGui::EndPopup();
        }
    }

    [[nodiscard]] bool& autoScaleRef(int axisIndex) { return (axisIndex == 0) ? x_auto_scale.value : (axisIndex == 1) ? y_auto_scale.value : z_auto_scale.value; }

    [[nodiscard]] double& minRef(int axisIndex) { return (axisIndex == 0) ? x_min.value : (axisIndex == 1) ? y_min.value : z_min.value; }

    [[nodiscard]] double& maxRef(int axisIndex) { return (axisIndex == 0) ? x_max.value : (axisIndex == 1) ? y_max.value : z_max.value; }

    void draw3DAxisSubmenuContent(int axisIndex) {
        static constexpr std::array kAxisNames  = {"X-Axis", "Y-Axis", "Z-Axis"};
        static constexpr std::array kScaleNames = {"Linear", "Log10"};
        static constexpr std::array kScaleIds   = {ImPlot3DScale_Linear, ImPlot3DScale_Log10};
        constexpr float             kDragWidth  = 70.0f;

        auto ai = static_cast<std::size_t>(axisIndex);

        // header
        {
            const char* icon = (axisIndex == 0) ? menu_icons::kXAxis : menu_icons::kYAxis;
            std::string label;
            if (axisIndex == 0) {
                auto [q, u] = sinkAxisInfo(true);
                label       = u.empty() ? q : std::format("{} [{}]", q, u);
            } else if (axisIndex == 1) {
                label = "time [s]";
            } else {
                auto [q, u] = sinkAxisInfo(false);
                label       = u.empty() ? q : std::format("{} [{}]", q, u);
            }
            if (label.empty()) {
                label = kAxisNames[ai];
            }
            menu_icons::iconText(icon, label.c_str());
            ImGui::Separator();
        }

        // scale selector (deferred to next frame's Setup phase)
        {
            menu_icons::iconText(menu_icons::kScale, "scale:");
            ImGui::SameLine();
            int currentScale = (_pendingScale[ai] >= 0) ? _pendingScale[ai] : ImPlot3DScale_Linear;
            if (_savedPlot) {
                currentScale = static_cast<int>(_savedPlot->Axes[axisIndex].Scale);
            }
            std::string_view currentName = (currentScale == ImPlot3DScale_Log10) ? kScaleNames[1] : kScaleNames[0];
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::BeginCombo("##scale", currentName.data())) {
                for (std::size_t s = 0; s < kScaleNames.size(); ++s) {
                    if (ImGui::Selectable(kScaleNames[s], kScaleIds[s] == currentScale)) {
                        _pendingScale[ai] = kScaleIds[s];
                    }
                }
                ImGui::EndCombo();
            }
        }

        // format selector
        {
            menu_icons::iconText(menu_icons::kFormat, "format:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::BeginCombo("##format", magic_enum::enum_name(_axisFormat[ai]).data())) {
                for (auto f : magic_enum::enum_values<LabelFormat>()) {
                    if (ImGui::Selectable(magic_enum::enum_name(f).data(), f == _axisFormat[ai])) {
                        _axisFormat[ai] = f;
                    }
                }
                ImGui::EndCombo();
            }
        }

        // auto-fit checkbox + fit-once
        bool& autoFit = autoScaleRef(axisIndex);
        {
            DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            ImGui::TextUnformatted(menu_icons::kAutoFit);
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Checkbox("##auto", &autoFit)) {
            if (autoFit) {
                _needsRefit = true;
            } else {
                // capture current axis range as manual limits
                if (_savedPlot) {
                    minRef(axisIndex) = _savedPlot->Axes[axisIndex].Range.Min;
                    maxRef(axisIndex) = _savedPlot->Axes[axisIndex].Range.Max;
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Auto-fit axis range");
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit once")) {
            _needsRefit = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fit axis to data once, then return to manual mode");
        }

        // min/max editors
        if (autoFit) {
            ImGui::BeginDisabled();
        }

        double minVal = autoFit ? (_savedPlot ? _savedPlot->Axes[axisIndex].Range.Min : 0.0) : minRef(axisIndex);
        double maxVal = autoFit ? (_savedPlot ? _savedPlot->Axes[axisIndex].Range.Max : 1.0) : maxRef(axisIndex);

        const double range     = std::abs(maxVal - minVal);
        const double increment = (range > 0.0 && range < 1e10) ? range * 0.01 : 0.1;
        const float  dragSpeed = static_cast<float>(increment * 0.1);

        auto drawSpinner = [&](const char* id, double& val, auto&& assign) {
            const std::string decId  = std::format("\uf146##{}_dec", id);
            const std::string incId  = std::format("\uf0fe##{}_inc", id);
            const std::string dragId = std::format("##{}", id);
            {
                DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
                if (ImGui::Button(decId.c_str())) {
                    val -= increment;
                    assign(val);
                }
            }
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::SetNextItemWidth(kDragWidth);
            if (ImGui::DragScalar(dragId.c_str(), ImGuiDataType_Double, &val, dragSpeed, nullptr, nullptr, "%.4g")) {
                assign(val);
            }
            detail::onScrollWheel([&](float wheel) {
                val += static_cast<double>(wheel) * increment;
                assign(val);
            });
            ImGui::SameLine(0.0f, 2.0f);
            {
                DigitizerUi::IMW::Font iconFont(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
                if (ImGui::Button(incId.c_str())) {
                    val += increment;
                    assign(val);
                }
            }
        };

        auto setMin = [&](double v) { minRef(axisIndex) = v; };
        auto setMax = [&](double v) { maxRef(axisIndex) = v; };

        drawSpinner("min", minVal, setMin);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        menu_icons::iconText(menu_icons::kArrow, "");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        drawSpinner("max", maxVal, setMax);

        if (autoFit) {
            ImGui::EndDisabled();
        }
    }

    void handleSurfaceDropTarget() {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd::kPayloadType)) {
                const auto* dndPayload = static_cast<const dnd::Payload*>(payload->Data);
                if (dndPayload && dndPayload->isValid()) {
                    std::string sinkName(dndPayload->sink_name);
                    auto        sinkSharedPtr = SinkRegistry::instance().findSink([&sinkName](const auto& s) { return s.signalName() == sinkName || s.name() == sinkName; });
                    if (sinkSharedPtr) {
                        onSinkAddedFromDnd(sinkName, sinkSharedPtr);
                        if (dndPayload->hasSource()) {
                            dnd::g_state.accepted = true;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void fetchAndPushData() {
        forEachValidSpectrum(_signalSinks, [&](const auto& /*sink*/, const SpectrumFrame& f) -> bool {
            if (f.timestamp == _lastPushedTimestamp && _lastPushedTimestamp != 0) {
                return false;
            }
            _lastPushedTimestamp = f.timestamp;

            if (_lastSpectrumSize != f.nBins) {
                _surface.init(f.nBins, static_cast<std::size_t>(n_history));
                _lastSpectrumSize = f.nBins;
                _needsRefit       = true;
            }

            _surface.updateAutoScale(f.yValues, f.nBins);
            _surface.pushRow(f.xValues, f.yValues, f.nBins, timestampFromNanos(f.timestamp));

            return false;
        });
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::SurfacePlot", opendigitizer::charts::SurfacePlot)
inline auto registerSurfacePlot = gr::registerBlock<opendigitizer::charts::SurfacePlot>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_CHARTS_SURFACEPLOT_HPP
