#ifndef OPENDIGITIZER_CHARTS_IMAGECHART_HPP
#define OPENDIGITIZER_CHARTS_IMAGECHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"
#include "SpectrumHelper.hpp"

#include "../common/LookAndFeel.hpp"
#include "../utils/Browser.hpp"

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

namespace gr::od::charts {

enum class ImageComposition { tile, overlay };

struct ImageLayer {
    gl::Texture2D              texture{gl::Texture2D::Options{.minFilter = GL_LINEAR}};
    std::vector<std::uint32_t> rgba; // texture upload buffer, alpha 0 outside the layer's transparency range

    std::size_t               rows = 0UZ, columns = 0UZ;
    std::pair<double, double> zRange{0.0, 1.0};
    std::pair<double, double> valueRange{0.0, 1.0}; // of the last frame, for the shared tile scale
    std::uint64_t             shown     = 0U;       // generation on screen
    std::uint64_t             displayed = 0U;
    std::uint64_t             skipped   = 0U;
    double                    shownRate = 0.0, receivedRate = 0.0; // Hz, smoothed
    double                    lastTime = -1.0;
    bool                      uploaded = false; // the texture holds the latest frame; not without a GL context
};

/// frames a sink received after the generation on screen and before `next`, the one shown now; none before the first
[[nodiscard]] constexpr std::uint64_t skippedBetween(std::uint64_t shown, std::uint64_t next) noexcept { return shown == 0U || next <= shown ? 0U : next - shown - 1U; }

struct ImageChart : gr::Block<ImageChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<R""(@brief Displays Tensor<T> frames: uint32 as packed 0xAARRGGBB colour, float/double through `colormap`.

Several inputs are tiled in a grid (shared colour scale) or overlaid in input order (own colormap each).
Frames a sink received but the chart never showed are counted and indicated.)"">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName    = "ImageChart";
    static constexpr SignalKind       kSupportedSignals = SignalKind::Image2D | SignalKind::Dataset1D; // a rank-2 DataSet is an image

    A<std::string, "chart name", gr::Visible>                                       chart_name;
    A<std::string, "chart title">                                                   chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                          data_sinks       = {};
    A<bool, "show legend", gr::Visible>                                             show_legend      = false;
    A<bool, "show grid", gr::Visible>                                               show_grid        = false;
    A<ImPlotColormap_, "colormap", gr::Visible, gr::Doc<"for float/double frames">> colormap         = ImPlotColormap_Greys;
    A<bool, "reverse colormap", gr::Doc<"ImPlot's Greys runs white to black">>      reverse_colormap = true;

    A<bool, "X auto-scale">                                                                                                                       x_auto_scale = true;
    A<bool, "Y auto-scale">                                                                                                                       y_auto_scale = true;
    A<ImageComposition, "composition", gr::Visible, gr::Doc<"several inputs: tile = grid, overlay = layers in input order">>                      composition  = ImageComposition::tile;
    A<std::vector<double>, "layer min", gr::Doc<"per input: lower transparency bound (luma in [0, 1] for colour frames); missing or NaN = none">> layer_min;
    A<std::vector<double>, "layer max", gr::Doc<"per input: upper transparency bound (luma in [0, 1] for colour frames); missing or NaN = none">> layer_max;
    A<std::vector<std::string>, "layer colormaps", gr::Doc<"per input from the second on (overlay): ImPlot colormap name">>                       layer_colormaps;

    GR_MAKE_REFLECTABLE(ImageChart, chart_name, chart_title, data_sinks, show_legend, show_grid, colormap, reverse_colormap, x_auto_scale, y_auto_scale, composition, layer_min, layer_max, layer_colormaps);

    std::vector<ImageLayer>   _layers;
    std::pair<double, double> _fittedExtent{0.0, 0.0};
    ImVec2                    _plotPos{0.f, 0.f}; // _plot*: as last drawn
    ImVec2                    _plotSize{0.f, 0.f};
    ImPlotRect                _plotLimits;
    ImVec2                    _fittedPlotSize{0.f, 0.f};

    [[nodiscard]] std::span<const ImageLayer> layers() const noexcept { return _layers; }

    /// screen position of image coordinates (column, row) as last drawn; row 0 is on top
    [[nodiscard]] ImVec2 screenPosition(double column, double row) const noexcept {
        const auto along = [](double v, const ImPlotRange& range, float origin, float length) { return origin + static_cast<float>((v - range.Min) / (range.Max - range.Min)) * length; };
        return {along(column, _plotLimits.X, _plotPos.x, _plotSize.x), along(row, _plotLimits.Y, _plotPos.y, _plotSize.y)};
    }

    void reset() { requestRenderReset(); }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) { applyChartSettings(newSettings); }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);
        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }
        if (!TouchHandler<>::beginZoomablePlot(chart_name.value, plotSize, plotFlags | ImPlotFlags_Equal)) {
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

        plotLegendEntries();
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
        updateInteractions(chartMode);
        TouchHandler<>::endZoomablePlot();
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

    [[nodiscard]] std::pair<double, double> cellSize() const {
        double w = 0.0, h = 0.0;
        for (const ImageLayer& l : _layers) {
            w = std::max(w, static_cast<double>(l.columns));
            h = std::max(h, static_cast<double>(l.rows));
        }
        const double gap = static_cast<double>(LookAndFeel::instance().chartStyle.imageTileGap) * std::max(w, h);
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
        ImageLayer&     layer = _layers[index];
        auto            guard = sink.dataGuard();
        const FrameView frame = sink.frame(layer.shown + 1U);
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
            frame.values);
        layer.uploaded = layer.texture.upload(layer.columns, layer.rows, layer.rgba.data());
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
        constexpr bool colour = std::is_same_v<TPixel, std::uint32_t>;
        const auto [lo, hi]   = finiteRange(pixels | std::views::transform(toScalar)).value_or(std::pair{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()});
        layer.valueRange      = {lo, hi};
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
    }

    [[nodiscard]] std::array<std::uint32_t, kColormapSize> layerLut(ImPlotColormap map) const {
        auto lut = buildColormapLut(map);
        if (reverse_colormap) {
            std::ranges::reverse(lut);
        }
        return lut;
    }

    void drawLayer(std::size_t index) {
        const ImageLayer& layer = _layers[index];
        if (layer.rows == 0UZ || !layer.uploaded) {
            return;
        }
        const ImPlotPoint origin        = tileOrigin(index);
        const ImageLayer& sizeReference = composition.value == ImageComposition::overlay ? _layers.front() : layer;
        const ImPlotPoint topLeft(origin.x, origin.y);
        const ImPlotPoint bottomRight(origin.x + static_cast<double>(sizeReference.columns), origin.y + static_cast<double>(sizeReference.rows));
        ImPlot::PlotImage(std::format("##frame{}", index).c_str(), layer.texture.id(), topLeft, bottomRight, ImVec2(0.f, 1.f), ImVec2(1.f, 0.f)); // PlotImage puts uv0 at (x min, y max)
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
        const auto& look = LookAndFeel::instance();
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

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_IMAGECHART_HPP
