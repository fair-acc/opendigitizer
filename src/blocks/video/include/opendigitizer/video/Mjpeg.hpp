#ifndef OPENDIGITIZER_VIDEO_MJPEG_HPP
#define OPENDIGITIZER_VIDEO_MJPEG_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace opendigitizer::video {

/// decodes one baseline JPEG of exactly width × height into 0xAARRGGBB pixels
[[nodiscard]] std::expected<void, std::string> decodeMjpeg(std::span<const std::byte> jpeg, std::uint32_t width, std::uint32_t height, std::span<std::uint32_t> dst);

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_MJPEG_HPP
