#ifndef OPENDIGITIZER_UTILS_COMMON_H
#define OPENDIGITIZER_UTILS_COMMON_H

#include <concepts>
#include <type_traits>

template<std::signed_integral T>
auto cast_to_unsigned(T value) {
    assert(value >= 0);
    return static_cast<std::make_unsigned_t<T>>(value);
}

template<std::unsigned_integral T>
auto cast_to_signed(T value) {
    assert(static_cast<std::make_signed_t<T>>(value) >= 0);
    return static_cast<std::make_signed_t<T>>(value);
}

#endif
