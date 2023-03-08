#ifndef YAMLUTILS_H
#define YAMLUTILS_H

#include <yaml-cpp/yaml.h>

struct YamlMap {
    YamlMap(YAML::Emitter &out)
        : out(out) {
        out << YAML::BeginMap;
    }

    ~YamlMap() {
        out << YAML::EndMap;
    }

    template<typename T>
        requires(!std::is_invocable_v<T>)
    void write(const char *key, T &&value) {
        out << YAML::Key << key;
        out << YAML::Value << value;
    }

    template<typename F>
        requires std::is_invocable_v<F>
    void write(const char *key, F &&fun) {
        out << YAML::Key << key;
        out << YAML::Value;
        fun();
    }

    void           write(const std::string &key, auto &&val) { write(key.c_str(), std::forward<decltype(val)>(val)); }

    YAML::Emitter &out;
};

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

#endif
