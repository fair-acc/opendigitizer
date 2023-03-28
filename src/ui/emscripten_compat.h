#ifndef EMSCRIPTEN_COMPAT_H
#define EMSCRIPTEN_COMPAT_H

// These functions are missing from emscripten's libcpp but are used by plf_colony.h
// so implement them here for now

namespace std {
namespace ranges {
auto distance(auto r) {
    return std::distance(std::begin(r), std::end(r));
}
} // namespace ranges

template<class I1, class I2, class Cmp>
constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2, Cmp comp)
        -> decltype(comp(*f1, *f2)) {
    bool exhaust1 = (f1 == l1);
    bool exhaust2 = (f2 == l2);
    for (; !exhaust1 && !exhaust2; exhaust1 = (++f1 == l1), exhaust2 = (++f2 == l2))
        if (auto c = comp(*f1, *f2); c != 0)
            return c;

    return !exhaust1 ? std::strong_ordering::greater : !exhaust2 ? std::strong_ordering::less
                                                                 : std::strong_ordering::equal;
}

template<class I1, class I2>
constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2) {
    return std::lexicographical_compare_three_way(f1, l1, f2, l2, std::compare_three_way());
}
} // namespace std

#endif
