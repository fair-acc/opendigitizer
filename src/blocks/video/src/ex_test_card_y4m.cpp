#include <opendigitizer/video/TestCard.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <print>
#include <string_view>
#include <system_error>
#include <vector>

// writes the test card as a looping Y4M clip, e.g. for Chrome's --use-file-for-fake-video-capture
int main(int argc, char** argv) {
    if (argc < 2) {
        std::println(stderr, "usage: {} <out.y4m> [width=640] [height=480] [frame_rate=25] [frames=250]", argv[0]);
        return 2;
    }
    std::array<std::size_t, 4> values{640UZ, 480UZ, 25UZ, 250UZ};
    for (int i = 2; i < argc && i < 6; ++i) {
        const std::string_view arg(argv[i]);
        if (const auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), values[static_cast<std::size_t>(i - 2)]); error != std::errc{} || end != arg.data() + arg.size()) {
            std::println(stderr, "not a non-negative integer: '{}'", arg);
            return 2;
        }
    }
    const auto [width, height, frameRate, frames] = values;
    if (width < opendigitizer::video::kMinImageSide || height < opendigitizer::video::kMinImageSide || width > opendigitizer::video::kMaxImageSide || height > opendigitizer::video::kMaxImageSide || frameRate == 0UZ) {
        std::println(stderr, "needs {}..{} px sides and a positive frame rate", opendigitizer::video::kMinImageSide, opendigitizer::video::kMaxImageSide);
        return 2;
    }
    const opendigitizer::video::test_card::Layout layout{width & ~1UZ, height & ~1UZ};
    std::vector<std::uint32_t>                    rgba(layout.width * layout.height);
    opendigitizer::video::test_card::drawBackground(layout, rgba);

    std::ofstream out(argv[1], std::ios::binary);
    out << std::format("YUV4MPEG2 W{} H{} F{}:1 Ip A1:1 C420jpeg\n", layout.width, layout.height, frameRate);
    std::vector<char> y(layout.width * layout.height), u(y.size() / 4UZ), v(y.size() / 4UZ);
    const auto        channel = [](std::uint32_t p, unsigned shift) { return static_cast<double>((p >> shift) & 0xFFU); };
    for (std::size_t n = 0UZ; n < frames; ++n) {
        opendigitizer::video::test_card::drawFrameStrips(layout, n, rgba);
        for (std::size_t i = 0UZ; i < rgba.size(); ++i) { // BT.601 full range, as 'C420jpeg' declares
            y[i] = static_cast<char>(std::lround(opendigitizer::video::bt601Luma(rgba[i])));
        }
        for (std::size_t row = 0UZ; row < layout.height / 2UZ; ++row) {
            for (std::size_t col = 0UZ; col < layout.width / 2UZ; ++col) {
                const std::uint32_t p = rgba[2UZ * row * layout.width + 2UZ * col];
                const std::size_t   c = row * (layout.width / 2UZ) + col;
                u[c]                  = static_cast<char>(std::clamp(std::lround(128.0 - 0.168736 * channel(p, 16U) - 0.331264 * channel(p, 8U) + 0.5 * channel(p, 0U)), 0L, 255L));
                v[c]                  = static_cast<char>(std::clamp(std::lround(128.0 + 0.5 * channel(p, 16U) - 0.418688 * channel(p, 8U) - 0.081312 * channel(p, 0U)), 0L, 255L));
            }
        }
        out << "FRAME\n";
        out.write(y.data(), static_cast<std::streamsize>(y.size()));
        out.write(u.data(), static_cast<std::streamsize>(u.size()));
        out.write(v.data(), static_cast<std::streamsize>(v.size()));
    }
    return out ? 0 : 1;
}
