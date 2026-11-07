#ifndef OPENDIGITIZER_VIDEO_IMAGETRANSFORMS_HPP
#define OPENDIGITIZER_VIDEO_IMAGETRANSFORMS_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tensor.hpp>

#include "CameraTypes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace opendigitizer::video {

template<typename T>
concept ImageSample = std::same_as<T, std::uint32_t> || std::same_as<T, float> || std::same_as<T, double>;

enum class Interpolation { nearest, bilinear, bicubic };

enum class DistortionModel { brownConrady, kannalaBrandt, map, homography };

namespace image {

[[nodiscard]] inline std::size_t rows(const auto& tensor) noexcept { return tensor.rank() == 2UZ ? tensor.extents()[0] : 0UZ; }
[[nodiscard]] inline std::size_t columns(const auto& tensor) noexcept { return tensor.rank() == 2UZ ? tensor.extents()[1] : 0UZ; }

/// emits one output frame of `block.extentsFor(input)` per input frame, filled by `block.apply(input, output)`
[[nodiscard]] gr::work::Status forEachFrame(auto& block, gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) {
    for (std::size_t i = 0UZ; i < inSpan.size(); ++i) {
        const auto extents = block.extentsFor(inSpan[i]);
        const auto output  = gr::emit(block, outSpan, i, std::span<const std::size_t>(extents));
        if (!output) {
            return gr::work::Status::ERROR;
        }
        block.apply(inSpan[i], *output);
    }
    outSpan.publish(inSpan.size());
    return gr::work::Status::OK;
}

/// uint32 pixels are packed 0xAARRGGBB with four 8-bit channels; float/double pixels are one scalar channel
template<ImageSample T>
inline constexpr std::size_t kChannels = std::is_same_v<T, std::uint32_t> ? 4UZ : 1UZ;

template<ImageSample T>
[[nodiscard]] constexpr double channel(T pixel, std::size_t c) noexcept {
    if constexpr (std::is_same_v<T, std::uint32_t>) {
        return static_cast<double>((pixel >> (8U * c)) & 0xFFU);
    } else {
        return static_cast<double>(pixel);
    }
}

template<ImageSample T>
[[nodiscard]] constexpr T compose(const std::array<double, kChannels<T>>& channels) noexcept {
    if constexpr (std::is_same_v<T, std::uint32_t>) {
        std::uint32_t pixel = 0U;
        for (std::size_t c = 0UZ; c < 4UZ; ++c) {
            pixel |= static_cast<std::uint32_t>(std::clamp(std::lround(channels[c]), 0L, 255L)) << (8U * c);
        }
        return pixel;
    } else {
        return static_cast<T>(channels[0]);
    }
}

[[nodiscard]] inline double cubicWeight(double t) noexcept { // Catmull-Rom (a = -0.5)
    t = std::abs(t);
    if (t < 1.0) {
        return (1.5 * t - 2.5) * t * t + 1.0;
    }
    if (t < 2.0) {
        return ((-0.5 * t + 2.5) * t - 4.0) * t + 2.0;
    }
    return 0.0;
}

/// samples `src` at the continuous position (fy, fx) with pixel centres on integers, clamping to the edge
template<ImageSample T>
[[nodiscard]] T sample(const auto& src, double fy, double fx, Interpolation interpolation) {
    const std::size_t srcH  = rows(src);
    const std::size_t srcW  = columns(src);
    const auto        pixel = [&](std::ptrdiff_t r, std::ptrdiff_t c) { return src.data()[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(r, 0, static_cast<std::ptrdiff_t>(srcH) - 1)) * srcW + static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(c, 0, static_cast<std::ptrdiff_t>(srcW) - 1))]; };
    if (interpolation == Interpolation::nearest) {
        return pixel(static_cast<std::ptrdiff_t>(std::floor(fy + 0.5)), static_cast<std::ptrdiff_t>(std::floor(fx + 0.5)));
    }
    const auto                       y0     = static_cast<std::ptrdiff_t>(std::floor(fy));
    const auto                       x0     = static_cast<std::ptrdiff_t>(std::floor(fx));
    const bool                       cubic  = interpolation == Interpolation::bicubic;
    const auto                       weight = [cubic](double t) { return cubic ? cubicWeight(t) : std::max(0.0, 1.0 - std::abs(t)); };
    const std::ptrdiff_t             lo     = cubic ? -1 : 0;
    const std::ptrdiff_t             hi     = cubic ? 2 : 1;
    std::array<double, kChannels<T>> acc{};
    for (std::ptrdiff_t dy = lo; dy <= hi; ++dy) {
        const double wy = weight(fy - static_cast<double>(y0 + dy));
        for (std::ptrdiff_t dx = lo; dx <= hi; ++dx) {
            const double w = wy * weight(fx - static_cast<double>(x0 + dx));
            const T      p = pixel(y0 + dy, x0 + dx);
            for (std::size_t c = 0UZ; c < acc.size(); ++c) {
                acc[c] += w * channel(p, c);
            }
        }
    }
    return compose<T>(acc);
}

} // namespace image

template<ImageSample T>
struct GreyScale : gr::Block<GreyScale<T>> {
    using Description = gr::Doc<"@brief Converts packed 0xAARRGGBB frames to ITU-R BT.601 luma: [0, 1] for float/double, [0, 255] for uint32.">;
    gr::TensorPortIn<std::uint32_t> in;
    gr::TensorPortOut<T>            out;

    GR_MAKE_REFLECTABLE(GreyScale, in, out);

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) { return image::forEachFrame(*this, inSpan, outSpan); }

    [[nodiscard]] static std::array<std::size_t, 2> extentsFor(const auto& src) noexcept { return {image::rows(src), image::columns(src)}; }

    static void apply(const auto& src, std::span<T> dst) {
        std::transform(src.data(), src.data() + dst.size(), dst.begin(), [](std::uint32_t p) {
            const double luma = bt601Luma(p);
            if constexpr (std::is_same_v<T, std::uint32_t>) {
                return static_cast<T>(std::lround(luma));
            } else {
                return static_cast<T>(luma / 255.0);
            }
        });
    }
};

template<ImageSample T>
struct ImageCrop : gr::Block<ImageCrop<T>> {
    using Description = gr::Doc<"@brief Crops the region [x, x+width) x [y, y+height) of a frame, clipped to it; width/height 0 extend to the frame edge.">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::TensorPortIn<T>  in;
    gr::TensorPortOut<T> out;

    A<gr::Size_t, "x", gr::Visible, gr::Unit<"px">>      x      = 0U;
    A<gr::Size_t, "y", gr::Visible, gr::Unit<"px">>      y      = 0U;
    A<gr::Size_t, "width", gr::Visible, gr::Unit<"px">>  width  = 0U;
    A<gr::Size_t, "height", gr::Visible, gr::Unit<"px">> height = 0U;

    GR_MAKE_REFLECTABLE(ImageCrop, in, out, x, y, width, height);

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) { return image::forEachFrame(*this, inSpan, outSpan); }

    /// the clipped region {y, x, height, width}
    [[nodiscard]] std::array<std::size_t, 4> region(const auto& src) const noexcept {
        const std::size_t x0 = std::min<std::size_t>(x, image::columns(src));
        const std::size_t y0 = std::min<std::size_t>(y, image::rows(src));
        const std::size_t h  = height == 0U ? image::rows(src) - y0 : std::min<std::size_t>(height, image::rows(src) - y0);
        const std::size_t w  = width == 0U ? image::columns(src) - x0 : std::min<std::size_t>(width, image::columns(src) - x0);
        return {y0, x0, h, w};
    }

    [[nodiscard]] std::array<std::size_t, 2> extentsFor(const auto& src) const noexcept {
        const auto [y0, x0, h, w] = region(src);
        return {h, w};
    }

    void apply(const auto& src, std::span<T> dst) const {
        const auto [y0, x0, h, w] = region(src);
        for (std::size_t row = 0UZ; row < h; ++row) {
            std::copy_n(src.data() + (y0 + row) * image::columns(src) + x0, w, dst.data() + row * w);
        }
    }
};

template<ImageSample T>
struct ImageResize : gr::Block<ImageResize<T>> {
    using Description = gr::Doc<"@brief Resamples frames to width x height, or by `scale` when positive; nearest, bilinear or bicubic (Catmull-Rom) interpolation.">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::TensorPortIn<T>  in;
    gr::TensorPortOut<T> out;

    A<gr::Size_t, "width", gr::Visible, gr::Unit<"px">, gr::Limits<1U, kMaxImageSide>>                           width         = 320U;
    A<gr::Size_t, "height", gr::Visible, gr::Unit<"px">, gr::Limits<1U, kMaxImageSide>>                          height        = 240U;
    A<float, "scale", gr::Visible, gr::Limits<0.f, 16.f>, gr::Doc<"if > 0: output = round(input size × scale)">> scale         = 0.f;
    A<Interpolation, "interpolation", gr::Visible>                                                               interpolation = Interpolation::bilinear;

    GR_MAKE_REFLECTABLE(ImageResize, in, out, width, height, scale, interpolation);

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) { return image::forEachFrame(*this, inSpan, outSpan); }

    [[nodiscard]] std::array<std::size_t, 2> extentsFor(const auto& src) const noexcept {
        const auto size = [this](std::size_t n, gr::Size_t fixed) -> std::size_t {
            if (n == 0UZ) {
                return 0UZ;
            }
            if (!(scale > 0.f)) {
                return fixed;
            }
            return std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(static_cast<double>(n) * static_cast<double>(scale.value))), 1UZ, std::size_t{kMaxImageSide});
        };
        return {size(image::rows(src), height), size(image::columns(src), width)};
    }

    void apply(const auto& src, std::span<T> dst) const {
        const std::size_t srcH  = image::rows(src);
        const std::size_t srcW  = image::columns(src);
        const auto [dstH, dstW] = extentsFor(src);
        const double sy         = static_cast<double>(srcH) / static_cast<double>(std::max(dstH, 1UZ));
        const double sx         = static_cast<double>(srcW) / static_cast<double>(std::max(dstW, 1UZ));
        for (std::size_t row = 0UZ; row < dstH; ++row) {
            const double fy = (static_cast<double>(row) + 0.5) * sy - 0.5;
            for (std::size_t col = 0UZ; col < dstW; ++col) {
                dst[row * dstW + col] = image::sample<T>(src, fy, (static_cast<double>(col) + 0.5) * sx - 0.5, interpolation);
            }
        }
    }
};

template<ImageSample T>
struct ImageDistortion : gr::Block<ImageDistortion<T>> {
    using Description = gr::Doc<"@brief Remaps frames, output(x) = input(model(x)); samples outside the input, or from a NaN in the model, take `fill`.">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::TensorPortIn<T>  in;
    gr::TensorPortOut<T> out;

    A<DistortionModel, "model", gr::Visible, gr::Doc<"brownConrady: radial k1..k3 (r², r⁴, r⁶) and tangential p1, p2, normalised to the half-diagonal about the centre; kannalaBrandt: equidistant fish-eye, θ = atan(r / (focal·half-diagonal)), source radius focal·θ(1 + k1θ² + k2θ⁴ + k3θ⁶ + k4θ⁸); map: map_x, map_y; homography: 3x3 matrix (projector keystone)">> model = DistortionModel::brownConrady;
    A<float, "k1", gr::Visible, gr::Doc<"radial coefficient: brownConrady r² (> 0 undoes barrel), kannalaBrandt θ²">>                                                                                                                                                                                                                                                     k1    = 0.f;
    A<float, "k2", gr::Doc<"radial coefficient: r⁴ or θ⁴">>                                                                                                                                                                                                                                                                                                               k2    = 0.f;
    A<float, "k3", gr::Doc<"radial coefficient: r⁶ or θ⁶">>                                                                                                                                                                                                                                                                                                               k3    = 0.f;
    A<float, "k4", gr::Doc<"kannalaBrandt θ^8 coefficient">>                                                                                                                                                                                                                                                                                                              k4    = 0.f;
    A<float, "focal", gr::Doc<"kannalaBrandt focal length as fraction of the half-diagonal">>                                                                                                                                                                                                                                                                             focal = 1.f;
    A<std::vector<float>, "map x", gr::Doc<"map: source column per output pixel, row-major, frame-sized; otherwise identity">>                                                                                                                                                                                                                                            map_x;
    A<std::vector<float>, "map y", gr::Doc<"map: source row per output pixel, row-major, frame-sized; otherwise identity">>                                                                                                                                                                                                                                               map_y;
    A<std::vector<float>, "homography", gr::Doc<"homography: row-major 3x3 from output pixel (x, y, 1) to source pixel; otherwise identity">>                                                                                                                                                                                                                             homography;
    A<float, "p1", gr::Doc<"tangential coefficient">>                                                                                                                                                                                                                                                                                                                     p1            = 0.f;
    A<float, "p2", gr::Doc<"tangential coefficient">>                                                                                                                                                                                                                                                                                                                     p2            = 0.f;
    A<float, "centre x", gr::Doc<"brownConrady and kannalaBrandt centre as fraction of the width">>                                                                                                                                                                                                                                                                       centre_x      = 0.5f;
    A<float, "centre y", gr::Doc<"brownConrady and kannalaBrandt centre as fraction of the height">>                                                                                                                                                                                                                                                                      centre_y      = 0.5f;
    A<Interpolation, "interpolation", gr::Visible>                                                                                                                                                                                                                                                                                                                        interpolation = Interpolation::bilinear;
    A<T, "fill", gr::Doc<"value outside the input frame">>                                                                                                                                                                                                                                                                                                                fill          = std::is_same_v<T, std::uint32_t> ? T(0xFF000000U) : T(0);

    GR_MAKE_REFLECTABLE(ImageDistortion, in, out, model, k1, k2, k3, k4, focal, map_x, map_y, homography, p1, p2, centre_x, centre_y, interpolation, fill);

    std::vector<std::array<double, 2>> _map; // per output pixel: source (fy, fx)
    std::size_t                        _mapRows    = 0UZ;
    std::size_t                        _mapColumns = 0UZ;

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) { return image::forEachFrame(*this, inSpan, outSpan); }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) { _mapRows = _mapColumns = 0UZ; }

    [[nodiscard]] static std::array<std::size_t, 2> extentsFor(const auto& src) noexcept { return {image::rows(src), image::columns(src)}; }

    void apply(const auto& src, std::span<T> dst) {
        const std::size_t h = image::rows(src);
        const std::size_t w = image::columns(src);
        if (h != _mapRows || w != _mapColumns) {
            buildMap(h, w);
        }
        const double maxY = static_cast<double>(h) - 0.5;
        const double maxX = static_cast<double>(w) - 0.5;
        for (std::size_t i = 0UZ; i < _map.size(); ++i) {
            const auto [fy, fx] = _map[i];
            dst[i]              = !(fy >= -0.5 && fy < maxY && fx >= -0.5 && fx < maxX) ? T(fill) : image::sample<T>(src, fy, fx, interpolation); // NaN: fill
        }
    }

    void buildMap(std::size_t h, std::size_t w) {
        const double cy                 = static_cast<double>(centre_y) * static_cast<double>(h) - 0.5;
        const double cx                 = static_cast<double>(centre_x) * static_cast<double>(w) - 0.5;
        const double radius             = std::max(0.5 * std::hypot(static_cast<double>(w), static_cast<double>(h)), 1.0);
        const auto [c1, c2, c3, t1, t2] = std::array{static_cast<double>(k1), static_cast<double>(k2), static_cast<double>(k3), static_cast<double>(p1), static_cast<double>(p2)};
        _map.resize(h * w);
        _mapRows    = h;
        _mapColumns = w;
        switch (model.value) {
        case DistortionModel::map: {
            const bool sized = map_x.value.size() == h * w && map_y.value.size() == h * w;
            for (std::size_t i = 0UZ; i < h * w; ++i) {
                _map[i] = sized ? std::array{static_cast<double>(map_y.value[i]), static_cast<double>(map_x.value[i])} : std::array{static_cast<double>(i / w), static_cast<double>(i % w)};
            }
            break;
        }
        case DistortionModel::homography: {
            const auto& m = homography.value;
            const auto  e = [&m](std::size_t k) { return static_cast<double>(m[k]); };
            for (std::size_t i = 0UZ; i < h * w; ++i) {
                const auto x = static_cast<double>(i % w), y = static_cast<double>(i / w);
                if (m.size() != 9UZ) {
                    _map[i] = {y, x};
                    continue;
                }
                const double scale = e(6UZ) * x + e(7UZ) * y + e(8UZ);
                _map[i]            = scale == 0.0 ? std::array{-1.0, -1.0} : std::array{(e(3UZ) * x + e(4UZ) * y + e(5UZ)) / scale, (e(0UZ) * x + e(1UZ) * y + e(2UZ)) / scale};
            }
            break;
        }
        case DistortionModel::kannalaBrandt: {
            const double f  = std::max(static_cast<double>(focal), 1e-6) * radius;
            const double c4 = static_cast<double>(k4);
            for (std::size_t row = 0UZ; row < h; ++row) {
                for (std::size_t col = 0UZ; col < w; ++col) {
                    const double dy = static_cast<double>(row) - cy, dx = static_cast<double>(col) - cx;
                    const double r     = std::hypot(dx, dy);
                    const double theta = std::atan(r / f), theta2 = theta * theta;
                    const double scale  = r > 0.0 ? f * theta * (1.0 + theta2 * (c1 + theta2 * (c2 + theta2 * (c3 + theta2 * c4)))) / r : 1.0;
                    _map[row * w + col] = {cy + dy * scale, cx + dx * scale};
                }
            }
            break;
        }
        case DistortionModel::brownConrady:
            for (std::size_t row = 0UZ; row < h; ++row) {
                const double y = (static_cast<double>(row) - cy) / radius;
                for (std::size_t col = 0UZ; col < w; ++col) {
                    const double x      = (static_cast<double>(col) - cx) / radius;
                    const double r2     = x * x + y * y;
                    const double radial = 1.0 + r2 * (c1 + r2 * (c2 + r2 * c3));
                    const double xs     = x * radial + 2.0 * t1 * x * y + t2 * (r2 + 2.0 * x * x);
                    const double ys     = y * radial + t1 * (r2 + 2.0 * y * y) + 2.0 * t2 * x * y;
                    _map[row * w + col] = {cy + ys * radius, cx + xs * radius};
                }
            }
            break;
        }
    }
};

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_IMAGETRANSFORMS_HPP
