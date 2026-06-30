#ifndef MAPUTILS_H
#define MAPUTILS_H

inline void pretty_print_map(const gr::property_map& map, std::size_t maxLevel = -1UZ, std::size_t level = 0) {
    if (level == maxLevel) {
        return;
    }
    for (const auto& [k, v] : map) {
        std::print("{}{} -> ", std::string(4 * level, ' '), k);
        if (k.ends_with("documentation")) {
            std::println("...");
        } else if (k.ends_with("meta_information")) {
            std::println("...");
        } else {
            auto visitorFunction = gr::meta::overloaded(
                [level, maxLevel](const gr::property_map& sub) {
                    std::println();
                    pretty_print_map(sub, maxLevel, level + 1);
                },
                [level, maxLevel](const gr::Tensor<gr::pmt::Value>& sub) {
                    std::println("ARRAY");
                    std::size_t index = 0UZ;
                    for (const auto& val : sub) {
                        std::println("{}[{}] -> ", std::string(4 * level + 4, ' '), index++);
                        if (val.is_tensor()) {
                            std::println("{}{}", std::string(5 * level, ' '), val);
                        } else if (const auto optMap = val.get_if<gr::property_map>()) {
                            pretty_print_map(*optMap, maxLevel, level + 2);
                        }
                    }
                },
                []<std::convertible_to<std::string_view> T>(const T& val) {
                    auto newline = std::ranges::find(val, '\n');
                    std::print("'{}'", std::string(val.begin(), newline));
                    if (newline != val.cend()) {
                        std::println("... [+{}]", std::distance(newline, val.cend()));
                    } else {
                        std::println(".");
                    }
                },
                []<typename T>(const T& val) {
                    if constexpr (std::is_integral_v<T>) {
                        std::println("{}", val);
                    } else if constexpr (std::is_same_v<T, float>) {
                        std::println("{}", val);
                    } else if constexpr (std::is_same_v<T, double>) {
                        std::println("{}", val);
                    } else {
                        std::println("[unsup. type {}]", gr::meta::type_name<T>());
                    }
                });
            gr::pmt::ValueVisitor(visitorFunction).visit(v);
        }
    }
}

template<typename T, bool allow_conversion = false>
inline std::expected<T, gr::Error> getOptionalProperty(const gr::property_map& map, std::string_view propertyName) {
    auto it = map.find(propertyName);
    if (it == map.cend()) {
        return std::unexpected(gr::Error(std::format("Missing field {} in YAML object", propertyName)));
    }

    if constexpr (std::same_as<T, std::string>) {
        if (!it->second.is_string()) {
            return std::unexpected(gr::Error(std::format("Field {} has incorrect type; expected {}", propertyName, gr::meta::type_name<T>())));
        }
        return it->second.value_or(std::string{});
    } else if constexpr (std::same_as<T, std::size_t>) {
        if (const auto* p = it->second.get_if<gr::Size_t>()) {
            return static_cast<std::size_t>(*p);
        }
        if (auto converted = gr::pmt::convert_safely<gr::Size_t>(it->second); converted) {
            return static_cast<std::size_t>(*converted);
        }
    } else if constexpr (std::same_as<T, std::pmr::string>) {
        if (!it->second.is_string()) {
            return std::unexpected(gr::Error(std::format("Field {} has incorrect type; expected {}", propertyName, gr::meta::type_name<T>())));
        }
        return std::pmr::string(it->second.value_or(std::string_view{}));
    } else if constexpr (!allow_conversion) {
        if constexpr (requires(const gr::pmt::Value& value) { value.template get_if<T>(); }) {
            if (const auto p = it->second.get_if<T>()) {
                return *p;
            }
        }
    } else {
        if (auto converted = gr::pmt::convert_safely<T>(it->second); converted) {
            return *converted;
        }
    }

    return std::unexpected(gr::Error(std::format("Field {} has incorrect type; expected {}", propertyName, gr::meta::type_name<T>())));
}

template<typename T, bool allow_conversion = false>
inline std::expected<T, gr::Error> getOptionalProperty(const gr::property_map& map, std::string_view propertyName, auto... propertySubNames)
requires(sizeof...(propertySubNames) > 0)
{
    static_assert((std::is_convertible_v<decltype(propertySubNames), std::string_view> && ...));
    auto it = map.find(propertyName);
    if (it == map.cend()) {
        return std::unexpected(gr::Error(std::format("Missing field {} in YAML object", propertyName)));
    }

    auto value = it->second.get_if<gr::property_map>();
    if (!value) {
        return std::unexpected(gr::Error(std::format("Field {} in YAML object has an incorrect type (expected gr::property_map)", propertyName)));
    }

    return getOptionalProperty<T, allow_conversion>(*value, propertySubNames...);
}

template<typename T, typename... Keys>
T getProperty(const gr::property_map& data, const Keys&... keys) {
    static_assert((std::is_convertible_v<Keys, std::string_view> && ...));
    return getOptionalProperty<T>(data, keys...).value_or(T{});
}

template<typename FieldType, typename... Keys>
void updateFieldFrom(FieldType& field, const auto& data, const FieldType& defaultValue, const Keys&... keys) { //
    field = getOptionalProperty<FieldType>(data, keys...).value_or(defaultValue);
}

#endif
