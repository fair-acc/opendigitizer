#ifndef OPENDIGITIZER_META_H
#define OPENDIGITIZER_META_H

namespace opendigitizer::meta {

template<typename T>
struct is_dataset {
    constexpr inline static bool value = false;
};

template<typename T>
struct is_dataset<gr::DataSet<T>> {
    constexpr inline static bool value = true;
};

template<typename T>
constexpr inline bool is_dataset_v = is_dataset<T>::value;

} // namespace opendigitizer::meta

#endif

