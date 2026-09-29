#ifndef OPENDIGITIZER_UTILS_PNGHELPER_HPP
#define OPENDIGITIZER_UTILS_PNGHELPER_HPP

#include <cstdio>
#include <png.h>
#include <string>
#include <vector>

namespace opendigitizer::png {

bool writePng(std::vector<unsigned char>& data, int width, int height, const std::string& filename) {
    auto file = std::fopen(filename.data(), "wb");
    if (!file) {
        return false;
    }
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png_ptr) {
        png_infop info_ptr = png_create_info_struct(png_ptr);
        if (info_ptr) {
            png_init_io(png_ptr, file);
            png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
            auto                   d = data.data();
            std::vector<png_byte*> row_pointers(height);
            for (int i = 0; i < height; ++i) {
                row_pointers[i] = d + width * (height - i - 1) * 4;
            }
            png_set_rows(png_ptr, info_ptr, row_pointers.data());
            png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);
        }
        png_destroy_write_struct(&png_ptr, &info_ptr);
    }
    std::fclose(file);
    return true;
}

} // namespace opendigitizer::png

#endif // OPENDIGITIZER_UTILS_PNGHELPER_HPP
