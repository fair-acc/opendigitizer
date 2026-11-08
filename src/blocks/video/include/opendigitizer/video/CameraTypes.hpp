#ifndef OPENDIGITIZER_VIDEO_CAMERATYPES_HPP
#define OPENDIGITIZER_VIDEO_CAMERATYPES_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendigitizer::video {

inline constexpr std::uint32_t    kMinImageSide   = 2U;     // px, the smallest frame side a camera accepts
inline constexpr std::uint32_t    kMaxImageSide   = 16384U; // px, the largest frame side a camera or image block accepts
inline constexpr std::string_view kDeviceIdPrefix = "@id:"; // a `device` selector naming an exact device id

enum class PixelFormat : std::uint8_t { yuyv, mjpeg, rgba8 };
inline constexpr std::array<std::string_view, 3> kPixelFormatNames{"YUYV", "MJPEG", "RGBA8"};
static_assert(std::to_underlying(PixelFormat::rgba8) + 1U == kPixelFormatNames.size());
[[nodiscard]] constexpr std::string_view formatName(PixelFormat format) noexcept { return kPixelFormatNames[std::to_underlying(format)]; }

enum class TimestampSource : std::uint8_t { unknown, driverStartOfExposure, driverEndOfFrame, synthetic, browserMedia };
inline constexpr std::array<std::string_view, 5> kTimestampSourceNames{"unknown", "driver_start_of_exposure", "driver_end_of_frame", "synthetic", "browser_media"};
static_assert(std::to_underlying(TimestampSource::browserMedia) + 1U == kTimestampSourceNames.size());

/// what `selectMode` maximises; `requested` picks the mode nearest to the requested width, height and frame rate
enum class ModePriority : std::uint8_t { bandwidth, resolution, rate, requested };

struct CameraMode {
    std::uint32_t width     = 640U;
    std::uint32_t height    = 480U;
    double        frameRate = 25.0;
    PixelFormat   format    = PixelFormat::yuyv;
};

[[nodiscard]] constexpr bool isSupportedFrameSize(std::uint32_t width, std::uint32_t height) noexcept { return width >= kMinImageSide && height >= kMinImageSide && width <= kMaxImageSide && height <= kMaxImageSide; }

[[nodiscard]] constexpr double pixelRate(const CameraMode& m) noexcept { return static_cast<double>(m.width) * m.height * m.frameRate; }

/// ties prefer raw formats (no decoding)
[[nodiscard]] inline std::optional<CameraMode> selectMode(std::span<const CameraMode> modes, ModePriority priority, const CameraMode& requested) {
    const auto area     = [](const CameraMode& m) { return static_cast<double>(m.width) * m.height; };
    const auto distance = [&requested](const CameraMode& m) { return std::abs(static_cast<double>(m.width) - requested.width) + std::abs(static_cast<double>(m.height) - requested.height); };
    const auto key      = [&](const CameraMode& m) {
        const double raw = m.format == PixelFormat::mjpeg ? 0.0 : 1.0;
        switch (priority) {
        case ModePriority::bandwidth: return std::array{pixelRate(m), raw, m.frameRate};
        case ModePriority::resolution: return std::array{area(m), m.frameRate, raw};
        case ModePriority::rate: return std::array{m.frameRate, area(m), raw};
        case ModePriority::requested: break;
        }
        return std::array{-distance(m), -std::abs(m.frameRate - requested.frameRate), raw};
    };
    if (modes.empty()) {
        return std::nullopt;
    }
    return *std::ranges::max_element(modes, std::less{}, key);
}

/// backend-neutral camera control; common controls carry canonical names (see `kCommonControls`), others the driver's
struct CameraControl {
    enum class Kind : std::uint8_t { integer, boolean, menu };

    std::string name;
    Kind        kind         = Kind::integer;
    double      value        = 0.0;
    double      defaultValue = 0.0;
    double      min          = 0.0;
    double      max          = 0.0;
    double      step         = 1.0;
};

/// offered by both the V4L2 and the browser backend (exposure_time in 100 µs units in both)
inline constexpr std::array<std::string_view, 11> kCommonControls{"brightness", "contrast", "saturation", "sharpness", "exposure_auto", "exposure_time", "white_balance_auto", "white_balance_temperature", "focus_auto", "focus", "zoom"};

struct DeviceInfo {
    std::string id;   // stable across replug where the backend allows it (V4L2 bus_info, browser deviceId)
    std::string name; // V4L2 card name or browser label
    std::string path; // backend handle, e.g. /dev/video2
};

/// "name [id]", as `available_devices` lists a device; `deviceSelector` turns it back into "@id:<id>"
[[nodiscard]] inline std::string deviceLabel(const DeviceInfo& device) { return std::format("{} [{}]", device.name, device.id); }

/// "@id:<id>" of a label "name [id]", or "name [id], <details>" as GR4's audio blocks list them; any other label as it is
[[nodiscard]] inline std::string deviceSelector(std::string_view label) {
    const auto open  = label.rfind(" [");
    const auto close = open == std::string_view::npos ? std::string_view::npos : label.find(']', open);
    if (close == std::string_view::npos) {
        return std::string(label);
    }
    return std::format("{}{}", kDeviceIdPrefix, label.substr(open + 2UZ, close - open - 2UZ));
}

struct FrameInfo {
    std::uint64_t   sequence     = 0U; // driver sequence where provided, else the backend's own delivery count
    std::int64_t    driverTimeNs = 0;  // in the clock domain named by timestampSource; 0 if unavailable
    TimestampSource timestampSource{TimestampSource::unknown};
};

struct FrameView {
    std::span<const std::byte> data;
    std::size_t                stride = 0UZ; // bytes per row
    FrameInfo                  info;
};

enum class CameraErrc { interrupted, disconnected, deviceChanged, unsupported, io };

struct CameraError {
    CameraErrc  code{CameraErrc::io};
    std::string message;
};

[[nodiscard]] constexpr std::uint32_t packRgba(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept { return 0xFF000000U | (r << 16U) | (g << 8U) | b; }

/// ITU-R BT.601 luma of a 0xAARRGGBB pixel, 0..255
[[nodiscard]] constexpr double bt601Luma(std::uint32_t pixel) noexcept { return 0.299 * static_cast<double>((pixel >> 16U) & 0xFFU) + 0.587 * static_cast<double>((pixel >> 8U) & 0xFFU) + 0.114 * static_cast<double>(pixel & 0xFFU); }

/// R,G,B,A bytes (canvas, GL) as a little-endian word ↔ 0xAARRGGBB; its own inverse
[[nodiscard]] constexpr std::uint32_t swapRedBlue(std::uint32_t pixel) noexcept { return (pixel & 0xFF00FF00U) | ((pixel >> 16U) & 0xFFU) | ((pixel & 0xFFU) << 16U); }

/// bytes of whole YUYV macro-pixels (Y0 U Y1 V) covering `width` pixels, an odd width's last one included
[[nodiscard]] constexpr std::size_t yuyvRowBytes(std::uint32_t width) noexcept { return 4UZ * ((std::size_t{width} + 1UZ) / 2UZ); }

/// ITU-R BT.601 limited range; one YUYV macro-pixel (Y0 U Y1 V) yields two RGBA pixels; rows of `stride` >= yuyvRowBytes(width)
inline void yuyvToRgba(std::span<const std::byte> src, std::size_t stride, std::uint32_t width, std::uint32_t height, std::span<std::uint32_t> dst) noexcept {
    assert(stride >= yuyvRowBytes(width) && src.size() >= stride * height && dst.size() >= std::size_t{width} * height);
    const auto clip = [](int v) { return static_cast<std::uint32_t>(std::clamp(v, 0, 255)); };
    for (std::size_t row = 0UZ; row < height; ++row) {
        const auto*    in  = reinterpret_cast<const std::uint8_t*>(src.data() + row * stride);
        std::uint32_t* out = dst.data() + row * width;
        for (std::size_t x = 0UZ; x < width; x += 2UZ, in += 4) {
            const int d = in[1] - 128;
            const int e = in[3] - 128;
            for (std::size_t k = 0UZ; k < 2UZ && x + k < width; ++k) { // an odd width's last pixel takes its pair's chroma
                const int c = 298 * (in[2UZ * k] - 16) + 128;
                out[x + k]  = packRgba(clip((c + 409 * e) >> 8), clip((c - 100 * d - 208 * e) >> 8), clip((c + 516 * d) >> 8));
            }
        }
    }
}

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_CAMERATYPES_HPP
