#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/TensorSource.hpp>

#include <opendigitizer/video/CameraSource.hpp>
#include <opendigitizer/video/Mjpeg.hpp>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace opendigitizer::video;
using Sink = gr::testing::TensorCollector<std::uint32_t>;

struct ClockTagSource : gr::Block<ClockTagSource> {
    using Description = gr::Doc<"Test clock emitting n_samples samples, each with a 'clk_index' tag.">;
    gr::PortOut<std::uint8_t> out;
    gr::Size_t                n_samples = 3U;

    GR_MAKE_REFLECTABLE(ClockTagSource, out, n_samples);

    gr::Size_t _emitted = 0U;

    [[nodiscard]] gr::work::Status processBulk(gr::OutputSpanLike auto& outSpan) {
        const std::size_t n = std::min(static_cast<std::size_t>(n_samples - _emitted), outSpan.size());
        for (std::size_t i = 0UZ; i < n; ++i) {
            outSpan[i] = 1U;
            outSpan.publishTag(gr::property_map{{"clk_index", std::uint64_t{_emitted} + i}}, i);
        }
        _emitted += static_cast<gr::Size_t>(n);
        outSpan.publish(n);
        return _emitted >= n_samples ? gr::work::Status::DONE : gr::work::Status::OK;
    }
};

/// quality 100: stb writes 4:4:4, so flat 8x8 blocks keep their colour to within rounding
[[nodiscard]] std::vector<std::byte> jpegOf(int width, int height, auto&& rgbAt) {
    std::vector<std::uint8_t> rgb;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::array<std::uint8_t, 3> pixel = rgbAt(x, y);
            rgb.insert(rgb.end(), pixel.begin(), pixel.end());
        }
    }
    std::vector<std::byte> jpeg;
    const auto             append = [](void* context, void* data, int size) {
        const auto* bytes = static_cast<const std::byte*>(data);
        static_cast<std::vector<std::byte>*>(context)->insert(static_cast<std::vector<std::byte>*>(context)->end(), bytes, bytes + size);
    };
    stbi_write_jpg_to_func(append, &jpeg, width, height, 3, rgb.data(), 100);
    return jpeg;
}

[[nodiscard]] std::array<std::byte, 4> yuyv(std::uint8_t y0, std::uint8_t u, std::uint8_t y1, std::uint8_t v) { return {std::byte{y0}, std::byte{u}, std::byte{y1}, std::byte{v}}; }

} // namespace

const boost::ut::suite<"camera core"> cameraCoreTests = [] {
    using namespace boost::ut;

    "resolveDevice matches the first device, an exact id or a name substring, never an index"_test = [] {
        const std::array<DeviceInfo, 2> devices{DeviceInfo{.id = "usb-1", .name = "Anker PowerConf C200", .path = "/dev/video0"}, DeviceInfo{.id = "usb-2", .name = "Global Shutter Camera", .path = "/dev/video2"}};
        expect(eq(resolveDevice("", devices).value(), 0UZ));
        expect(!resolveDevice("1", devices).has_value()) << "no index, only names and ids";
        expect(eq(resolveDevice("global shutter", devices).value(), 1UZ));
        expect(eq(resolveDevice("@id:usb-1", devices).value(), 0UZ));
        expect(!resolveDevice("@id:usb", devices).has_value());
        expect(!resolveDevice("nonexistent", devices).has_value());
        expect(!resolveDevice("", std::span<const DeviceInfo>{}).has_value());
    };

    "yuyvToRgba BT.601 limited range"_test = [] {
        std::array<std::byte, 8> src{};
        const auto               white = yuyv(235U, 128U, 16U, 128U);
        const auto               red   = yuyv(81U, 90U, 81U, 240U);
        std::ranges::copy(white, src.begin());
        std::ranges::copy(red, src.begin() + 4);
        std::array<std::uint32_t, 4> dst{};
        yuyvToRgba(src, src.size(), 4U, 1U, dst);
        expect(eq(dst[0], 0xFFFFFFFFU)) << "white";
        expect(eq(dst[1], 0xFF000000U)) << "black";
        expect(eq(dst[2] & 0xFFFF0000U, 0xFFFF0000U)) << "red channel saturated";
        expect(eq((dst[2] >> 8U) & 0xFFU, 0U) && eq(dst[2] & 0xFFU, 0U)) << "green and blue";
    };

    "yuyvToRgba honours the row stride and gives an odd width's last pixel its pair's chroma"_test = [] {
        constexpr std::size_t                kStride = 12UZ; // two macro-pixels (8 bytes) for 3 pixels, plus padding
        std::array<std::byte, 2UZ * kStride> src{};
        std::ranges::fill(src, std::byte{0xAB});
        for (std::size_t row = 0UZ; row < 2UZ; ++row) {
            std::ranges::copy(yuyv(235U, 128U, 16U, 128U), src.begin() + static_cast<std::ptrdiff_t>(row * kStride));
            std::ranges::copy(yuyv(235U, 128U, 0U, 128U), src.begin() + static_cast<std::ptrdiff_t>(row * kStride + 4UZ));
        }
        std::array<std::uint32_t, 6> dst{};
        yuyvToRgba(src, kStride, 3U, 2U, dst);
        for (std::size_t row = 0UZ; row < 2UZ; ++row) {
            expect(eq(dst[row * 3UZ], 0xFFFFFFFFU) && eq(dst[row * 3UZ + 1UZ], 0xFF000000U) && eq(dst[row * 3UZ + 2UZ], 0xFFFFFFFFU)) << "row" << row;
        }
    };

    "packRgba, swapRedBlue and bt601Luma on reference colours"_test = [] {
        expect(eq(packRgba(0x12U, 0x34U, 0x56U), 0xFF123456U));
        expect(eq(swapRedBlue(0xFF123456U), 0xFF563412U)) << "0xAARRGGBB <-> R,G,B,A bytes";
        expect(eq(swapRedBlue(swapRedBlue(0x80A1B2C3U)), 0x80A1B2C3U)) << "its own inverse";
        expect(eq(bt601Luma(0xFFFFFFFFU), 255.0) && eq(bt601Luma(0xFF000000U), 0.0));
        expect(lt(std::abs(bt601Luma(packRgba(255U, 0U, 0U)) - 0.299 * 255.0), 1e-9) && lt(std::abs(bt601Luma(packRgba(0U, 255U, 0U)) - 0.587 * 255.0), 1e-9) && lt(std::abs(bt601Luma(packRgba(0U, 0U, 255U)) - 0.114 * 255.0), 1e-9)) << "R, G, B weights";
    };
};

const boost::ut::suite<"mode selection and test card"> modeTests = [] {
    using namespace boost::ut;

    "decodeMjpeg turns flat colour blocks into 0xAARRGGBB and refuses a mismatching header, a short buffer or a cut stream"_test = [] {
        const auto                 jpeg = jpegOf(16, 8, [](int x, int) { return x < 8 ? std::array<std::uint8_t, 3>{255U, 0U, 0U} : std::array<std::uint8_t, 3>{0U, 0U, 255U}; });
        std::vector<std::uint32_t> dst(16UZ * 8UZ);
        expect(decodeMjpeg(jpeg, 16U, 8U, dst).has_value()) << fatal;
        const auto channel = [](std::uint32_t pixel, unsigned shift) { return static_cast<int>((pixel >> shift) & 0xFFU); };
        for (std::size_t i = 0UZ; i < dst.size(); ++i) {
            const bool red = i % 16UZ < 8UZ;
            expect(eq(channel(dst[i], 24U), 255)) << "opaque, pixel" << i;
            expect(le(std::abs(channel(dst[i], 16U) - (red ? 255 : 0)), 2) && le(channel(dst[i], 8U), 2) && le(std::abs(channel(dst[i], 0U) - (red ? 0 : 255)), 2)) << "colour of pixel" << i;
        }
        expect(!decodeMjpeg(jpeg, 8U, 8U, dst).has_value()) << "header 16x8, mode 8x8";
        expect(!decodeMjpeg(jpeg, 16U, 8U, std::span(dst).first(dst.size() - 1UZ)).has_value()) << "one pixel short";
        expect(!decodeMjpeg(std::span(jpeg).first(16UZ), 16U, 8U, dst).has_value()) << "cut inside the header";
        expect(!decodeMjpeg({}, 16U, 8U, dst).has_value()) << "empty";
    };

    "selectMode picks the mode each priority maximises, and the nearest one for requested"_test = [] {
        using enum PixelFormat;
        const std::array<CameraMode, 5> modes{CameraMode{640U, 480U, 30.0, yuyv}, CameraMode{640U, 480U, 90.0, mjpeg}, CameraMode{1920U, 1080U, 5.0, yuyv}, CameraMode{1920U, 1080U, 30.0, mjpeg}, CameraMode{1920U, 1200U, 90.0, mjpeg}};
        const auto                      pick = [&](ModePriority p, CameraMode request = {}) { return selectMode(modes, p, request).value(); };
        expect(pick(ModePriority::bandwidth).height == 1200U && pick(ModePriority::bandwidth).frameRate == 90.0);
        expect(pick(ModePriority::resolution).height == 1200U);
        expect(pick(ModePriority::rate).frameRate == 90.0 && pick(ModePriority::rate).height == 1200U) << "fastest, then largest";
        expect(pick(ModePriority::requested, {640U, 480U, 30.0, yuyv}).format == yuyv) << "exact raw mode";
        expect(pick(ModePriority::requested, {1920U, 1080U, 25.0, yuyv}).format == mjpeg) << "full HD at 25 Hz needs MJPEG";
        expect(!selectMode({}, ModePriority::bandwidth, {}).has_value());
    };

    "a test card frame barcode reads back the sequence number it was drawn with"_test = [](std::pair<std::size_t, std::size_t> size) {
        const test_card::Layout    layout{size.first, size.second};
        std::vector<std::uint32_t> pixels(layout.width * layout.height);
        test_card::drawBackground(layout, pixels);
        for (std::uint64_t sequence : {0ULL, 1ULL, 0xA5A5ULL, 0xFFFFULL, 0x1'0002ULL}) {
            test_card::drawFrameStrips(layout, sequence, pixels);
            expect(eq(test_card::readBarcode(layout, pixels).value_or(~0ULL), sequence & 0xFFFFU)) << layout.width << "x" << layout.height;
        }
    } | std::vector<std::pair<std::size_t, std::size_t>>{{64UZ, 48UZ}, {640UZ, 480UZ}, {1920UZ, 1080UZ}};

    "DeviceWatch wakes on a new video node only"_test = [] {
        const auto directory = std::filesystem::temp_directory_path() / std::format("od-device-watch-{}", ::getpid());
        std::filesystem::create_directories(directory);
        DeviceWatch watch(directory.string());
        const auto  past = std::chrono::steady_clock::now();
        std::ofstream(directory / "other").put('x');
        expect(watch.waitUntil(past, true) == DeviceWatch::Wake::deadline) << "unrelated node";
        std::ofstream(directory / "video99").put('x');
        expect(watch.waitUntil(past, false) == DeviceWatch::Wake::deadline) << "not watching";
        expect(watch.waitUntil(past, true) == DeviceWatch::Wake::deviceChanged);
        watch.interrupt();
        expect(watch.waitUntil(std::chrono::steady_clock::now() + std::chrono::hours(1), true) == DeviceWatch::Wake::interrupted);
        watch.interrupt();
        std::ofstream(directory / "video98").put('x');
        watch.reset();
        expect(watch.waitUntil(past, true) == DeviceWatch::Wake::deadline) << "reset drops a pending interrupt and device event";
        std::filesystem::remove_all(directory);
    };
};

namespace {
[[nodiscard]] std::optional<gr::property_map> nested(const gr::property_map& map, std::string_view key) { return map.contains(key) ? std::optional(map.value_or<gr::property_map>(key, {})) : std::nullopt; }
[[nodiscard]] std::optional<gr::property_map> frameMeta(const gr::property_map& tag) { return nested(tag, gr::tag::kTriggerMetaInfo.key()); }
[[nodiscard]] std::optional<gr::property_map> userData(const gr::property_map& tag) { return nested(tag, gr::tag::kUserData.key()); }

/// switches the camera to `device` once it saw `after` frames, and stops it once frames carry user_data with an empty fallback_reason
struct SwitchingSink : gr::Block<SwitchingSink> {
    gr::TensorPortIn<std::uint32_t> in;
    GR_MAKE_REFLECTABLE(SwitchingSink, in);

    CameraSource*            camera = nullptr;
    std::string              device;
    std::size_t              after  = 3UZ;
    std::size_t              frames = 0UZ;
    std::vector<std::string> reasons; // fallback_reason of every user_data tag, in order

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan) {
        for (const auto& tag : inSpan.rawTags()) {
            if (const auto data = userData(gr::property_map(tag.map))) {
                reasons.emplace_back(data->value_or<std::string_view>("fallback_reason", "?"));
            }
        }
        frames += inSpan.size();
        if (frames >= after && frames - inSpan.size() < after) {
            std::ignore = camera->settings().setStaged({{"device", device}}); // what a settings message does
        }
        std::ignore = inSpan.consume(inSpan.size());
        if (reasons.size() > 1UZ && reasons.back().empty()) {
            std::ignore = camera->settings().setStaged({{"n_frames", gr::Size_t{1U}}});
            return gr::work::Status::DONE;
        }
        return gr::work::Status::OK;
    }
};
} // namespace

const boost::ut::suite<"CameraSource (test card)"> cameraSourceTests = [] {
    using namespace boost::ut;
    using namespace std::string_literals;

    "frames, frame tags and unmerged clk tags; the clock's end ends the camera stream"_test = [] {
        gr::Graph graph; // no n_frames: only the clock's end-of-stream can end this run
        auto&     camera = graph.emplaceBlock<CameraSource>({{"device", "test card"}, {"width", gr::Size_t{64U}}, {"height", gr::Size_t{48U}}, {"frame_rate", 200.f}});
        auto&     clock  = graph.emplaceBlock<ClockTagSource>({{"n_samples", gr::Size_t{3U}}});
        auto&     sink   = graph.emplaceBlock<Sink>();
        expect(graph.connect(clock, "out"s, camera, "clk"s).has_value());
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        expect(eq(camera.actual_width.value, gr::Size_t{64U}));
        expect(eq(camera.actual_height.value, gr::Size_t{48U}));
        expect(eq(camera.active_device.value, std::string(TestCardCamera::kName)));
        expect(camera.fallback_reason.value.empty());
        expect(ge(sink._elements.size(), 3UZ)) << "frames until the clock's three tags went out";
        expect(eq(camera.sequenceGaps(), 0UZ));

        std::vector<std::size_t> frameTagIndices;
        std::size_t              clkTags = 0UZ, userDataTags = 0UZ;
        for (const auto& [index, map] : sink._tags) {
            if (const auto meta = frameMeta(map)) {
                frameTagIndices.push_back(index);
                const auto& frame = sink._elements[index];
                expect(sink._shapes[index] == std::vector<std::size_t>{48UZ, 64UZ});
                expect(eq(test_card::readBarcode({64UZ, 48UZ}, {frame.data(), frame.size()}).value_or(~0ULL), meta->value_or<std::uint64_t>("sequence", 0U) & 0xFFFFU)) << "barcode of frame" << index;
                const auto flags = nested(*meta, gr::tag::kAcquisitionFlags.key());
                expect(flags.has_value() && flags->value_or<bool>("test_card", false)) << "test card frames are flagged";
                expect(map.contains(gr::tag::kTriggerTime.key()) && map.contains(gr::tag::kEstFrameRate.key()));
                if (const auto data = userData(map)) {
                    ++userDataTags;
                    expect(index == 0UZ) << "user_data only on change";
                    expect(eq(data->value_or<gr::Size_t>("width", 0U), gr::Size_t{64U}) && data->value_or<std::string_view>("backend", "") == "test_card");
                    expect(map.contains(gr::tag::kAxisName.key()) && map.contains(gr::tag::kFrameRate.key()));
                }
            } else {
                expect(eq(map.size(), 1UZ) && map.contains("clk_index")) << "clk tag kept separate";
                expect(std::ranges::contains(frameTagIndices, index)) << "clk tag sits on a frame sample";
                ++clkTags;
            }
        }
        expect(eq(frameTagIndices.size(), sink._elements.size()));
        expect(eq(userDataTags, 1UZ));
        expect(eq(clkTags, 3UZ));
    };

    "clk tags beyond the pending bound are dropped oldest first and counted"_test = [] {
        gr::Graph graph;
        auto&     camera = graph.emplaceBlock<CameraSource>({{"device", "test card"}, {"width", gr::Size_t{16U}}, {"height", gr::Size_t{8U}}, {"frame_rate", 500.f}});
        auto&     clock  = graph.emplaceBlock<ClockTagSource>({{"n_samples", gr::Size_t{200U}}});
        auto&     sink   = graph.emplaceBlock<Sink>();
        expect(graph.connect(clock, "out"s, camera, "clk"s).has_value());
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        std::vector<std::uint64_t> published;
        for (const auto& [index, map] : sink._tags) {
            if (map.contains("clk_index")) {
                published.push_back(map.value_or<std::uint64_t>("clk_index", 0U));
            }
        }
        expect(eq(published.size() + camera.droppedClkTags(), 200UZ)) << "every clk tag is published or counted";
        expect(std::ranges::is_sorted(published) && std::ranges::adjacent_find(published) == published.end()) << "kept in order";
        expect(!published.empty() && eq(published.back(), std::uint64_t{199U})) << "the newest is never dropped";
    };

    "unknown device falls back to the test card"_test = [] {
        gr::Graph graph;
        auto&     camera = graph.emplaceBlock<CameraSource>({{"device", "no such camera"}, {"width", gr::Size_t{32U}}, {"height", gr::Size_t{24U}}, {"frame_rate", 200.f}, {"n_frames", gr::Size_t{5U}}});
        auto&     sink   = graph.emplaceBlock<Sink>();
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(eq(sink._elements.size() + camera.droppedFrames(), 5UZ));
        expect(camera.fallback_reason.value.contains("no camera matches")) << camera.fallback_reason.value;
        expect(!sink._tags.empty() && userData(sink._tags.front().second).has_value() && !userData(sink._tags.front().second)->value_or<std::string_view>("fallback_reason", "").empty());
    };

    "device setting switches the camera at runtime"_test = [] {
        gr::Graph graph;
        auto&     camera = graph.emplaceBlock<CameraSource>({{"device", "no such camera"}, {"width", gr::Size_t{32U}}, {"height", gr::Size_t{24U}}, {"frame_rate", 500.f}});
        auto&     sink   = graph.emplaceBlock<SwitchingSink>();
        sink.camera      = &camera;
        sink.device      = "test card";
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(ge(camera.opens(), 2UZ));
        expect(sink.reasons.size() >= 2UZ && !sink.reasons.front().empty() && sink.reasons.back().empty()) << "fallback, then the requested test card";
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
