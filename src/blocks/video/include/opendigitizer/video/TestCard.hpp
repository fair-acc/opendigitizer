#ifndef OPENDIGITIZER_VIDEO_TESTCARD_HPP
#define OPENDIGITIZER_VIDEO_TESTCARD_HPP

#include "CameraTypes.hpp"
#include "DeviceWatch.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <vector>

namespace opendigitizer::video {

namespace test_card {

inline constexpr std::size_t kBarcodeBits  = 16UZ;
inline constexpr std::size_t kBarcodeCells = kBarcodeBits + 2UZ; // white start and stop cells around the bits

struct Layout {
    std::size_t width;
    std::size_t height;

    [[nodiscard]] std::size_t cellWidth() const noexcept { return std::max<std::size_t>(1UZ, width / (kBarcodeCells + 2UZ)); }
    [[nodiscard]] std::size_t stripHeight() const noexcept { return std::max<std::size_t>(1UZ, height / 16UZ); }
    [[nodiscard]] std::size_t barcodeTop() const noexcept { return height - std::min(height, stripHeight()); }
    [[nodiscard]] std::size_t motionTop() const noexcept { return barcodeTop() - std::min(barcodeTop(), stripHeight()); }
};

[[nodiscard]] constexpr std::uint32_t grey(std::uint32_t v) noexcept { return packRgba(v, v, v); }

/// PAL-like card: grid on grey, circle with 75 % colour bars, grey wedge and line gratings; the bottom strips hold a
/// moving marker and the frame sequence as a barcode
inline void drawBackground(Layout layout, std::span<std::uint32_t> pixels) {
    static constexpr std::array<std::uint32_t, 8> kBars{packRgba(191U, 191U, 191U), packRgba(191U, 191U, 0U), packRgba(0U, 191U, 191U), packRgba(0U, 191U, 0U), packRgba(191U, 0U, 191U), packRgba(191U, 0U, 0U), packRgba(0U, 0U, 191U), packRgba(0U, 0U, 0U)};
    static constexpr std::array<std::size_t, 5>   kGratingPeriods{12UZ, 8UZ, 6UZ, 4UZ, 2UZ};
    const auto [w, h] = layout;
    const double cx = 0.5 * static_cast<double>(w), cy = 0.5 * static_cast<double>(layout.motionTop());
    const double r    = 0.45 * static_cast<double>(std::min(layout.motionTop(), w));
    const auto   grid = std::max<std::size_t>(2UZ, h / 12UZ);
    const double line = std::max(1.0, static_cast<double>(h) / 240.0);
    for (std::size_t y = 0UZ; y < layout.motionTop(); ++y) {
        for (std::size_t x = 0UZ; x < w; ++x) {
            const double  dx = static_cast<double>(x) + 0.5 - cx, dy = static_cast<double>(y) + 0.5 - cy;
            const double  d     = std::sqrt(dx * dx + dy * dy);
            const double  u     = (dx + r) / (2.0 * r); // 0..1 across the circle
            const double  v     = dy / r;               // -1..1 down the circle
            std::uint32_t pixel = (x % grid < static_cast<std::size_t>(line) || y % grid < static_cast<std::size_t>(line)) ? grey(235U) : grey(128U);
            if (std::abs(d - r) < line) {
                pixel = grey(235U);
            } else if (d < r) {
                const auto band = [&](std::size_t n) { return std::min(n - 1UZ, static_cast<std::size_t>(u * static_cast<double>(n))); };
                if (v < -0.2) {
                    pixel = kBars[band(kBars.size())];
                } else if (v < 0.2) {
                    pixel = grey(static_cast<std::uint32_t>(band(6UZ) * 51UZ));
                } else {
                    const std::size_t period = kGratingPeriods[band(kGratingPeriods.size())];
                    pixel                    = (x % period) < period / 2UZ ? grey(235U) : grey(16U);
                }
            }
            pixels[y * w + x] = pixel;
        }
    }
}

inline void drawFrameStrips(Layout layout, std::uint64_t sequence, std::span<std::uint32_t> pixels) {
    const auto [w, h]  = layout;
    const auto cell    = layout.cellWidth();
    const auto marker  = (sequence * std::max<std::size_t>(1UZ, w / 64UZ)) % w;
    const auto markerW = std::max<std::size_t>(1UZ, w / 32UZ);
    for (std::size_t y = layout.motionTop(); y < layout.barcodeTop(); ++y) {
        for (std::size_t x = 0UZ; x < w; ++x) {
            pixels[y * w + x] = (x + w - marker) % w < markerW ? grey(235U) : grey(16U);
        }
    }
    for (std::size_t y = layout.barcodeTop(); y < h; ++y) {
        for (std::size_t x = 0UZ; x < w; ++x) {
            const std::size_t c  = x / cell; // cells 1 .. kBarcodeCells; 0 and beyond are quiet zone
            bool              on = false;
            if (c == 1UZ || c == kBarcodeCells) {
                on = true;
            } else if (c > 1UZ && c < kBarcodeCells) {
                on = ((sequence >> (kBarcodeBits - (c - 1UZ))) & 1U) != 0U; // most significant bit first
            }
            pixels[y * w + x] = on ? grey(235U) : grey(16U);
        }
    }
}

/// reads the barcode at fraction `rowFraction` of the strip height; `pixelAt(x, y)` gives the card's pixel at image
/// coordinates from any rendering of it, e.g. a screen capture, in any 8-bit channel order
[[nodiscard]] inline std::optional<std::uint64_t> readBarcode(Layout layout, std::invocable<std::size_t, std::size_t> auto pixelAt, double rowFraction = 0.5) {
    const auto cell = layout.cellWidth();
    const auto y    = std::min(layout.height - 1UZ, layout.barcodeTop() + static_cast<std::size_t>(rowFraction * static_cast<double>(layout.height - layout.barcodeTop())));
    const auto isOn = [&](std::size_t c) {
        const std::uint32_t p = pixelAt(std::min(layout.width - 1UZ, c * cell + cell / 2UZ), y);
        return ((p >> 16U) & 0xFFU) + ((p >> 8U) & 0xFFU) + (p & 0xFFU) > 3U * 128U;
    };
    if (!isOn(1UZ) || !isOn(kBarcodeCells) || isOn(0UZ)) {
        return std::nullopt;
    }
    std::uint64_t value = 0U;
    for (std::size_t c = 2UZ; c < kBarcodeCells; ++c) {
        value = (value << 1U) | (isOn(c) ? 1U : 0U);
    }
    return value;
}

[[nodiscard]] inline std::optional<std::uint64_t> readBarcode(Layout layout, std::span<const std::uint32_t> pixels, double rowFraction = 0.5) {
    return readBarcode(layout, [&](std::size_t x, std::size_t y) { return pixels[y * layout.width + x]; }, rowFraction);
}

} // namespace test_card

/// test card frames at the requested size and rate; while `watching`, a device appearing ends the wait with `deviceChanged`
struct TestCardCamera {
    static constexpr std::string_view kName = "Test card";

    CameraMode                            _mode;
    std::vector<std::uint32_t>            _background;
    std::vector<std::uint32_t>            _frame;
    std::uint64_t                         _sequence = 0U;
    std::chrono::steady_clock::time_point _next;
    DeviceWatch                           _watch;
    bool                                  _watching = false;

    [[nodiscard]] static DeviceInfo device() { return {.id = "test_card", .name = std::string(kName), .path = {}}; }

    [[nodiscard]] std::expected<CameraMode, CameraError> open(const DeviceInfo&, CameraMode requested, bool watchDevices = false) {
        if (!isSupportedFrameSize(requested.width, requested.height) || !(requested.frameRate > 0.0)) {
            return std::unexpected(CameraError{CameraErrc::unsupported, std::format("test card needs {}..{} px sides and a positive frame rate, not {}x{} at {} Hz", kMinImageSide, kMaxImageSide, requested.width, requested.height, requested.frameRate)});
        }
        if (!_watch.interruptible()) {
            return std::unexpected(CameraError{CameraErrc::io, "test card cannot be interrupted: eventfd failed"});
        }
        _mode        = requested;
        _mode.format = PixelFormat::rgba8;
        _background.assign(std::size_t{_mode.width} * _mode.height, 0U);
        test_card::drawBackground(layout(), _background);
        _frame    = _background;
        _sequence = 0U;
        _next     = std::chrono::steady_clock::now();
        _watching = watchDevices;
        _watch.reset();
        return _mode;
    }

    [[nodiscard]] test_card::Layout layout() const noexcept { return {_mode.width, _mode.height}; }

    [[nodiscard]] std::expected<FrameView, CameraError> waitFrame() {
        switch (_watch.waitUntil(_next, _watching)) {
        case DeviceWatch::Wake::interrupted: return std::unexpected(CameraError{CameraErrc::interrupted, "interrupted"});
        case DeviceWatch::Wake::deviceChanged: return std::unexpected(CameraError{CameraErrc::deviceChanged, "a camera device appeared"});
        case DeviceWatch::Wake::deadline: break;
        }
        _next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / _mode.frameRate));
        const std::size_t stripStart = std::size_t{_mode.width} * layout().motionTop();
        std::copy(_background.begin(), _background.begin() + static_cast<std::ptrdiff_t>(stripStart), _frame.begin());
        test_card::drawFrameStrips(layout(), _sequence, _frame);
        const auto timeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return FrameView{.data = std::as_bytes(std::span(_frame)), .stride = std::size_t{_mode.width} * sizeof(std::uint32_t), .info = {.sequence = _sequence++, .driverTimeNs = timeNs, .timestampSource = TimestampSource::synthetic}};
    }

    void release() noexcept {}
    void interrupt() { _watch.interrupt(); }
};

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_TESTCARD_HPP
