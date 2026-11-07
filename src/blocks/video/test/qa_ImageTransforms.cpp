#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/localisation/PairCrossSpectra.hpp>
#include <gnuradio-4.0/testing/TensorSource.hpp>

#include <opendigitizer/video/AcousticImage.hpp>
#include <opendigitizer/video/BrowserMicrophonePairs.hpp>
#include <opendigitizer/video/CameraSource.hpp>
#include <opendigitizer/video/ImageTransforms.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <ranges>
#include <span>
#include <string>

namespace {

using namespace opendigitizer::video;

template<typename T>
[[nodiscard]] gr::Tensor<T> makeImage(std::size_t rows, std::size_t columns, auto&& value) {
    gr::Tensor<T> tensor;
    tensor.resize({rows, columns});
    for (std::size_t r = 0UZ; r < rows; ++r) {
        for (std::size_t c = 0UZ; c < columns; ++c) {
            tensor.data()[r * columns + c] = static_cast<T>(value(r, c));
        }
    }
    return tensor;
}

/// one frame through `block`'s extentsFor() and apply(), as its processBulk() does with an emitted output
[[nodiscard]] auto run(auto& block, const auto& src) {
    using T               = typename std::remove_cvref_t<decltype(block.out)>::value_type::value_type;
    const auto    extents = block.extentsFor(src);
    gr::Tensor<T> dst(gr::extents_from, std::vector<std::size_t>(extents.begin(), extents.end()));
    block.apply(src, std::span<T>(dst.data(), dst.size()));
    return dst;
}

template<typename T>
[[nodiscard]] T at(const gr::Tensor<T>& tensor, std::size_t r, std::size_t c) {
    return tensor.data()[r * tensor.extents()[1] + c];
}

/// pair lag curves of UMA-16 channels `elements` (empty: all 16) for a source in `direction` (far) or at that point (near) at 48 kHz: a Gaussian at
/// each pair's lag, zero lag at 32
[[nodiscard]] gr::Tensor<float> lagCurvesOf(const std::array<double, 3>& direction, std::vector<std::size_t> elements = {}, gr::algorithm::Field field = gr::algorithm::Field::far) {
    if (elements.empty()) {
        elements = std::views::iota(0UZ, 16UZ) | std::ranges::to<std::vector>();
    }
    constexpr std::size_t kLength   = 65UZ;
    const std::size_t     kElements = elements.size();
    const auto            positions = gr::algorithm::geometry::uma16();
    const auto            delayOf   = [&](std::size_t m) { return gr::algorithm::delayOf(std::array{positions[elements[m], 0UZ], positions[elements[m], 1UZ], positions[elements[m], 2UZ]}.data(), direction, field, 343.0) * 48000.0; };
    const std::size_t     pairs     = gr::algorithm::pairsOf(kElements);
    gr::Tensor<float>     curves(gr::extents_from, {pairs, kLength});
    for (std::size_t p = 0UZ; p < pairs; ++p) {
        const auto   ij  = gr::algorithm::pairOf(p, kElements);
        const double lag = delayOf(ij.i) - delayOf(ij.j);
        for (std::size_t k = 0UZ; k < kLength; ++k) {
            const double x                 = (static_cast<double>(k) - 32.0 - lag) / 1.5;
            curves.data()[p * kLength + k] = static_cast<float>(std::exp(-0.5 * x * x));
        }
    }
    return curves;
}

/// the UMA-16 rows `channels`, in that order: the microphones behind the inputs
[[nodiscard]] gr::Tensor<double> uma16Rows(std::span<const std::size_t> channels) {
    const auto         all = gr::algorithm::geometry::uma16();
    gr::Tensor<double> rows(gr::extents_from, {channels.size(), 3UZ});
    for (std::size_t m = 0UZ; m < channels.size(); ++m) {
        for (std::size_t k = 0UZ; k < 3UZ; ++k) {
            rows[m, k] = all[channels[m], k];
        }
    }
    return rows;
}

template<typename T>
[[nodiscard]] gr::Tensor<T> resize(const gr::Tensor<T>& src, std::size_t width, std::size_t height, Interpolation method, float scale = 0.f) {
    ImageResize<T> block;
    block.width         = static_cast<gr::Size_t>(width);
    block.height        = static_cast<gr::Size_t>(height);
    block.scale         = scale;
    block.interpolation = method;
    return run(block, src);
}

template<typename T>
[[nodiscard]] gr::Tensor<T> distort(ImageDistortion<T>& block, const gr::Tensor<T>& src) {
    return run(block, src);
}

template<typename T>
[[nodiscard]] ImageDistortion<T> distortion(float k1, float p1, Interpolation method, T fill) {
    ImageDistortion<T> block;
    block.k1            = k1;
    block.p1            = p1;
    block.interpolation = method;
    block.fill          = fill;
    return block;
}

/// `frames` through `block` in a graph, a tag on frame 1; returns what arrived
template<typename TBlock, typename T>
[[nodiscard]] gr::testing::TensorCollector<T> throughGraph(gr::property_map settings, const std::vector<gr::Tensor<T>>& frames) {
    using namespace std::string_literals;
    gr::Graph graph;
    auto&     source = graph.emplaceBlock<gr::testing::TensorSource<T>>();
    auto&     block  = graph.emplaceBlock<TBlock>(std::move(settings));
    auto&     sink   = graph.emplaceBlock<gr::testing::TensorCollector<T>>();
    for (const gr::Tensor<T>& frame : frames) {
        source._frames.push_back({.extents = std::vector<std::size_t>(frame.extents().begin(), frame.extents().end()), .elements = std::vector<T>(frame.data(), frame.data() + frame.size())});
    }
    source._frameTags.emplace_back(1UZ, gr::property_map{{"marker", std::uint64_t{1U}}});
    boost::ut::expect(graph.connect(source, "out"s, block, "in"s).has_value());
    boost::ut::expect(graph.connect(block, "out"s, sink, "in"s).has_value());
    gr::scheduler::Simple sched;
    boost::ut::expect(sched.exchange(std::move(graph)).has_value());
    boost::ut::expect(sched.runAndWait().has_value());
    return std::move(sink);
}

} // namespace

const boost::ut::suite<"image transforms"> imageTransformTests = [] {
    using namespace boost::ut;
    using namespace std::string_literals;

    "GreyScale in a graph keeps extents and frame tags"_test = [] {
        gr::Graph graph;
        auto&     camera = graph.emplaceBlock<CameraSource>({{"device", "test card"}, {"width", gr::Size_t{16U}}, {"height", gr::Size_t{4U}}, {"frame_rate", 500.f}, {"n_frames", gr::Size_t{5U}}});
        auto&     grey   = graph.emplaceBlock<GreyScale<float>>();
        auto&     sink   = graph.emplaceBlock<gr::testing::TensorCollector<float>>();
        expect(graph.connect(camera, "out"s, grey, "in"s, {.minBufferSize = 8UZ}).has_value());
        expect(graph.connect(grey, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(eq(sink._elements.size() + camera.droppedFrames(), 5UZ));
        expect(!sink._shapes.empty() && sink._shapes[0] == std::vector<std::size_t>{4UZ, 16UZ});
        expect(eq(std::ranges::count_if(sink._tags, [](const auto& entry) { return entry.second.contains(gr::tag::kTriggerMetaInfo.key()); }), static_cast<std::ptrdiff_t>(sink._elements.size())));
    };

    "GreyScale BT.601 luma"_test = [] {
        const auto               rgb = makeImage<std::uint32_t>(1UZ, 3UZ, [](std::size_t, std::size_t c) { return std::array{0xFFFFFFFFU, 0xFF000000U, 0xFFFF0000U}[c]; });
        GreyScale<float>         toFloat;
        GreyScale<std::uint32_t> toInteger;
        const auto               f = run(toFloat, rgb);
        const auto               u = run(toInteger, rgb);
        expect(eq(f.extents()[0], 1UZ) && eq(f.extents()[1], 3UZ));
        expect(lt(std::abs(at(f, 0UZ, 0UZ) - 1.f), 1e-6f)) << "white";
        expect(eq(at(f, 0UZ, 1UZ), 0.f)) << "black";
        expect(lt(std::abs(at(f, 0UZ, 2UZ) - 0.299f), 1e-6f)) << "red";
        expect(eq(at(u, 0UZ, 0UZ), 255U) && eq(at(u, 0UZ, 2UZ), 76U)) << "uint32 luma 0..255";
    };

    "ImageCrop clips the region to the frame"_test = [] {
        const auto       src = makeImage<float>(4UZ, 4UZ, [](std::size_t r, std::size_t c) { return 10 * r + c; });
        ImageCrop<float> crop;
        crop.x      = 1U;
        crop.y      = 2U;
        crop.width  = 5U; // clipped to 3
        crop.height = 0U; // to the bottom edge
        auto dst    = run(crop, src);
        expect(eq(dst.extents()[0], 2UZ) && eq(dst.extents()[1], 3UZ));
        expect(eq(at(dst, 0UZ, 0UZ), 21.f) && eq(at(dst, 1UZ, 2UZ), 33.f));
        crop.x = 9U;
        dst    = run(crop, src);
        expect(eq(dst.size(), 0UZ)) << "origin outside the frame gives an empty frame";
    };

    "ImageResize nearest, bilinear, bicubic"_test = [] {
        const auto ramp = makeImage<double>(4UZ, 8UZ, [](std::size_t, std::size_t c) { return static_cast<double>(c); });
        const auto up   = resize(makeImage<double>(2UZ, 2UZ, [](std::size_t r, std::size_t c) { return 2.0 * static_cast<double>(r) + static_cast<double>(c); }), 4UZ, 4UZ, Interpolation::nearest);
        expect(eq(at(up, 0UZ, 1UZ), 0.0) && eq(at(up, 0UZ, 2UZ), 1.0) && eq(at(up, 3UZ, 3UZ), 3.0)) << "nearest replicates";

        const auto half = resize(ramp, 4UZ, 2UZ, Interpolation::bilinear);
        expect(eq(half.extents()[0], 2UZ) && eq(half.extents()[1], 4UZ));
        for (std::size_t c = 0UZ; c < 4UZ; ++c) {
            expect(lt(std::abs(at(half, 0UZ, c) - (2.0 * static_cast<double>(c) + 0.5)), 1e-12)) << "bilinear on a ramp is exact, column " << c;
        }

        const auto constant = makeImage<double>(5UZ, 5UZ, [](std::size_t, std::size_t) { return 7.0; });
        for (const auto method : {Interpolation::nearest, Interpolation::bilinear, Interpolation::bicubic}) {
            const auto r = resize(constant, 9UZ, 3UZ, method);
            expect(std::ranges::all_of(std::span(r.data(), r.size()), [](double v) { return std::abs(v - 7.0) < 1e-12; })) << "constant preserved";
        }

        const auto rgba = makeImage<std::uint32_t>(3UZ, 3UZ, [](std::size_t, std::size_t) { return 0xFF336699U; });
        const auto big  = resize(rgba, 0UZ, 0UZ, Interpolation::bicubic, 2.f);
        expect(eq(big.extents()[0], 6UZ) && eq(big.extents()[1], 6UZ)) << "scale overrides width/height";
        expect(std::ranges::all_of(std::span(big.data(), big.size()), [](std::uint32_t p) { return p == 0xFF336699U; })) << "RGBA channels interpolated separately";
    };

    "ImageDistortion without coefficients is the identity"_test = [] {
        const auto src   = makeImage<std::uint32_t>(6UZ, 9UZ, [](std::size_t r, std::size_t c) { return 0xFF000000U | static_cast<std::uint32_t>(r * 0x10203U + c * 0x30201U); });
        auto       block = distortion<std::uint32_t>(0.f, 0.f, Interpolation::nearest, 0U);
        const auto dst   = distort(block, src);
        expect(eq(dst.extents()[0], 6UZ) && eq(dst.extents()[1], 9UZ));
        expect(std::ranges::equal(std::span(dst.data(), dst.size()), std::span(src.data(), src.size())));
    };

    "ImageDistortion radial model along the centre row, fill outside"_test = [] {
        constexpr std::size_t kRows = 21UZ, kColumns = 41UZ;
        constexpr double      kK1 = 0.2, kCentre = 20.0;
        const double          radius = 0.5 * std::hypot(static_cast<double>(kColumns), static_cast<double>(kRows));
        const auto            ramp   = makeImage<double>(kRows, kColumns, [](std::size_t, std::size_t c) { return static_cast<double>(c); });
        auto                  block  = distortion<double>(static_cast<float>(kK1), 0.f, Interpolation::bilinear, -1.0);
        const auto            dst    = distort(block, ramp);
        std::size_t           filled = 0UZ;
        for (std::size_t c = 0UZ; c < kColumns; ++c) {
            const double x        = (static_cast<double>(c) - kCentre) / radius;
            const double source   = kCentre + x * (1.0 + kK1 * x * x) * radius;
            const bool   outside  = source < -0.5 || source >= static_cast<double>(kColumns) - 0.5;
            const double expected = outside ? -1.0 : std::clamp(source, 0.0, static_cast<double>(kColumns - 1UZ));
            filled += outside ? 1UZ : 0UZ;
            expect(approx(at(dst, kRows / 2UZ, c), expected, 1e-4)) << "column" << c;
        }
        expect(gt(filled, 0UZ) && lt(filled, kColumns)) << "both fill and samples on the centre row";
        expect(eq(at(dst, 0UZ, 0UZ), -1.0)) << "corner maps outside";
    };

    "ImageDistortion equidistant fish-eye along the centre row"_test = [] {
        constexpr std::size_t kRows = 21UZ, kColumns = 41UZ;
        constexpr double      kCentre = 20.0, kFocal = 0.5;
        const double          f     = kFocal * 0.5 * std::hypot(static_cast<double>(kColumns), static_cast<double>(kRows));
        const auto            ramp  = makeImage<double>(kRows, kColumns, [](std::size_t, std::size_t c) { return static_cast<double>(c); });
        auto                  block = distortion<double>(0.f, 0.f, Interpolation::bilinear, -1.0);
        block.model                 = DistortionModel::kannalaBrandt;
        block.focal                 = static_cast<float>(kFocal);
        const auto dst              = distort(block, ramp);
        for (std::size_t c = 0UZ; c < kColumns; ++c) {
            const double dx = static_cast<double>(c) - kCentre;
            expect(approx(at(dst, kRows / 2UZ, c), kCentre + std::copysign(f * std::atan(std::abs(dx) / f), dx), 1e-4)) << "column" << c << "samples the equidistant radius";
        }
    };

    "ImageDistortion explicit map, identity when not frame-sized"_test = [] {
        const auto src   = makeImage<double>(3UZ, 4UZ, [](std::size_t r, std::size_t c) { return static_cast<double>(10UZ * r + c); });
        auto       block = distortion<double>(0.f, 0.f, Interpolation::nearest, -1.0);
        block.model      = DistortionModel::map;
        for (std::size_t r = 0UZ; r < 3UZ; ++r) {
            for (std::size_t c = 0UZ; c < 4UZ; ++c) {
                block.map_x.value.push_back(static_cast<float>(3UZ - c));
                block.map_y.value.push_back(static_cast<float>(r));
            }
        }
        const auto mirrored = distort(block, src);
        expect(eq(at(mirrored, 1UZ, 0UZ), 13.0) && eq(at(mirrored, 2UZ, 3UZ), 20.0)) << "mirrored columns";
        const auto other = makeImage<double>(2UZ, 2UZ, [](std::size_t r, std::size_t c) { return static_cast<double>(10UZ * r + c); });
        const auto same  = distort(block, other);
        expect(eq(at(same, 1UZ, 0UZ), 10.0) && eq(at(same, 0UZ, 1UZ), 1.0)) << "identity for another frame size";
    };

    "ImageDistortion homography: projective division, translation, identity without nine entries"_test = [] {
        const auto src   = makeImage<double>(3UZ, 4UZ, [](std::size_t r, std::size_t c) { return static_cast<double>(10UZ * r + c); });
        auto       block = distortion<double>(0.f, 0.f, Interpolation::nearest, -1.0);
        block.model      = DistortionModel::homography;
        expect(eq(at(distort(block, src), 2UZ, 3UZ), 23.0)) << "identity";
        block.homography = std::vector{2.f, 0.f, 2.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f}; // x + 1, scaled by w = 2
        block.settingsChanged({}, {});
        const auto shifted = distort(block, src);
        expect(eq(at(shifted, 1UZ, 0UZ), 11.0) && eq(at(shifted, 2UZ, 2UZ), 23.0) && eq(at(shifted, 0UZ, 3UZ), -1.0)) << "shifted left, right edge filled";
    };

    "ImageDistortion rebuilds its map when the frame size changes"_test = [] {
        auto       block  = distortion<float>(0.1f, 0.f, Interpolation::bilinear, 0.f);
        const auto first  = distort(block, makeImage<float>(8UZ, 8UZ, [](std::size_t r, std::size_t c) { return static_cast<float>(r + c); }));
        const auto small  = makeImage<float>(4UZ, 6UZ, [](std::size_t r, std::size_t c) { return static_cast<float>(r * c); });
        const auto second = distort(block, small);
        auto       fresh  = distortion<float>(0.1f, 0.f, Interpolation::bilinear, 0.f);
        const auto ref    = distort(fresh, small);
        expect(eq(first.extents()[0], 8UZ) && eq(second.extents()[0], 4UZ) && eq(second.extents()[1], 6UZ));
        expect(std::ranges::equal(std::span(second.data(), second.size()), std::span(ref.data(), ref.size())));
    };

    "ImageDistortion rebuilds its map when a setting changes"_test = [] {
        const auto src    = makeImage<float>(9UZ, 12UZ, [](std::size_t r, std::size_t c) { return static_cast<float>(r * 12UZ + c); });
        auto       block  = distortion<float>(0.f, 0.f, Interpolation::bilinear, -1.f);
        const auto before = distort(block, src);
        expect(block.settings().set({{"k1", 0.3f}}).empty());
        expect(block.settings().activateContext() != std::nullopt);
        std::ignore      = block.settings().applyStagedParameters();
        const auto after = distort(block, src);
        auto       fresh = distortion<float>(0.3f, 0.f, Interpolation::bilinear, -1.f);
        const auto ref   = distort(fresh, src);
        expect(eq(block.k1.value, 0.3f));
        expect(!std::ranges::equal(std::span(before.data(), before.size()), std::span(after.data(), after.size()))) << "stale map";
        expect(std::ranges::equal(std::span(after.data(), after.size()), std::span(ref.data(), ref.size())));
    };

    "ImageDistortion interpolates RGBA channels independently"_test = [] {
        constexpr std::size_t kRows = 8UZ, kColumns = 10UZ;
        const auto            value = [](std::size_t r, std::size_t c, std::size_t channel) { return std::array{30.0 * static_cast<double>(r), 50.0, 20.0 * static_cast<double>(c), 255.0}[channel]; }; // B, G, R, A
        const auto            rgba  = makeImage<std::uint32_t>(kRows, kColumns, [&](std::size_t r, std::size_t c) {
            std::uint32_t p = 0U;
            for (std::size_t ch = 0UZ; ch < 4UZ; ++ch) {
                p |= static_cast<std::uint32_t>(value(r, c, ch)) << (8U * ch);
            }
            return p;
        });
        auto                  block = distortion<std::uint32_t>(-0.15f, 0.02f, Interpolation::bilinear, 0xFF000000U);
        const auto            dst   = distort(block, rgba);
        for (std::size_t ch = 0UZ; ch < 4UZ; ++ch) {
            auto       plane    = distortion<double>(-0.15f, 0.02f, Interpolation::bilinear, ch == 3UZ ? 255.0 : 0.0);
            const auto expected = distort(plane, makeImage<double>(kRows, kColumns, [&](std::size_t r, std::size_t c) { return value(r, c, ch); }));
            for (std::size_t i = 0UZ; i < dst.size(); ++i) {
                expect(approx(image::channel(dst.data()[i], ch), expected.data()[i], 0.5 + 1e-9)) << "channel" << ch << "pixel" << i;
            }
        }
    };

    "cubicWeight is the Catmull-Rom kernel, and bicubic reproduces a linear ramp"_test = [] {
        expect(eq(image::cubicWeight(0.0), 1.0) && eq(image::cubicWeight(1.0), 0.0) && eq(image::cubicWeight(2.0), 0.0));
        expect(eq(image::cubicWeight(0.5), 0.5625) && eq(image::cubicWeight(-0.5), 0.5625) && eq(image::cubicWeight(1.5), -0.0625));
        const auto ramp = makeImage<double>(1UZ, 8UZ, [](std::size_t, std::size_t c) { return 3.0 * static_cast<double>(c); });
        const auto up   = resize(ramp, 15UZ, 1UZ, Interpolation::bicubic);
        for (std::size_t col = 0UZ; col < 15UZ; ++col) {
            const double fx = (static_cast<double>(col) + 0.5) * 8.0 / 15.0 - 0.5;
            if (fx >= 1.0 && fx <= 5.0) { // all four taps inside the frame
                expect(lt(std::abs(at(up, 0UZ, col) - 3.0 * fx), 1e-9)) << "column" << col;
            }
        }
    };

    "ImageResize clamps a scaled size to the largest frame side"_test = [] {
        const auto wide = resize(makeImage<float>(1UZ, 2000UZ, [](std::size_t, std::size_t) { return 1.f; }), 0UZ, 0UZ, Interpolation::nearest, 16.f);
        expect(wide.extents().size() == 2UZ && eq(wide.extents()[0], 16UZ) && eq(wide.extents()[1], std::size_t{kMaxImageSide}));
    };

    "ImageDistortion brownConrady maps each pixel to its analytic source position"_test = [] {
        constexpr std::size_t   kRows = 24UZ, kColumns = 32UZ;
        ImageDistortion<double> block;
        block.k1           = 0.05f;
        block.k2           = 0.02f;
        block.k3           = 0.01f;
        block.p1           = 0.003f;
        block.p2           = 0.004f;
        block.centre_x     = 0.45f;
        block.centre_y     = 0.55f;
        const auto columns = distort(block, makeImage<double>(kRows, kColumns, [](std::size_t, std::size_t c) { return static_cast<double>(c); }));
        block.settingsChanged({}, {});
        const auto   rows = distort(block, makeImage<double>(kRows, kColumns, [](std::size_t r, std::size_t) { return static_cast<double>(r); }));
        const double cy = 0.55 * kRows - 0.5, cx = 0.45 * kColumns - 0.5, radius = 0.5 * std::hypot(double{kColumns}, double{kRows});
        const auto   k       = [](float v) { return static_cast<double>(v); };
        std::size_t  checked = 0UZ;
        for (std::size_t r = 0UZ; r < kRows; ++r) {
            for (std::size_t c = 0UZ; c < kColumns; ++c) {
                const double x = (static_cast<double>(c) - cx) / radius, y = (static_cast<double>(r) - cy) / radius, r2 = x * x + y * y;
                const double radial = 1.0 + k(block.k1) * r2 + k(block.k2) * r2 * r2 + k(block.k3) * r2 * r2 * r2;
                const double fx     = cx + radius * (x * radial + 2.0 * k(block.p1) * x * y + k(block.p2) * (r2 + 2.0 * x * x));
                const double fy     = cy + radius * (y * radial + k(block.p1) * (r2 + 2.0 * y * y) + 2.0 * k(block.p2) * x * y);
                if (fx >= 0.0 && fx <= double{kColumns - 1UZ} && fy >= 0.0 && fy <= double{kRows - 1UZ}) { // bilinear is exact on a ramp
                    expect(lt(std::abs(at(columns, r, c) - fx), 1e-6) && lt(std::abs(at(rows, r, c) - fy), 1e-6)) << "pixel" << r << c;
                    ++checked;
                }
            }
        }
        expect(gt(checked, kRows * kColumns / 2UZ));
    };

    "ImageDistortion fills a NaN map entry and every pixel of a degenerate homography"_test = [] {
        const auto             src = makeImage<float>(2UZ, 3UZ, [](std::size_t r, std::size_t c) { return static_cast<float>(10UZ * r + c); });
        ImageDistortion<float> mapped;
        mapped.model   = DistortionModel::map;
        mapped.fill    = -1.f;
        mapped.map_x   = std::vector<float>{0.f, 1.f, std::numeric_limits<float>::quiet_NaN(), 0.f, 1.f, 2.f};
        mapped.map_y   = std::vector<float>{0.f, 0.f, 0.f, 1.f, 1.f, 1.f};
        const auto out = distort(mapped, src);
        expect(eq(at(out, 0UZ, 2UZ), -1.f) && eq(at(out, 0UZ, 1UZ), 1.f) && eq(at(out, 1UZ, 2UZ), 12.f)) << "NaN → fill, the rest identity";
        ImageDistortion<float> projective;
        projective.model      = DistortionModel::homography;
        projective.fill       = -1.f;
        projective.homography = std::vector<float>{1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f};
        const auto degenerate = distort(projective, src);
        expect(std::ranges::all_of(std::span(degenerate.data(), degenerate.size()), [](float v) { return v == -1.f; })) << "w = 0 everywhere";
    };

    "ImageCrop, ImageResize and ImageDistortion in a graph pass an empty frame and keep frame tags"_test = [] {
        const std::vector<gr::Tensor<float>> frames{makeImage<float>(4UZ, 6UZ, [](std::size_t r, std::size_t c) { return static_cast<float>(r + c); }), gr::Tensor<float>(gr::extents_from, {0UZ, 0UZ}), makeImage<float>(4UZ, 6UZ, [](std::size_t, std::size_t) { return 2.f; })};
        const auto                           check = [](std::string_view block, const gr::testing::TensorCollector<float>& sink, std::vector<std::size_t> firstShape) {
            expect(eq(sink._shapes.size(), 3UZ)) << fatal << block;
            expect(sink._shapes[0] == firstShape && sink._shapes[1] == std::vector<std::size_t>{0UZ, 0UZ}) << block;
            expect(std::ranges::any_of(sink._tags, [](const auto& entry) { return entry.first == 1UZ && entry.second.contains("marker"); })) << block << "tag on frame 1";
        };
        check("ImageCrop", throughGraph<ImageCrop<float>>({{"x", gr::Size_t{1U}}, {"width", gr::Size_t{3U}}}, frames), {4UZ, 3UZ});
        check("ImageResize", throughGraph<ImageResize<float>>({{"width", gr::Size_t{3U}}, {"height", gr::Size_t{2U}}}, frames), {2UZ, 3UZ});
        check("ImageDistortion", throughGraph<ImageDistortion<float>>({{"k1", 0.1f}}, frames), {4UZ, 6UZ});
    };
};

const boost::ut::suite<"acoustic image"> acousticImageTests = [] {
    using namespace boost::ut;
    using namespace std::string_literals;

    "AcousticImage puts a far source at the pixel of its direction"_test = [] {
        AcousticImage block; // UMA-16, 64x40, 120 deg across the width: 32 columns per 60 deg about column 32, row 20
        const auto    peakOf = [&block](double azimuthDegrees, double elevationDegrees) {
            const double                 a = azimuthDegrees * std::numbers::pi / 180.0, e = elevationDegrees * std::numbers::pi / 180.0;
            const auto                   power = run(block, lagCurvesOf({std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)}));
            const std::span<const float> values(power.data(), power.size());
            expect(eq(std::ranges::max(values), 1.f) && eq(std::ranges::min(values), 0.f)) << "normalised";
            const auto i = static_cast<std::size_t>(std::ranges::max_element(values) - values.begin());
            return std::pair{i / image::columns(power), i % image::columns(power)};
        };
        const auto [rightRow, rightColumn] = peakOf(30.0, 0.0);
        expect(rightColumn >= 47UZ && rightColumn <= 48UZ && rightRow >= 19UZ && rightRow <= 20UZ) << std::format("30 deg right at ({}, {})", rightRow, rightColumn);
        const auto [upRow, upColumn] = peakOf(0.0, 20.0);
        expect(upRow >= 8UZ && upRow <= 10UZ && upColumn >= 31UZ && upColumn <= 32UZ) << std::format("20 deg up at ({}, {})", upRow, upColumn);
        block.flip_horizontal = true;
        block.settingsChanged({}, {});
        const auto [flippedRow, flippedColumn] = peakOf(30.0, 0.0);
        expect(flippedColumn >= 15UZ && flippedColumn <= 16UZ && flippedRow >= 19UZ && flippedRow <= 20UZ) << std::format("mirrored at ({}, {})", flippedRow, flippedColumn);
        block.flip_horizontal   = false;
        block.element_positions = uma16Rows(std::array{0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 5UZ, 6UZ, 7UZ});
        block.settingsChanged({}, {});
        const auto                   firstEight = run(block, lagCurvesOf({std::sin(std::numbers::pi / 6.0), 0.0, std::cos(std::numbers::pi / 6.0)}, std::views::iota(0UZ, 8UZ) | std::ranges::to<std::vector>()));
        const std::span<const float> eight(firstEight.data(), firstEight.size());
        const auto                   eightPeak = static_cast<std::size_t>(std::ranges::max_element(eight) - eight.begin());
        expect(eightPeak % 64UZ >= 47UZ && eightPeak % 64UZ <= 48UZ) << std::format("8 of 16 channels: 30 deg right at column {}", eightPeak % 64UZ);
        block.element_positions = uma16Rows(std::array{1UZ, 14UZ, 7UZ, 8UZ}); // the corner microphones of the browser pairs
        block.settingsChanged({}, {});
        const auto                   corners = run(block, lagCurvesOf({std::sin(std::numbers::pi / 6.0), 0.0, std::cos(std::numbers::pi / 6.0)}, {1UZ, 14UZ, 7UZ, 8UZ}));
        const std::span<const float> four(corners.data(), corners.size());
        const auto                   cornerPeak = static_cast<std::size_t>(std::ranges::max_element(four) - four.begin());
        expect(cornerPeak % 64UZ >= 47UZ && cornerPeak % 64UZ <= 48UZ) << std::format("corner microphones: 30 deg right at column {}", cornerPeak % 64UZ);
        const auto unplaced = run(block, lagCurvesOf({0.0, 0.0, 1.0}));
        expect(std::ranges::all_of(std::span(unplaced.data(), unplaced.size()), [](float v) { return v == 0.f; })) << "16 inputs, 4 positions: no map";
        block.element_positions = gr::algorithm::geometry::uma16();
        block.threshold         = 0.9f;
        block.settingsChanged({}, {});
        const auto cut = run(block, lagCurvesOf({0.0, 0.0, 1.0}));
        expect(std::ranges::all_of(std::span(cut.data(), cut.size()), [](float v) { return v == 0.f || v >= 0.9f; }) && std::ranges::contains(std::span(cut.data(), cut.size()), 1.f)) << "below the threshold 0";
        gr::Tensor<float> fivePairs(gr::extents_from, {5UZ, 65UZ});
        std::ranges::fill(std::span(fivePairs.data(), fivePairs.size()), 1.f);
        const auto unpaired = run(block, fivePairs);
        expect(std::ranges::all_of(std::span(unpaired.data(), unpaired.size()), [](float v) { return v == 0.f; })) << "5 lag curves are no pair count: no map";
        block.width  = 1024U;
        block.height = 1024U;
        block.settingsChanged({}, {});
        const auto oversized = run(block, lagCurvesOf({0.0, 0.0, 1.0}));
        expect(oversized.empty() || std::ranges::all_of(std::span(oversized.data(), oversized.size()), [](float v) { return v == 0.f; })) << "1024x1024 pixels x 120 pairs exceed the lag budget: no map";
        expect(block._lags.empty());
    };

    "AcousticImage mirrors vertically, follows a moved optical centre and focuses a near source"_test = [] {
        const auto direction = [](double azimuthDegrees, double elevationDegrees) {
            const double a = azimuthDegrees * std::numbers::pi / 180.0, e = elevationDegrees * std::numbers::pi / 180.0;
            return std::array{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
        };
        const auto peakOf = [](AcousticImage& block, const std::array<double, 3>& source, gr::algorithm::Field field) {
            const auto                   power = run(block, lagCurvesOf(source, {}, field));
            const std::span<const float> values(power.data(), power.size());
            const auto                   i = static_cast<std::size_t>(std::ranges::max_element(values) - values.begin());
            return std::pair{i / image::columns(power), i % image::columns(power)};
        };
        AcousticImage flipped; // 64x40, 120 deg across the width: 32 columns per 60 deg
        flipped.flip_vertical           = true;
        const auto [flippedRow, column] = peakOf(flipped, direction(0.0, 20.0), gr::algorithm::Field::far);
        expect(flippedRow >= 29UZ && flippedRow <= 31UZ && column >= 31UZ && column <= 32UZ) << std::format("20 deg up, mirrored to row {} column {}", flippedRow, column);
        AcousticImage shifted;
        shifted.centre_x                       = 0.25f;
        const auto [shiftedRow, shiftedColumn] = peakOf(shifted, direction(30.0, 0.0), gr::algorithm::Field::far);
        expect(shiftedColumn >= 31UZ && shiftedColumn <= 32UZ) << std::format("optical axis at column 16: 30 deg right at column {}", shiftedColumn);
        AcousticImage nearField;
        nearField.field                  = gr::algorithm::Field::near;
        nearField.distance               = 1.f;
        const auto [nearRow, nearColumn] = peakOf(nearField, direction(30.0, 0.0), gr::algorithm::Field::near);
        expect(nearColumn >= 47UZ && nearColumn <= 48UZ && nearRow >= 19UZ && nearRow <= 20UZ) << std::format("near source 1 m at 30 deg right at ({}, {})", nearRow, nearColumn);
    };

    "AcousticImage refuses element_positions that do not match the elements the input already showed"_test = [] {
        AcousticImage block;
        block.init(std::make_shared<gr::Sequence>());
        std::ignore = run(block, lagCurvesOf({0.0, 0.0, 1.0}));
        expect(block.settings().set({{"element_positions", uma16Rows(std::array{0UZ, 1UZ, 2UZ})}}).empty());
        std::ignore = block.settings().activateContext();
        std::ignore = block.settings().applyStagedParameters();
        expect(eq(block.element_positions.value.extent(0UZ), 16UZ)) << "the 16-element geometry kept";
    };

    "AcousticImage behind PairCrossCorrelation puts a delayed left microphone's source on the right"_test = [] {
        constexpr std::size_t kSamples = 2048UZ, kDelay = 28UZ; // 0.4 m baseline, 48 kHz, 343 m/s: 28 samples = 30 deg
        const auto            noise      = gr::testing::whiteNoise(kSamples + kDelay, 7U);
        const auto            peakColumn = [&](bool leftLate) {
            gr::Graph          graph;
            auto&              source      = graph.emplaceBlock<gr::testing::TensorSource<float>>();
            auto&              correlation = graph.emplaceBlock<gr::localisation::PairCrossCorrelation<float>>({{"max_lag", gr::Size_t{48U}}, {"sample_rate", 48000.f}});
            auto&              image       = graph.emplaceBlock<AcousticImage>({{"element_positions", gr::Tensor<double>(std::array<std::size_t, 2>{2UZ, 3UZ}, std::vector<double>{-0.2, 0.0, 0.0, 0.2, 0.0, 0.0})}, {"width", gr::Size_t{64U}}, {"height", gr::Size_t{8U}}, {"field_of_view", 120.f}, {"sample_rate", 48000.f}});
            auto&              sink        = graph.emplaceBlock<gr::testing::TensorCollector<float>>();
            std::vector<float> frame(2UZ * kSamples);
            for (std::size_t n = 0UZ; n < kSamples; ++n) {
                const float early = static_cast<float>(noise[n + kDelay]), late = static_cast<float>(noise[n]);
                frame[n]            = leftLate ? late : early; // row 0: the left microphone at x = -0.2 m
                frame[kSamples + n] = leftLate ? early : late;
            }
            source._frames.push_back({.extents = {2UZ, kSamples}, .elements = frame});
            expect(graph.connect(source, "out"s, correlation, "in"s).has_value());
            expect(graph.connect(correlation, "out"s, image, "in"s).has_value());
            expect(graph.connect(image, "out"s, sink, "in"s).has_value());
            gr::scheduler::Simple sched;
            expect(sched.exchange(std::move(graph)).has_value());
            expect(sched.runAndWait().has_value());
            expect(eq(sink._elements.size(), 1UZ)) << fatal;
            const std::vector<float>& power = sink._elements.front();
            return static_cast<std::size_t>(std::ranges::max_element(power) - power.begin()) % 64UZ;
        };
        const std::size_t right = peakColumn(true);
        const std::size_t left  = peakColumn(false);
        expect(right >= 46UZ && right <= 49UZ) << std::format("left late: source at +30 deg, column {}", right);
        expect(left >= 14UZ && left <= 17UZ) << std::format("right late: source at -30 deg, column {}", left);
    };
};

const boost::ut::suite<"browser microphone pairs"> browserMicrophonePairTests = [] {
    using namespace boost::ut;
    using namespace std::string_literals;

    "browser microphone pairs: pending frames are capped at the backlog, the oldest dropped and counted"_test = [] {
        BrowserMicrophonePairs block;
        block.init(std::make_shared<gr::Sequence>());
        block._aligner.maxBacklog = 8UZ;
        const auto write          = [&block](std::size_t frames, float first) {
            float* staging = block.buffer(frames * PairAligner::kChannels);
            for (std::size_t i = 0UZ; i < frames * PairAligner::kChannels; ++i) {
                staging[i] = first + static_cast<float>(i / PairAligner::kChannels);
            }
            block.written(frames * PairAligner::kChannels);
        };
        write(5UZ, 0.f);
        write(6UZ, 5.f);
        expect(eq(block._pending.size(), 8UZ * PairAligner::kChannels));
        expect(eq(block._pending.front(), 3.f) && eq(block._pending.back(), 10.f)) << "frames 0..2 dropped";
        expect(eq(block._droppedPending, 3UZ));
        write(20UZ, 11.f);
        expect(eq(block._pending.size(), 8UZ * PairAligner::kChannels) && eq(block._pending.front(), 23.f)) << "the newest 8 frames of an oversized write";
        expect(eq(block._droppedPending, 23UZ));
    };

    "browser microphone pairs: the buffering offset is a whole number of quanta, removed by the aligner"_test = [] {
        std::vector<float> noise(40000UZ);
        std::ranges::generate(noise, [state = 1U]() mutable {
            state = state * 1664525U + 1013904223U;
            return static_cast<float>(state >> 8U) / 16777216.f - 0.5f;
        });
        const auto shifted = [&noise](std::ptrdiff_t by) { return std::views::iota(0UZ, 30000UZ) | std::views::transform([&noise, by](std::size_t i) { return noise[static_cast<std::size_t>(5000 + static_cast<std::ptrdiff_t>(i) - by)]; }) | std::ranges::to<std::vector>(); };
        const auto a       = shifted(0);
        expect(eq(quantisedLag(a, shifted(-256 + 5), 256UZ, 4UZ, 24UZ).value_or(0), -256z)) << "quantum plus acoustic delay";
        expect(eq(quantisedLag(a, shifted(512 - 7), 256UZ, 4UZ, 24UZ).value_or(0), 512z));
        expect(!quantisedLag(a, std::views::reverse(a) | std::ranges::to<std::vector>(), 256UZ, 4UZ, 24UZ).has_value()) << "uncorrelated streams: no estimate";

        PairAligner        aligner; // pair B 256 samples late, its microphone 3 samples behind pair A's
        const auto         b = shifted(256 + 3);
        std::vector<float> frames;
        for (std::size_t i = 0UZ; i < a.size(); ++i) {
            frames.insert(frames.end(), {a[i], 0.f, b[i], 0.f});
        }
        aligner.push(frames);
        std::array<std::vector<float>, 4UZ> out;
        std::vector<float>                  alignedA, alignedB;
        while (aligner.available() >= 1000UZ) {
            for (auto& o : out) {
                o.resize(1000UZ);
            }
            std::array<std::span<float>, 4UZ> spans{out[0], out[1], out[2], out[3]};
            std::ignore = aligner.pop(spans, 1000UZ);
            alignedA.insert(alignedA.end(), out[0].begin(), out[0].end());
            alignedB.insert(alignedB.end(), out[2].begin(), out[2].end());
        }
        expect(eq(aligner.lag, 256z)) << "estimated and applied";
        const std::span<const float> tailA(alignedA.end() - 8000, alignedA.end()), tailB(alignedB.end() - 8000, alignedB.end());
        expect(eq(quantisedLag(tailA, tailB, 256UZ, 4UZ, 24UZ).value_or(0), 0z)) << "aligned to within the acoustic delay";
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
