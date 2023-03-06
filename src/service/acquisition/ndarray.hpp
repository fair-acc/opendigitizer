#ifndef OPENDIGITIZER_SERVICE_NDARRAY
#define OPENDIGITIZER_SERVICE_NDARRAY
#include <array>
#include <concepts>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

template<std::integral index_type, std::size_t... exts>
struct extents {
    using idx_type = index_type;
};

static constexpr std::size_t dynamic_extent = std::numeric_limits<size_t>::max();
static constexpr std::size_t multi_extent   = std::numeric_limits<size_t>::max() - 1;

template<typename T, class ext /*layout & accessor omitted for now*/>
struct ndarray {
    std::vector<T>           data;
    std::vector<std::size_t> exts;
    // special cases for static/dynamic(->use array) only extents

    ndarray(std::vector<T> data, std::vector<std::size_t> exts)
        : data{ std::move(data) }, exts{ std::move(exts) } {};
};

void example() {
    auto arr = ndarray<double, extents<int, multi_extent>>({ 1, 2, 3, 4, 5, 6 }, { 2, 3 });
}
#endif // OPENDIGITIZER_SERVICE_NDARRAY
