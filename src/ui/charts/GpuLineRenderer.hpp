#ifndef OPENDIGITIZER_CHARTS_GPULINERENDERER_HPP
#define OPENDIGITIZER_CHARTS_GPULINERENDERER_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <print>
#include <vector>

#include "../utils/ShaderHelper.hpp"
#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>

namespace opendigitizer::charts {

// one instanced quad per line segment, after R. Terrell, "Instanced line rendering," 2019
struct GpuLineProgram {
    enum ScaleKind : GLint { linear = 0, logarithmic = 1, symLog = 2, imPlotSymLog = 3 };

    struct Handles {
        GLuint program          = 0;
        GLint  xMapping         = -1;
        GLint  yMapping         = -1;
        GLint  scaleKinds       = -1;
        GLint  symLogThresholds = -1;
        GLint  viewport         = -1;
        GLint  color            = -1;
        GLint  width            = -1;
    };

    Handles handles;
    bool    _compileAttempted = false;

    GpuLineProgram()                                 = default;
    GpuLineProgram(const GpuLineProgram&)            = delete;
    GpuLineProgram& operator=(const GpuLineProgram&) = delete;

    ~GpuLineProgram() {
        if (handles.program != 0) {
            glDeleteProgram(handles.program);
        }
    }

    [[nodiscard]] bool available() {
        if (!_compileAttempted) {
            _compileAttempted = true;
            compile();
            if (handles.program == 0) {
                std::println(stderr, "[XYChart] GPU line shaders unavailable, streaming traces are drawn on the CPU");
            }
        }
        return handles.program != 0;
    }

    void compile() {
        static constexpr auto* kVertexShader = R"glsl(
layout(location = 0) in vec2 a_from;
layout(location = 1) in vec2 a_to;

uniform vec4  u_xMapping; // pixel min, pixels per scale unit, value offset, scale min
uniform vec4  u_yMapping;
uniform ivec2 u_scaleKinds;
uniform vec2  u_symLogThresholds;
uniform vec4  u_viewport;
uniform vec2  u_width;

out float v_across;

const int   kLogarithmic   = 1;
const int   kSymLog        = 2;
const int   kImPlotSymLog  = 3;
const float kGap           = 3.0e38;
const float kLog10Of2      = 0.30102999566;
const float kLog10OfDblMin = -307.6526555685888;

float scaled(float v, int kind, float threshold) {
    if (kind == kLogarithmic) {
        return v > 0.0 ? log2(v) * kLog10Of2 : kLog10OfDblMin;
    }
    if (kind == kSymLog) {
        return sign(v) * log2(1.0 + abs(v) / threshold) * kLog10Of2;
    }
    if (kind == kImPlotSymLog) {
        return 2.0 * sign(v) * asinh(0.5 * abs(v));
    }
    return v;
}

vec2 toPixel(vec2 point) {
    return vec2(u_xMapping.x + u_xMapping.y * (scaled(point.x + u_xMapping.z, u_scaleKinds.x, u_symLogThresholds.x) - u_xMapping.w),
                u_yMapping.x + u_yMapping.y * (scaled(point.y + u_yMapping.z, u_scaleKinds.y, u_symLogThresholds.y) - u_yMapping.w));
}

void main() {
    const vec2 kCorners[6] = vec2[6](vec2(0.0, -1.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, -1.0), vec2(1.0, 1.0), vec2(1.0, -1.0));
    vec2  from = toPixel(a_from);
    vec2  to   = toPixel(a_to);
    float span = length(to - from);
    if (abs(a_from.y) >= kGap || abs(a_to.y) >= kGap || span == 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        v_across    = 0.0;
        return;
    }
    vec2  corner     = kCorners[gl_VertexID];
    float halfExtent = u_width.x + u_width.y;
    vec2  normal     = vec2(from.y - to.y, to.x - from.x) / span;
    vec2  pixel      = mix(from, to, corner.x) + normal * (corner.y * halfExtent);
    v_across         = corner.y * halfExtent;
    gl_Position      = vec4((pixel.x - u_viewport.x) * u_viewport.z - 1.0, 1.0 - (pixel.y - u_viewport.y) * u_viewport.w, 0.0, 1.0);
}
)glsl";

        static constexpr auto* kFragmentShader = R"glsl(
in float v_across;
out vec4 fragColor;

uniform vec4 u_color;
uniform vec2 u_width;

void main() {
    float coverage = u_width.y > 0.0 ? clamp(u_width.x + 0.5 - abs(v_across), 0.0, 1.0) : 1.0;
    fragColor      = vec4(u_color.rgb, u_color.a * coverage);
}
)glsl";

        const GLuint vertexShader   = shader::compileShader(GL_VERTEX_SHADER, shader::kGlslPrefix, kVertexShader);
        const GLuint fragmentShader = shader::compileShader(GL_FRAGMENT_SHADER, shader::kGlslPrefix, kFragmentShader);
        handles.program             = shader::linkProgram(vertexShader, fragmentShader);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        if (handles.program == 0) {
            return;
        }
        handles.xMapping         = glGetUniformLocation(handles.program, "u_xMapping");
        handles.yMapping         = glGetUniformLocation(handles.program, "u_yMapping");
        handles.scaleKinds       = glGetUniformLocation(handles.program, "u_scaleKinds");
        handles.symLogThresholds = glGetUniformLocation(handles.program, "u_symLogThresholds");
        handles.viewport         = glGetUniformLocation(handles.program, "u_viewport");
        handles.color            = glGetUniformLocation(handles.program, "u_color");
        handles.width            = glGetUniformLocation(handles.program, "u_width");
    }
};

struct GpuLineRenderer {
    static constexpr std::size_t kBlockSize   = 256UZ;
    static constexpr std::size_t kUploadChunk = 4096UZ;
    static constexpr float       kGap         = std::numeric_limits<float>::max();

    struct Vertex {
        float x;
        float y;
    };

    struct Extents {
        double xMin         = std::numeric_limits<double>::infinity();
        double xMax         = -std::numeric_limits<double>::infinity();
        double yMin         = std::numeric_limits<double>::infinity();
        double yMax         = -std::numeric_limits<double>::infinity();
        double yMinPositive = std::numeric_limits<double>::infinity();

        void add(double x, double y) {
            if (std::isfinite(x)) {
                xMin = std::fmin(xMin, x);
                xMax = std::fmax(xMax, x);
            }
            if (!std::isfinite(y)) {
                return;
            }
            yMin = std::fmin(yMin, y);
            yMax = std::fmax(yMax, y);
            if (y > 0.0) {
                yMinPositive = std::fmin(yMinPositive, y);
            }
        }

        void add(const Extents& other) {
            xMin         = std::fmin(xMin, other.xMin);
            xMax         = std::fmax(xMax, other.xMax);
            yMin         = std::fmin(yMin, other.yMin);
            yMax         = std::fmax(yMax, other.yMax);
            yMinPositive = std::fmin(yMinPositive, other.yMinPositive);
        }
    };

    struct AxisMapping {
        std::array<float, 4UZ> coefficients;
        GLint                  scaleKind;
        float                  symLogThreshold;
    };

    struct Frame {
        GpuLineProgram::Handles  program;
        GLuint                   vao       = 0;
        GLuint                   vbo       = 0;
        GLint                    firstSlot = 0;
        std::array<GLsizei, 2UZ> segmentCounts{};
        std::array<float, 4UZ>   xMapping{};
        std::array<float, 4UZ>   yMapping{};
        std::array<GLint, 2UZ>   scaleKinds{};
        std::array<float, 2UZ>   symLogThresholds{};
        std::array<float, 4UZ>   viewport{};
        std::array<float, 4UZ>   color{};
        std::array<float, 2UZ>   width{};
        ImVec2                   framebufferScale{};
        float                    framebufferHeight = 0.0f;
    };

    GLuint               _vao         = 0;
    GLuint               _vbo         = 0;
    std::size_t          _capacity    = 0UZ; // ring slots, plus one repeating slot 0 for the segment across the wrap
    std::size_t          _uploadedEnd = 0UZ;
    double               _origin      = 0.0;
    double               _newestX     = std::numeric_limits<double>::quiet_NaN();
    double               _newestY     = std::numeric_limits<double>::quiet_NaN();
    std::size_t          _windowBegin = 0UZ;
    double               _shift       = 0.0;
    Extents              _windowExtents;
    std::vector<Extents> _blocks;
    std::vector<Vertex>  _staging;
    int                  lastDrawnFrame = -1;

    GpuLineRenderer()                                  = default;
    GpuLineRenderer(const GpuLineRenderer&)            = delete;
    GpuLineRenderer& operator=(const GpuLineRenderer&) = delete;

    ~GpuLineRenderer() {
        if (_vao != 0) {
            glDeleteVertexArrays(1, &_vao);
            glDeleteBuffers(1, &_vbo);
        }
    }

    // caller holds sink.dataGuard()
    void refresh(const SignalSink& sink, std::size_t nHistory, AxisScale xAxisScale) {
        const std::size_t size      = sink.size();
        const std::size_t sinkBegin = oldestSample(sink);
        const std::size_t total     = sinkBegin + size;
        const std::size_t begin     = total - std::min(size, nHistory);
        const auto        xAt       = [&](std::size_t sample) { return sink.xAt(sample - sinkBegin); };
        const auto        yAt       = [&](std::size_t sample) { return static_cast<double>(sink.yAt(sample - sinkBegin)); };

        if (_capacity != nHistory) {
            allocate(nHistory);
        }
        const double newest      = xAt(total - 1UZ);
        const double span        = std::abs(newest - xAt(begin));
        const bool   historyKept = _uploadedEnd > begin && _uploadedEnd <= total && identical(xAt(_uploadedEnd - 1UZ), _newestX) && identical(yAt(_uploadedEnd - 1UZ), _newestY);
        const bool   originKept  = historyKept && std::abs(newest - _origin) <= 2.0 * span;
        if (!originKept) {
            _uploadedEnd = begin;
            _origin      = newest + span;
            std::ranges::fill(_blocks, Extents{});
        }
        upload(sink, sinkBegin, total);

        _windowBegin   = begin;
        _windowExtents = {};
        switch (xAxisScale) {
        case AxisScale::Time: _shift = 0.0; break;
        case AxisScale::LinearReverse: _shift = newest; break;
        default: _shift = xAt(begin); break;
        }
        const std::size_t firstWholeBlock = std::min(begin + (kBlockSize - begin % kBlockSize) % kBlockSize, total);
        for (std::size_t sample = begin; sample < firstWholeBlock; ++sample) {
            _windowExtents.add(xAt(sample), yAt(sample));
        }
        for (std::size_t blockBegin = firstWholeBlock; blockBegin < total; blockBegin += kBlockSize) {
            _windowExtents.add(blockOf(blockBegin));
        }
    }

    void draw(const GpuLineProgram& program, const SignalSink& sink, const char* label, const ImVec4& color, float lineWidth, bool antiAliased) const {
        ImPlot::SetNextLineStyle(color, lineWidth);
        if (!ImPlot::BeginItem(label, ImPlotItemFlags_None, ImPlotCol_Line)) {
            return;
        }
        ImPlotPlot& plot  = *ImPlot::GetCurrentPlot();
        ImPlotAxis& xAxis = plot.Axes[plot.CurrentX];
        ImPlotAxis& yAxis = plot.Axes[plot.CurrentY];
        if (plot.FitThisFrame) {
            fit(sink, xAxis, yAxis);
        }
        const ImPlotNextItemData& item = ImPlot::GetItemData();
        if (item.RenderLine && _uploadedEnd - _windowBegin >= 2UZ) {
            Frame       frame    = makeFrame(program.handles, xAxis, yAxis, item, antiAliased);
            ImDrawList* drawList = ImPlot::GetPlotDrawList();
            drawList->AddCallback(renderCallback, &frame, sizeof(frame));
            drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
        ImPlot::EndItem();
    }

    void allocate(std::size_t capacity) {
        if (_vao == 0) {
            glGenVertexArrays(1, &_vao);
            glGenBuffers(1, &_vbo);
            glBindVertexArray(_vao);
            for (const GLuint attribute : {0U, 1U}) {
                glEnableVertexAttribArray(attribute);
                glVertexAttribDivisor(attribute, 1);
            }
            glBindVertexArray(0);
        }
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>((capacity + 1UZ) * sizeof(Vertex)), nullptr, GL_DYNAMIC_DRAW);
        _capacity    = capacity;
        _uploadedEnd = 0UZ;
        _blocks.resize(capacity / kBlockSize + 2UZ);
    }

    void upload(const SignalSink& sink, std::size_t sinkBegin, std::size_t end) {
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        while (_uploadedEnd < end) {
            const std::size_t slot  = _uploadedEnd % _capacity;
            const std::size_t count = std::min({end - _uploadedEnd, _capacity - slot, kUploadChunk});
            _staging.clear();
            for (std::size_t sample = _uploadedEnd; sample < _uploadedEnd + count; ++sample) {
                const double x     = sink.xAt(sample - sinkBegin);
                const double y     = static_cast<double>(sink.yAt(sample - sinkBegin));
                Extents&     block = blockOf(sample);
                if (sample % kBlockSize == 0UZ) {
                    block = {};
                }
                block.add(x, y);
                _staging.push_back(toVertex(x, y));
                _newestX = x;
                _newestY = y;
            }
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(slot * sizeof(Vertex)), static_cast<GLsizeiptr>(count * sizeof(Vertex)), _staging.data());
            if (slot == 0UZ) {
                glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(_capacity * sizeof(Vertex)), sizeof(Vertex), _staging.data());
            }
            _uploadedEnd += count;
        }
    }

    [[nodiscard]] Vertex toVertex(double x, double y) const {
        const auto relativeX = static_cast<float>(x - _origin);
        const auto value     = static_cast<float>(y);
        if (!std::isfinite(relativeX) || !std::isfinite(value)) {
            return {0.0f, kGap};
        }
        return {relativeX, value};
    }

    [[nodiscard]] auto blockOf(this auto& self, std::size_t sample) noexcept -> decltype(self._blocks[0UZ]) { return self._blocks[sample / kBlockSize % self._blocks.size()]; }

    [[nodiscard]] static std::size_t oldestSample(const SignalSink& sink) noexcept { return std::max(sink.totalSampleCount(), sink.size()) - sink.size(); }

    [[nodiscard]] static bool identical(double a, double b) noexcept { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

    void fit(const SignalSink& sink, ImPlotAxis& xAxis, ImPlotAxis& yAxis) const {
        const bool xRangeFit = ImHasFlag(xAxis.Flags, ImPlotAxisFlags_RangeFit);
        const bool yRangeFit = ImHasFlag(yAxis.Flags, ImPlotAxisFlags_RangeFit);
        if (!xRangeFit) {
            fitX(xAxis, _windowExtents);
        }
        if (!yRangeFit) {
            fitY(yAxis, _windowExtents);
        }
        if (!xRangeFit && !yRangeFit) {
            return;
        }
        const std::size_t size      = sink.size();
        const std::size_t sinkBegin = oldestSample(sink);
        for (std::size_t blockBegin = _windowBegin - _windowBegin % kBlockSize; blockBegin < _uploadedEnd; blockBegin += kBlockSize) {
            const Extents& block     = blockOf(blockBegin);
            const bool     xFitsNone = !xRangeFit || block.yMax < yAxis.Range.Min || block.yMin > yAxis.Range.Max;
            const bool     yFitsNone = !yRangeFit || block.xMax - _shift < xAxis.Range.Min || block.xMin - _shift > xAxis.Range.Max;
            if (xFitsNone && yFitsNone) {
                continue;
            }
            const bool xFitsAll = !xRangeFit || (yAxis.Range.Contains(block.yMin) && yAxis.Range.Contains(block.yMax));
            const bool yFitsAll = !yRangeFit || (xAxis.Range.Contains(block.xMin - _shift) && xAxis.Range.Contains(block.xMax - _shift));
            if (blockBegin >= _windowBegin && xFitsAll && yFitsAll) {
                if (xRangeFit) {
                    fitX(xAxis, block);
                }
                if (yRangeFit) {
                    fitY(yAxis, block);
                }
                continue;
            }
            for (std::size_t sample = std::max({blockBegin, _windowBegin, sinkBegin}); sample < std::min({blockBegin + kBlockSize, _uploadedEnd, sinkBegin + size}); ++sample) {
                const double x = sink.xAt(sample - sinkBegin) - _shift;
                const double y = static_cast<double>(sink.yAt(sample - sinkBegin));
                if (xRangeFit) {
                    xAxis.ExtendFitWith(yAxis, x, y);
                }
                if (yRangeFit) {
                    yAxis.ExtendFitWith(xAxis, y, x);
                }
            }
        }
    }

    void fitX(ImPlotAxis& axis, const Extents& extents) const {
        axis.ExtendFit(extents.xMin - _shift);
        axis.ExtendFit(extents.xMax - _shift);
    }

    static void fitY(ImPlotAxis& axis, const Extents& extents) {
        axis.ExtendFit(extents.yMin >= axis.ConstraintRange.Min ? extents.yMin : extents.yMinPositive);
        axis.ExtendFit(extents.yMax);
    }

    [[nodiscard]] static AxisMapping axisMapping(const ImPlotAxis& axis, double valueOffset) {
        if (axis.TransformForward == nullptr) {
            return {{axis.PixelMin, static_cast<float>(axis.ScaleToPixel), static_cast<float>(valueOffset - axis.Range.Min), 0.0f}, GpuLineProgram::linear, 1.0f};
        }
        const double pixelsPerScaleUnit = static_cast<double>(axis.PixelMax - axis.PixelMin) / (axis.ScaleMax - axis.ScaleMin);
        const GLint  scaleKind          = axis.Scale == ImPlotScale_Log10 ? GpuLineProgram::logarithmic : (axis.Scale == ImPlotScale_SymLog ? GpuLineProgram::imPlotSymLog : GpuLineProgram::symLog);
        const float  threshold          = scaleKind == GpuLineProgram::symLog ? static_cast<float>(*static_cast<const double*>(axis.TransformData)) : 1.0f;
        return {{axis.PixelMin, static_cast<float>(pixelsPerScaleUnit), static_cast<float>(valueOffset), static_cast<float>(axis.ScaleMin)}, scaleKind, threshold};
    }

    [[nodiscard]] Frame makeFrame(const GpuLineProgram::Handles& program, const ImPlotAxis& xAxis, const ImPlotAxis& yAxis, const ImPlotNextItemData& item, bool antiAliased) const {
        const ImGuiViewport& viewport         = *ImGui::GetWindowViewport();
        const ImVec2         framebufferScale = viewport.FramebufferScale.x != 0.0f ? viewport.FramebufferScale : ImGui::GetIO().DisplayFramebufferScale;
        const AxisMapping    xMapping         = axisMapping(xAxis, _origin - _shift);
        const AxisMapping    yMapping         = axisMapping(yAxis, 0.0);
        const std::size_t    segments         = _uploadedEnd - _windowBegin - 1UZ;
        const std::size_t    firstSlot        = _windowBegin % _capacity;
        const std::size_t    beforeWrap       = std::min(segments, _capacity - firstSlot);
        const ImVec4         lineColor        = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(item.Colors[ImPlotCol_Line]));
        return Frame{
            .program           = program,
            .vao               = _vao,
            .vbo               = _vbo,
            .firstSlot         = static_cast<GLint>(firstSlot),
            .segmentCounts     = {static_cast<GLsizei>(beforeWrap), static_cast<GLsizei>(segments - beforeWrap)},
            .xMapping          = xMapping.coefficients,
            .yMapping          = yMapping.coefficients,
            .scaleKinds        = {xMapping.scaleKind, yMapping.scaleKind},
            .symLogThresholds  = {xMapping.symLogThreshold, yMapping.symLogThreshold},
            .viewport          = {viewport.Pos.x, viewport.Pos.y, 2.0f / viewport.Size.x, 2.0f / viewport.Size.y},
            .color             = {lineColor.x, lineColor.y, lineColor.z, lineColor.w},
            .width             = {0.5f * std::max(1.0f, item.LineWeight), antiAliased ? 1.0f : 0.0f},
            .framebufferScale  = framebufferScale,
            .framebufferHeight = static_cast<float>(static_cast<int>(viewport.Size.y * framebufferScale.y)),
        };
    }

    static void renderCallback(const ImDrawList* /*parentList*/, const ImDrawCmd* command) {
        Frame frame;
        std::memcpy(&frame, command->UserCallbackData, sizeof(frame)); // the callback data is unaligned
        const ImVec2 clipMin{(command->ClipRect.x - frame.viewport[0]) * frame.framebufferScale.x, (command->ClipRect.y - frame.viewport[1]) * frame.framebufferScale.y};
        const ImVec2 clipMax{(command->ClipRect.z - frame.viewport[0]) * frame.framebufferScale.x, (command->ClipRect.w - frame.viewport[1]) * frame.framebufferScale.y};
        if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
            return;
        }
        glScissor(static_cast<GLint>(clipMin.x), static_cast<GLint>(frame.framebufferHeight - clipMax.y), static_cast<GLsizei>(clipMax.x - clipMin.x), static_cast<GLsizei>(clipMax.y - clipMin.y));
        glUseProgram(frame.program.program);
        glUniform4fv(frame.program.xMapping, 1, frame.xMapping.data());
        glUniform4fv(frame.program.yMapping, 1, frame.yMapping.data());
        glUniform2i(frame.program.scaleKinds, frame.scaleKinds[0], frame.scaleKinds[1]);
        glUniform2fv(frame.program.symLogThresholds, 1, frame.symLogThresholds.data());
        glUniform4fv(frame.program.viewport, 1, frame.viewport.data());
        glUniform4fv(frame.program.color, 1, frame.color.data());
        glUniform2fv(frame.program.width, 1, frame.width.data());
        glBindVertexArray(frame.vao);
        glBindBuffer(GL_ARRAY_BUFFER, frame.vbo);
        for (std::size_t call = 0UZ; call < frame.segmentCounts.size(); ++call) {
            if (frame.segmentCounts[call] == 0) {
                continue;
            }
            const std::uintptr_t offset = static_cast<std::uintptr_t>(call == 0UZ ? frame.firstSlot : 0) * sizeof(Vertex);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offset));
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offset + sizeof(Vertex)));
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, frame.segmentCounts[call]);
        }
    }
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_GPULINERENDERER_HPP
