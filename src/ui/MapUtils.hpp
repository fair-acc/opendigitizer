#ifndef MAPUTILS_H
#define MAPUTILS_H

#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>

inline std::string valueToString(const gr::pmt::Value& val) {
    std::string out;
    gr::pmt::ValueVisitor([&]<typename TArg>(const TArg& arg) {
        using T = std::decay_t<TArg>;
        if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string>) {
            out = std::string(arg);
        } else if constexpr (std::same_as<T, bool>) {
            out = arg ? "true" : "false";
        } else if constexpr (std::integral<T> || std::floating_point<T>) {
            out = std::to_string(arg);
        } else {
            out.clear();
        }
    }).visit(val);
    return out;
}

inline void printMap(const gr::property_map& map, std::size_t maxLevel = -1UZ, std::size_t level = 0) {
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
                    printMap(sub, maxLevel, level + 1);
                },
                [level, maxLevel](const gr::Tensor<gr::pmt::Value>& sub) {
                    std::println("ARRAY");
                    std::size_t index = 0UZ;
                    for (const auto& val : sub) {
                        std::println("{}[{}] -> ", std::string(4 * level + 4, ' '), index++);
                        if (val.is_tensor()) {
                            std::println("{}{}", std::string(5 * level, ' '), val);
                        } else if (const auto optMap = val.get_if<gr::property_map>()) {
                            printMap(*optMap, maxLevel, level + 2);
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

template<typename FieldType, typename... Keys>
void updateFieldFrom(FieldType& field, const auto& data, const FieldType& defaultValue, const Keys&... keys) { //
    field = gr::getProperty<FieldType>(data, keys...).value_or(defaultValue);
}

#endif
