#ifndef OPENDIGITIZER_UTILS_TOLOWER_H
#define OPENDIGITIZER_UTILS_TOLOWER_H

#include <cctype>
#include <ranges>

namespace Digitizer::utils {
/// std::tolower but it does not exhibit undefined behavior when the char is not representable in an unsigned char
inline char safe_tolower(char character) { //
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

inline bool caseInsensitiveCompare(char lhs, char rhs) { return Digitizer::utils::safe_tolower(lhs) == Digitizer::utils::safe_tolower(rhs); }

inline auto caseInsensitiveSearch(const auto& haystack, const auto& needle) { return std::ranges::search(haystack, needle, caseInsensitiveCompare); }

inline auto caseInsensitiveContains(const auto& haystack, const auto& needle) {
    if (needle.empty()) {
        return true;
    }

    return !caseInsensitiveSearch(haystack, needle).empty();
}

} // namespace Digitizer::utils

#endif
