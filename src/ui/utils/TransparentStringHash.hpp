#ifndef OPENDIGITIZER_UTILS_TRANSPARENT_STRING_HASH_HPP
#define OPENDIGITIZER_UTILS_TRANSPARENT_STRING_HASH_HPP

#include <string>

namespace opendigitizer {
struct TransparentStringHash // why isn't std::hash<std::string> this exact same thing?
{
    using hash_type      = std::hash<std::string_view>;
    using is_transparent = void;

    size_t operator()(const char* str) const { return hash_type{}(str); }
    size_t operator()(std::string_view str) const { return hash_type{}(str); }
    size_t operator()(std::string const& str) const { return hash_type{}(str); }
};
} // namespace opendigitizer

#endif
