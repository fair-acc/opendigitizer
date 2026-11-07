#include <opendigitizer/video/CameraTypes.hpp>
#include <opendigitizer/video/Mjpeg.hpp>

#include <format>
#include <limits>
#include <memory>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS opendigitizer::video::kMaxImageSide
#include <stb_image.h>

namespace opendigitizer::video {

std::expected<void, std::string> decodeMjpeg(std::span<const std::byte> jpeg, std::uint32_t width, std::uint32_t height, std::span<std::uint32_t> dst) {
    if (jpeg.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected("MJPEG frame too large");
    }
    int w = 0, h = 0, channels = 0; // untrusted camera data: the header must match the mode before anything is decoded
    if (stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(jpeg.data()), static_cast<int>(jpeg.size()), &w, &h, &channels) == 0 || static_cast<std::uint32_t>(w) != width || static_cast<std::uint32_t>(h) != height || dst.size() < std::size_t{width} * height) {
        return std::unexpected(std::format("MJPEG frame is {}x{}, expected {}x{}", w, h, width, height));
    }
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> rgba(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(jpeg.data()), static_cast<int>(jpeg.size()), &w, &h, &channels, 4), &stbi_image_free);
    if (!rgba) {
        return std::unexpected(std::format("MJPEG decode: {}", stbi_failure_reason()));
    }
    if (static_cast<std::uint32_t>(w) != width || static_cast<std::uint32_t>(h) != height) {
        return std::unexpected(std::format("MJPEG frame is {}x{}, expected {}x{}", w, h, width, height));
    }
    const stbi_uc* p = rgba.get();
    for (std::uint32_t& pixel : dst.first(std::size_t{width} * height)) {
        pixel = packRgba(p[0], p[1], p[2]);
        p += 4;
    }
    return {};
}

} // namespace opendigitizer::video
