#ifndef YAMLUTILS_H
#define YAMLUTILS_H

#include <yaml-cpp/yaml.h>

template<typename T>
inline auto
toYamlString(const T &value) {
    if constexpr (std::is_same_v<std::string, std::remove_cvref_t<T>>) {
        return value;
    } else if constexpr (std::is_same_v<bool, std::remove_cvref_t<T>>) {
        return value ? "true" : "false";
    } else if constexpr (requires { std::to_string(value); }) {
        return std::to_string(value);
    } else {
        return "";
    }
}

struct YamlSeq {
    YamlSeq(YAML::Emitter &out)
        : out(out) {
        out << YAML::BeginSeq;
    }

    ~YamlSeq() {
        out << YAML::EndSeq;
    }

    template<typename F>
        requires std::is_invocable_v<F>
    void write(const char *key, F &&fun) {
        fun();
    }

    void           write(const std::string &key, auto &&val) { write(key.c_str(), std::forward<decltype(val)>(val)); }

    YAML::Emitter &out;
};

struct YamlMap {
    YamlMap(YAML::Emitter &out)
        : out(out) {
        out << YAML::BeginMap;
    }

    ~YamlMap() {
        out << YAML::EndMap;
    }

    template<typename T>
    void
    write(const std::string_view &key, const std::vector<T> &value) {
        out << YAML::Key << key.data();
        YamlSeq seq(out);
        for (const auto &elem : value) out << YAML::Value << toYamlString(elem);
    }

    template<typename T>
    void
    write(const std::string_view &key, const T &value) {
        out << YAML::Key << key.data();
        out << YAML::Value << toYamlString(value);
    }

    template<typename F>
        requires std::is_invocable_v<F>
    void write(const char *key, F &&fun) {
        out << YAML::Key << key;
        out << YAML::Value;
        fun();
    }

    YAML::Emitter &out;
};

#endif
