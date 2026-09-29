#ifndef OPENDIGITIZER_UTILS_PNGHELPER_HPP
#define OPENDIGITIZER_UTILS_PNGHELPER_HPP

#include <string>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include <span>
#include <stb_image_write.h>

namespace opendigitizer::png {

bool writePng(std::vector<unsigned char>& data, int width, int height, const std::string& filename) {
    std::vector<unsigned char> flipped;
    // need to flip from OpenGL
    for (int y = 0; y < height; ++y) {
        auto row{std::span{data}.subspan(static_cast<size_t>((height - 1 - y) * width * 4), static_cast<size_t>(width * 4))};
        flipped.append_range(row);
    }
    return stbi_write_png(filename.data(), width, height, 4, flipped.data(), 4 * width);
}

} // namespace opendigitizer::png

#endif // OPENDIGITIZER_UTILS_PNGHELPER_HPP
