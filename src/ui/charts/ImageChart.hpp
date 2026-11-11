#ifndef OPENDIGITIZER_CHARTS_IMAGECHART_HPP
#define OPENDIGITIZER_CHARTS_IMAGECHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include "../common/LookAndFeel.hpp"
#include "../utils/EmscriptenHelper.hpp"

#include <opendigitizer/video/CameraTypes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/TouchHandler.hpp"
#include <imgui.h>
#include <implot.h>

namespace opendigitizer::charts {

/// RGBA8 texture holding the latest frame; GL_RGBA bytes are R,G,B,A, i.e. 0xAABBGGRR on little-endian hosts
struct ImageTexture {
    GLuint      _id      = 0U;
    std::size_t _rows    = 0UZ;
    std::size_t _columns = 0UZ;
    bool        _failed  = false;

    ImageTexture() = default;
    ImageTexture(ImageTexture&& other) noexcept : _id(std::exchange(other._id, 0U)), _rows(other._rows), _columns(other._columns), _failed(other._failed) {}
    ImageTexture& operator=(ImageTexture&& other) noexcept {
        std::swap(_id, other._id);
        std::swap(_rows, other._rows);
        std::swap(_columns, other._columns);
        std::swap(_failed, other._failed);
        return *this;
    }
    ~ImageTexture() {
        if (_id != 0U) {
            glDeleteTextures(1, &_id);
        }
    }

    [[nodiscard]] bool upload(std::span<const std::uint32_t> pixels, std::size_t rows, std::size_t columns) {
        if (_id == 0U && !_failed) {
            glGenTextures(1, &_id);
            _failed = _id == 0U;
        }
        if (_id == 0U) {
            return false;
        }
        GLint previous = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glBindTexture(GL_TEXTURE_2D, _id);
        if (rows != _rows || columns != _columns) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(columns), static_cast<GLsizei>(rows), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            _rows    = rows;
            _columns = columns;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(columns), static_cast<GLsizei>(rows), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        return true;
    }
};

enum class ImageComposition { tile, overlay };

struct ImageLayer {
    ImageTexture               texture;
    std::vector<std::uint32_t> rgba;   // texture upload buffer, alpha 0 outside the layer's transparency range
    std::vector<double>        scalar; // CPU fallback, decimated, rows flipped for PlotHeatmap
    std::size_t                rows = 0UZ, columns = 0UZ, scalarRows = 0UZ, scalarColumns = 0UZ;
    std::pair<double, double>  zRange{0.0, 1.0};
    std::pair<double, double>  valueRange{0.0, 1.0}; // of the last frame, for the shared tile scale
    std::uint64_t              shown     = 0U;       // generation on screen
    std::uint64_t              displayed = 0U;
    std::uint64_t              skipped   = 0U;
    double                     shownRate = 0.0, receivedRate = 0.0; // Hz, smoothed
    double                     lastTime = -1.0;
    bool                       onGpu    = false;
};

/// frames a sink received after the generation on screen and before `next`, the one shown now; none before the first
[[nodiscard]] constexpr std::uint64_t skippedBetween(std::uint64_t shown, std::uint64_t next) noexcept { return shown == 0U || next <= shown ? 0U : next - shown - 1U; }

/// the CPU fallback's step in rows and columns, so a rows x columns frame fits within `budget` cells
[[nodiscard]] inline std::size_t cpuDecimationStep(std::size_t rows, std::size_t columns, std::size_t budget) noexcept {
    const double cellsPerBudget = static_cast<double>(rows * columns) / static_cast<double>(std::max<std::size_t>(1UZ, budget));
    return std::max<std::size_t>(1UZ, static_cast<std::size_t>(std::ceil(std::sqrt(cellsPerBudget))));
}

struct ImageChart : gr::Block<ImageChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<R""(@brief Displays Tensor<T> frames: uint32 as packed 0xAARRGGBB colour, float/double through `colormap`.

Several inputs are tiled in a grid (shared colour scale) or overlaid in input order (own colormap each).
Frames a sink received but the chart never showed are counted and indicated.)"">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName = "ImageChart";

    A<std::string, "chart name", gr::Visible>                                                                                                     chart_name;
    A<std::string, "chart title">                                                                                                                 chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                                                                                        data_sinks       = {};
    A<bool, "show legend", gr::Visible>                                                                                                           show_legend      = false;
    A<bool, "show grid", gr::Visible>                                                                                                             show_grid        = false;
    A<ImPlotColormap_, "colormap", gr::Visible, gr::Doc<"for float/double frames">>                                                               colormap         = ImPlotColormap_Greys;
    A<bool, "reverse colormap", gr::Doc<"ImPlot's Greys runs white to black">>                                                                    reverse_colormap = true;
    A<bool, "GPU acceleration", gr::Doc<"use a GPU texture (falls back to a CPU heatmap if unavailable)">>                                        gpu_acceleration = true;
    A<bool, "X auto-scale">                                                                                                                       x_auto_scale     = true;
    A<bool, "Y auto-scale">                                                                                                                       y_auto_scale     = true;
    A<ImageComposition, "composition", gr::Visible, gr::Doc<"several inputs: tile = grid, overlay = layers in input order">>                      composition      = ImageComposition::tile;
    A<std::vector<double>, "layer min", gr::Doc<"per input: lower transparency bound (luma in [0, 1] for colour frames); missing or NaN = none">> layer_min;
    A<std::vector<double>, "layer max", gr::Doc<"per input: upper transparency bound (luma in [0, 1] for colour frames); missing or NaN = none">> layer_max;
    A<std::vector<std::string>, "layer colormaps", gr::Doc<"per input from the second on (overlay): ImPlot colormap name">>                       layer_colormaps;

    static constexpr SignalKind supportedSignals = SignalKind::Image2D;

    GR_MAKE_REFLECTABLE(ImageChart, chart_name, chart_title, data_sinks, show_legend, show_grid, colormap, reverse_colormap, gpu_acceleration, x_auto_scale, y_auto_scale, composition, layer_min, layer_max, layer_colormaps);

    std::vector<ImageLayer>   _layers;
    std::pair<double, double> _fittedExtent{0.0, 0.0};
    ImVec2                    _plotPos{0.f, 0.f}; // _plot*: as last drawn
    ImVec2                    _plotSize{0.f, 0.f};
    ImPlotRect                _plotLimits;
    ImVec2                    _fittedPlotSize{0.f, 0.f};

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }
    [[nodiscard]] std::span<const ImageLayer>       layers() const noexcept { return _layers; }
    [[nodiscard]] bool                              rendersOnGpu() const noexcept { return !_layers.empty() && _layers.front().onGpu; }

    /// screen position of image coordinates (column, row) as last drawn; row 0 is on top
    [[nodiscard]] ImVec2 screenPosition(double column, double row) const noexcept {
        const auto along = [](double v, const ImPlotRange& range, float origin, float length) { return origin + static_cast<float>((v - range.Min) / (range.Max - range.Min)) * length; };
        return {along(column, _plotLimits.X, _plotPos.x, _plotSize.x), along(row, _plotLimits.Y, _plotPos.y, _plotSize.y)};
    }

    void reset() { requestRenderReset(); }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { handleSettingsChanged(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);
        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags | ImPlotFlags_Equal)) {
            return gr::work::Status::OK;
        }
        _layers.resize(_signalSinks.size());
        const auto shared = sharedRange();
        for (std::size_t i = 0UZ; i < _layers.size(); ++i) {
            fetchFrame(*_signalSinks[i], i, shared);
        }

        const ImPlotAxisFlags gridFlags = showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines;
        ImPlot::SetupAxis(ImAxis_X1, "x [px]", gridFlags | ImPlotAxisFlags_Foreground);
        ImPlot::SetupAxis(ImAxis_Y1, "y [px]", gridFlags | ImPlotAxisFlags_Foreground | ImPlotAxisFlags_Invert); // image row 0 on top
        fitFrame();
        setupFinish();

        for (const auto& sink : _signalSinks) {
            ImPlot::SetNextLineStyle(sinkColor(sink->color()));
            ImPlot::PlotDummy(plotLabel(*sink).c_str());
        }
        for (std::size_t i = 0UZ; i < _layers.size(); ++i) {
            drawLayer(i);
        }
        drawSkippedIndicator();
        _plotPos    = ImPlot::GetPlotPos();
        _plotSize   = ImPlot::GetPlotSize();
        _plotLimits = ImPlot::GetPlotLimits();
        if (browserTestHooks()) {
            const ImageLayer& first = _layers.front();
            publishForBrowserTests("odImageCharts", chart_name.value, std::format(R"({{"x":{},"y":{},"w":{},"h":{},"x0":{},"x1":{},"y0":{},"y1":{},"rows":{},"columns":{},"shown":{},"displayed":{},"skipped":{}}})", _plotPos.x, _plotPos.y, _plotSize.x, _plotSize.y, _plotLimits.X.Min, _plotLimits.X.Max, _plotLimits.Y.Min, _plotLimits.Y.Max, first.rows, first.columns, first.shown, first.displayed, first.skipped));
        }

        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        return gr::work::Status::OK;
    }

    void resetRenderState() {
        _layers.clear();
        _fittedExtent   = {0.0, 0.0};
        _fittedPlotSize = {0.f, 0.f};
    }

    [[nodiscard]] std::pair<double, double> transparencyRange(std::size_t layer) const {
        const auto bound = [layer](const std::vector<double>& v, double fallback) { return layer < v.size() && !std::isnan(v[layer]) ? v[layer] : fallback; };
        return {bound(layer_min.value, -std::numeric_limits<double>::infinity()), bound(layer_max.value, std::numeric_limits<double>::infinity())};
    }

    /// tile grid cell of `layer`: columns = ceil(sqrt(n)); every cell as large as the largest frame
    [[nodiscard]] ImPlotPoint tileOrigin(std::size_t layer) const {
        if (composition.value == ImageComposition::overlay || _layers.size() < 2UZ) {
            return {0.0, 0.0};
        }
        const auto [cellW, cellH] = cellSize();
        const auto columns        = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(_layers.size()))));
        return {static_cast<double>(layer % columns) * cellW, static_cast<double>(layer / columns) * cellH};
    }

private:
    [[nodiscard]] std::pair<double, double> cellSize() const {
        double w = 0.0, h = 0.0;
        for (const ImageLayer& l : _layers) {
            w = std::max(w, static_cast<double>(l.columns));
            h = std::max(h, static_cast<double>(l.rows));
        }
        const double gap = static_cast<double>(DigitizerUi::LookAndFeel::instance().chartStyle.imageTileGap) * std::max(w, h);
        return {w + gap, h + gap};
    }

    [[nodiscard]] std::pair<double, double> extent() const {
        if (_layers.empty()) {
            return {0.0, 0.0};
        }
        if (composition.value == ImageComposition::overlay || _layers.size() < 2UZ) {
            return {static_cast<double>(_layers.front().columns), static_cast<double>(_layers.front().rows)};
        }
        const auto [cellW, cellH] = cellSize();
        const auto columns        = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(_layers.size()))));
        return {static_cast<double>(columns) * cellW, static_cast<double>((_layers.size() + columns - 1UZ) / columns) * cellH};
    }

    /// tiles share one colour scale, the union of the inputs' last value ranges (one frame behind for the others)
    [[nodiscard]] std::optional<std::pair<double, double>> sharedRange() const {
        if (composition.value != ImageComposition::tile || _layers.size() < 2UZ) {
            return std::nullopt;
        }
        std::pair range{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        for (const ImageLayer& l : _layers) {
            range = {std::min(range.first, l.valueRange.first), std::max(range.second, l.valueRange.second)};
        }
        return range;
    }

    [[nodiscard]] ImPlotColormap layerColormap(std::size_t layer) const {
        if (composition.value == ImageComposition::overlay && layer > 0UZ && layer - 1UZ < layer_colormaps.value.size()) {
            if (const ImPlotColormap named = ImPlot::GetColormapIndex(layer_colormaps.value[layer - 1UZ].c_str()); named >= 0) {
                return named;
            }
        }
        return colormap.value;
    }

    void fetchFrame(const SignalSink& sink, std::size_t index, std::optional<std::pair<double, double>> shared) {
        ImageLayer&      layer = _layers[index];
        auto             guard = sink.dataGuard();
        const ImageFrame frame = sink.imageFrame(layer.shown + 1U);
        if (frame.generation == 0U || frame.generation <= layer.shown || frame.rows * frame.columns == 0UZ) {
            return;
        }
        layer.skipped += skippedBetween(layer.shown, frame.generation);
        updateRates(layer, frame.generation);
        layer.shown   = frame.generation;
        layer.rows    = frame.rows;
        layer.columns = frame.columns;
        ++layer.displayed;
        std::visit(
            [&]<typename TPixels>(const TPixels& pixels) {
                if constexpr (std::is_same_v<TPixels, std::span<const std::uint32_t>>) {
                    convert(layer, index, pixels, [](std::uint32_t p) { return opendigitizer::video::bt601Luma(p) / 255.0; }, std::nullopt);
                } else if constexpr (!std::is_same_v<TPixels, std::monostate>) {
                    convert(layer, index, pixels, [](auto v) { return static_cast<double>(v); }, shared);
                }
            },
            frame.pixels);
        layer.onGpu = gpu_acceleration && layer.texture.upload(layer.rgba, layer.rows, layer.columns);
    }

    static void updateRates(ImageLayer& layer, std::uint64_t generation) {
        const double now = ImGui::GetTime();
        if (layer.lastTime >= 0.0 && now > layer.lastTime) {
            const double dt     = now - layer.lastTime;
            const auto   smooth = [](double rate, double sample) { return rate > 0.0 ? 0.9 * rate + 0.1 * sample : sample; };
            layer.shownRate     = smooth(layer.shownRate, 1.0 / dt);
            layer.receivedRate  = smooth(layer.receivedRate, static_cast<double>(generation - layer.shown) / dt);
        }
        layer.lastTime = now;
    }

    template<typename TPixel>
    void convert(ImageLayer& layer, std::size_t index, std::span<const TPixel> pixels, auto toScalar, std::optional<std::pair<double, double>> shared) {
        constexpr bool colour  = std::is_same_v<TPixel, std::uint32_t>;
        auto           numbers = pixels | std::views::transform(toScalar) | std::views::filter([](double v) { return !std::isnan(v); });
        const auto [lo, hi]    = std::ranges::empty(numbers) ? std::ranges::minmax_result<double>{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()} : std::ranges::minmax(numbers);
        layer.valueRange       = {lo, hi};
        if constexpr (colour) {
            layer.zRange = {0.0, 1.0};
        } else {
            const auto [sLo, sHi] = shared.value_or(std::pair{lo, hi});
            layer.zRange          = effectiveColourRange(this->ui_constraints.value, std::min(sLo, lo), std::max(sHi, hi));
        }
        const auto [cutLo, cutHi] = transparencyRange(index);
        const auto lut            = colour ? std::array<std::uint32_t, kColormapSize>{} : layerLut(layerColormap(index));
        layer.rgba.resize(pixels.size());
        for (std::size_t i = 0UZ; i < pixels.size(); ++i) {
            const double  v    = toScalar(pixels[i]);
            std::uint32_t rgba = 0U;
            if constexpr (colour) {
                rgba = opendigitizer::video::swapRedBlue(pixels[i]);
            } else {
                rgba = colormapLookup(v, layer.zRange.first, layer.zRange.second, lut);
            }
            layer.rgba[i] = (v < cutLo || v > cutHi) ? (rgba & 0x00FFFFFFU) : rgba;
        }
        fillScalar(layer, pixels, toScalar);
    }

    [[nodiscard]] std::array<std::uint32_t, kColormapSize> layerLut(ImPlotColormap map) const {
        auto lut = buildColormapLut(map);
        if (reverse_colormap) {
            std::ranges::reverse(lut);
        }
        return lut;
    }

    /// PlotHeatmap puts its first row at the top of the bounds, i.e. at y = rows on the inverted axis
    void fillScalar(ImageLayer& layer, auto pixels, auto toScalar) {
        const std::size_t step = cpuDecimationStep(layer.rows, layer.columns, DigitizerUi::LookAndFeel::instance().chartStyle.imageCpuMaxCells);
        layer.scalarRows       = (layer.rows + step - 1UZ) / step;
        layer.scalarColumns    = (layer.columns + step - 1UZ) / step;
        layer.scalar.resize(layer.scalarRows * layer.scalarColumns);
        for (std::size_t r = 0UZ; r < layer.scalarRows; ++r) {
            const std::size_t target = (layer.scalarRows - 1UZ - r) * layer.scalarColumns;
            for (std::size_t c = 0UZ; c < layer.scalarColumns; ++c) {
                layer.scalar[target + c] = toScalar(pixels[r * step * layer.columns + c * step]);
            }
        }
    }

    /// PlotHeatmap paints every cell, so the opaque ones are drawn as rectangles
    void drawCells(const ImageLayer& layer, std::size_t index, ImPlotPoint topLeft, ImPlotPoint bottomRight) const {
        const auto [cutLo, cutHi] = transparencyRange(index);
        const auto        lut     = layerLut(layerColormap(index));
        const std::size_t step    = cpuDecimationStep(layer.scalarRows, layer.scalarColumns, DigitizerUi::LookAndFeel::instance().chartStyle.imageCpuMaxRects);
        const double      cellW = (bottomRight.x - topLeft.x) / static_cast<double>(layer.scalarColumns), cellH = (bottomRight.y - topLeft.y) / static_cast<double>(layer.scalarRows);
        ImDrawList*       drawList = ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        for (std::size_t r = 0UZ; r < layer.scalarRows; r += step) {
            for (std::size_t c = 0UZ; c < layer.scalarColumns; c += step) {
                const double v = layer.scalar[r * layer.scalarColumns + c];
                if (v < cutLo || v > cutHi) {
                    continue;
                }
                const double y = topLeft.y + static_cast<double>(layer.scalarRows - r - std::min(step, layer.scalarRows - r)) * cellH; // scalar rows are flipped
                drawList->AddRectFilled(ImPlot::PlotToPixels(topLeft.x + static_cast<double>(c) * cellW, y), ImPlot::PlotToPixels(topLeft.x + static_cast<double>(std::min(c + step, layer.scalarColumns)) * cellW, y + static_cast<double>(std::min(step, layer.scalarRows - r)) * cellH), colormapLookup(v, layer.zRange.first, layer.zRange.second, lut));
            }
        }
        ImPlot::PopPlotClipRect();
    }

    void drawLayer(std::size_t index) {
        const ImageLayer& layer = _layers[index];
        if (layer.rows == 0UZ) {
            return;
        }
        const ImPlotPoint origin        = tileOrigin(index);
        const ImageLayer& sizeReference = composition.value == ImageComposition::overlay ? _layers.front() : layer;
        const ImPlotPoint topLeft(origin.x, origin.y);
        const ImPlotPoint bottomRight(origin.x + static_cast<double>(sizeReference.columns), origin.y + static_cast<double>(sizeReference.rows));
        const std::string id = std::format("##frame{}", index);
        if (layer.onGpu) {
            const auto textureId = []<typename TexId = ImTextureID>(GLuint texture) -> TexId {
                if constexpr (std::is_pointer_v<TexId>) {
                    return reinterpret_cast<TexId>(static_cast<std::uintptr_t>(texture));
                } else {
                    return static_cast<TexId>(texture);
                }
            };
            ImPlot::PlotImage(id.c_str(), textureId(layer.texture._id), topLeft, bottomRight, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f)); // PlotImage puts uv0 at (x min, y max)
            return;
        }
        if (const auto [cutLo, cutHi] = transparencyRange(index); std::isfinite(cutLo) || std::isfinite(cutHi)) {
            drawCells(layer, index, topLeft, bottomRight);
            return;
        }
        ImPlot::PushColormap(layerColormap(index));
        const auto [scaleMin, scaleMax] = reverse_colormap ? std::pair{layer.zRange.second, layer.zRange.first} : layer.zRange;
        ImPlot::PlotHeatmap(id.c_str(), layer.scalar.data(), static_cast<int>(layer.scalarRows), static_cast<int>(layer.scalarColumns), scaleMin, scaleMax, nullptr, topLeft, bottomRight);
        ImPlot::PopColormap();
    }

    /// equal axes would otherwise crop the wider dimension
    void fitFrame() {
        const auto [w, h] = extent();
        if (w <= 0.0 || (std::pair{w, h} == _fittedExtent && _plotSize.x == _fittedPlotSize.x && _plotSize.y == _fittedPlotSize.y)) {
            return;
        }
        double x0 = 0.0, x1 = w, y0 = 0.0, y1 = h;
        if (_plotSize.x > 0.f && _plotSize.y > 0.f) {
            const double aspect = static_cast<double>(_plotSize.x) / static_cast<double>(_plotSize.y);
            if (w / h > aspect) {
                const double margin = 0.5 * (w / aspect - h);
                y0 -= margin;
                y1 += margin;
            } else {
                const double margin = 0.5 * (h * aspect - w);
                x0 -= margin;
                x1 += margin;
            }
        }
        ImPlot::SetupAxesLimits(x0, x1, y0, y1, ImPlotCond_Always);
        _fittedExtent   = {w, h};
        _fittedPlotSize = _plotSize;
    }

    void drawSkippedIndicator() {
        const auto& look = DigitizerUi::LookAndFeel::instance();
        ImVec2      pos  = ImPlot::GetPlotPos();
        pos.x += look.chartStyle.imageStatusPadding.x;
        pos.y += look.chartStyle.imageStatusPadding.y;
        for (std::size_t i = 0UZ; i < _layers.size(); ++i) {
            const ImageLayer& layer = _layers[i];
            if (layer.skipped == 0U) {
                continue;
            }
            const double      share = 100.0 * static_cast<double>(layer.skipped) / static_cast<double>(layer.skipped + layer.displayed);
            const std::string label = plotLabel(*_signalSinks[i]);
            const std::string text  = std::format("{}: shows {:.0f} of {:.0f} fps, skipped {} ({:.0f} %)", std::string_view(label).substr(0UZ, label.find("##")), layer.shownRate, layer.receivedRate, layer.skipped, share);
            ImPlot::GetPlotDrawList()->AddText(pos, ImGui::GetColorU32(look.palette().warningColor), text.c_str());
            pos.y += ImGui::GetTextLineHeight();
        }
    }
};

} // namespace opendigitizer::charts

GR_REGISTER_BLOCK("opendigitizer::charts::ImageChart", opendigitizer::charts::ImageChart)
inline auto registerImageChartCompatibilities = opendigitizer::charts::registerChartSignalCompatibility<opendigitizer::charts::ImageChart>();

#endif // OPENDIGITIZER_CHARTS_IMAGECHART_HPP
