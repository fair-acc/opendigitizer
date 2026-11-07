#ifndef OPENDIGITIZER_VIDEO_ACOUSTICIMAGE_HPP
#define OPENDIGITIZER_VIDEO_ACOUSTICIMAGE_HPP

#include "ImageTransforms.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/algorithm/ArrayGeometry.hpp>
#include <gnuradio-4.0/algorithm/SteeredPower.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace opendigitizer::video {

struct AcousticImage : gr::Block<AcousticImage> {
    using Description = gr::Doc<R""(@brief Steered power of a microphone array on a camera's pixel grid, to overlay the camera image in an ImageChart.
Input: pair lag curves [M(M-1)/2, lags] of gr::localisation::PairCrossCorrelation, zero lag at lags/2; output: [height, width] power, normalised to [0, 1] per frame.
The camera sits at the array centre looking along its normal (+z), image right +x, image up +y.
Row m of `element_positions` is the microphone behind input channel m; the default is the UMA-16 v2.)"">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::TensorPortIn<float>  in;
    gr::TensorPortOut<float> out;

    gr::algorithm::ElementPositions                                                                                                                                   element_positions = gr::algorithm::geometry::uma16();
    gr::algorithm::PropagationSpeed                                                                                                                                   propagation_speed = 343.0;
    gr::algorithm::FieldSetting                                                                                                                                       field             = gr::algorithm::Field::far;
    A<gr::Size_t, "width", gr::Visible, gr::Limits<1U, 1024U>, gr::Doc<"output columns">>                                                                             width             = 64U;
    A<gr::Size_t, "height", gr::Visible, gr::Limits<1U, 1024U>, gr::Doc<"output rows">>                                                                               height            = 40U;
    A<float, "field of view", gr::Visible, gr::Unit<"deg">, gr::Doc<"across the width; equidistant fish-eye: the angle off the axis grows linearly with the radius">> field_of_view     = 120.f;
    A<float, "centre x", gr::Doc<"optical axis as fraction of the width">>                                                                                            centre_x          = 0.5f;
    A<float, "centre y", gr::Doc<"optical axis as fraction of the height">>                                                                                           centre_y          = 0.5f;
    A<bool, "flip horizontal", gr::Visible>                                                                                                                           flip_horizontal   = false;
    A<bool, "flip vertical", gr::Visible>                                                                                                                             flip_vertical     = false;
    A<float, "distance", gr::Visible, gr::Unit<"m">, gr::Doc<"of the sources in the near field">>                                                                     distance          = 1.f;
    A<float, "threshold", gr::Visible, gr::Doc<"relative power below which the output is 0, e.g. transparent in an overlay">>                                         threshold         = 0.f;
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"of the lag axis; forwarded from upstream">>                                                                      sample_rate       = 48000.f;

    GR_MAKE_REFLECTABLE(AcousticImage, in, out, element_positions, propagation_speed, field, width, height, field_of_view, centre_x, centre_y, flip_horizontal, flip_vertical, distance, threshold, sample_rate);

    static constexpr std::size_t kMaxLagEntries = 1UZ << 24U; // 64 MB of per-pixel pair lags

    std::vector<float> _lags;                // per output pixel, its pairs' lags in samples
    std::size_t        _inputElements = 0UZ; // M of the last input frame, 0 before the first
    std::size_t        _inputPairs    = 0UZ;
    bool               _configured    = false;

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& outSpan) { return image::forEachFrame(*this, inSpan, outSpan); }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        gr::algorithm::refuseGeometry(*this, oldSettings, newSettings, _inputElements);
        _configured = false;
    }

    [[nodiscard]] std::array<std::size_t, 2> extentsFor(const auto& /*curves*/) const noexcept { return {std::max<gr::Size_t>(height, 1U), std::max<gr::Size_t>(width, 1U)}; }

    void apply(const auto& curves, std::span<float> power) {
        const std::size_t pairs    = curves.rank() == 2UZ ? curves.extent(0UZ) : 0UZ;
        const std::size_t length   = pairs > 0UZ ? curves.extent(1UZ) : 0UZ;
        const auto        M        = static_cast<std::size_t>(std::lround((1.0 + std::sqrt(1.0 + 8.0 * static_cast<double>(pairs))) / 2.0));
        const auto [rows, columns] = extentsFor(curves);
        if (!_configured || pairs != _inputPairs) {
            configure(pairs, M, rows, columns);
        }
        if (length < 2UZ || _lags.size() != power.size() * pairs) {
            std::ranges::fill(power, 0.f);
            return;
        }
        for (std::size_t i = 0UZ; i < power.size(); ++i) {
            power[i] = gr::algorithm::computeSteeredPower(curves.data(), _lags.data() + i * pairs, pairs, length);
        }
        const auto [lo, hi] = std::ranges::minmax(power);
        std::ranges::transform(power, power.begin(), [lo, range = hi - lo, cut = threshold.value](float v) {
            const float relative = range > 0.f ? (v - lo) / range : 0.f;
            return relative >= cut ? relative : 0.f;
        });
    }

    void configure(std::size_t pairs, std::size_t M, std::size_t rows, std::size_t columns) {
        _inputPairs    = pairs;
        _inputElements = M * (M - 1UZ) / 2UZ == pairs ? M : 0UZ;
        _configured    = true;
        _lags.clear();
        const auto&       positions = element_positions.value;
        const auto        placed    = gr::algorithm::elementsOf(positions, propagation_speed.value);
        const bool        far       = field.value == gr::algorithm::Field::far;
        const std::string refusal   = [&]() -> std::string {
            if (_inputElements == 0UZ) {
                return std::format("{} lag curves are no pair count M(M-1)/2", pairs);
            }
            if (M < 2UZ) {
                return "fewer than two elements";
            }
            if (rows * columns * pairs > kMaxLagEntries) {
                return std::format("{}x{} pixels x {} pairs exceed {} lag entries", rows, columns, pairs, kMaxLagEntries);
            }
            if (!placed) {
                return placed.error();
            }
            if (*placed != M) {
                return std::format("{} lag curves need {} elements, element_positions place {}", pairs, M, *placed);
            }
            if (!far && !(distance > 0.f)) {
                return "a near field needs distance > 0";
            }
            return {};
        }();
        if (!refusal.empty()) {
            this->emitErrorMessage("AcousticImage::configure()", gr::Error(refusal));
            return;
        }
        const double        pixelsPerRadian = 0.5 * static_cast<double>(columns) / std::max(0.5 * static_cast<double>(field_of_view) * std::numbers::pi / 180.0, 1e-6);
        std::vector<double> delays(M);
        _lags.resize(rows * columns * pairs);
        for (std::size_t i = 0UZ; i < rows * columns; ++i) {
            const double                du    = (static_cast<double>(i % columns) + 0.5 - static_cast<double>(centre_x) * static_cast<double>(columns)) * (flip_horizontal ? -1.0 : 1.0);
            const double                dv    = (static_cast<double>(i / columns) + 0.5 - static_cast<double>(centre_y) * static_cast<double>(rows)) * (flip_vertical ? -1.0 : 1.0);
            const double                r     = std::hypot(du, dv);
            const double                theta = r / pixelsPerRadian;
            const double                s     = r > 0.0 ? std::sin(theta) / r : 0.0;
            const double                scale = far ? 1.0 : static_cast<double>(distance);
            const std::array<double, 3> point{scale * du * s, -scale * dv * s, scale * std::cos(theta)}; // image rows run down, +y up
            for (std::size_t m = 0UZ; m < M; ++m) {
                const std::array<double, 3> p{positions[m, 0UZ], positions[m, 1UZ], positions[m, 2UZ]};
                delays[m] = gr::algorithm::delayOf(p.data(), point, field.value, propagation_speed.value) * static_cast<double>(sample_rate);
            }
            for (std::size_t p = 0UZ; p < pairs; ++p) {
                const auto ij        = gr::algorithm::pairOf(p, M);
                _lags[i * pairs + p] = static_cast<float>(delays[ij.i] - delays[ij.j]);
            }
        }
    }
};

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_ACOUSTICIMAGE_HPP
