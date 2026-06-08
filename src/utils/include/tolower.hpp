#ifndef OPENDIGITIZER_UTILS_TOLOWER_H
#define OPENDIGITIZER_UTILS_TOLOWER_H

#include <cctype>

namespace Digitizer::utils {
/// std::tolower but it does not exhibit undefined behavior when the char is not representable in an unsigned char
constexpr char safe_tolower(char character) { //
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}
} // namespace Digitizer::utils

#endif
