#ifndef OPENDIGITIZER_PLUGIN_PATHS_H
#define OPENDIGITIZER_PLUGIN_PATHS_H

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Digitizer {

inline std::string trimWhitespace(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        start++;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        end--;
    }

    return std::string(text.substr(start, end - start));
}

struct SearchPaths {
    std::vector<std::string> paths;

    inline static std::string normalisePath(std::string_view path) {
        auto trimmedPath = trimWhitespace(path);
        if (trimmedPath.empty() || trimmedPath.starts_with("http://") || trimmedPath.starts_with("https://")) {
            return trimmedPath;
        }

        return std::filesystem::path(trimmedPath).lexically_normal().string();
    }

    void append(std::string_view _path) {
        if (auto path = normalisePath(_path); !path.empty()) {
            paths.push_back(path);
        }
    }

    void appendList(std::string_view pathList, char separator) {
        for (const auto token : pathList | std::views::split(separator)) {
            append(std::string_view(token));
        }
    }
};

inline std::vector<std::string> resolvePluginSearchPaths(std::span<const std::string> additionalPaths = {}) {
#if defined(__EMSCRIPTEN__)
    (void)additionalPaths;
    return {};
#else
    SearchPaths searchPaths;

#ifdef _WIN32
    constexpr char kPluginPathSeparator = ';';
#else
    constexpr char kPluginPathSeparator = ':';
#endif

    for (const auto& path : additionalPaths) {
        searchPaths.append(path);
    }

    constexpr std::array<std::string_view, 3> kPluginPathEnvVars = {
        "OPENDIGITIZER_PLUGIN_PATHS",
        "GR_PLUGIN_PATH",
        "GNURADIO_PLUGIN_PATH",
    };
    for (const auto envVar : kPluginPathEnvVars) {
        if (const auto* value = std::getenv(std::string(envVar).c_str()); value != nullptr) {
            searchPaths.appendList(value, kPluginPathSeparator);
        }
    }

    const auto currentPath = std::filesystem::current_path();
    searchPaths.append((currentPath / "plugins").string());
    searchPaths.append((currentPath / "assets").string());

    // GR_ASSET_PATHS: semicolon-separated list of additional local paths or HTTP asset root URLs.
    if (const auto* value = std::getenv("GR_ASSET_PATHS"); value != nullptr) {
        searchPaths.appendList(value, ';');
    }

    constexpr std::array<std::string_view, 5> kDefaultPluginDirectories = {
        "/opt/gnuradio4/plugins",
        "/opt/gnuradio4/lib/gnuradio/plugins",
        "/opt/gnuradio/plugins",
        "/usr/local/lib/gnuradio/plugins",
        "/usr/lib/gnuradio/plugins",
    };
    for (const auto defaultDir : kDefaultPluginDirectories) {
        searchPaths.append(defaultDir);
    }

    std::vector<std::string> uniquePaths;
    std::set<std::string>    seen;
    uniquePaths.reserve(searchPaths.paths.size());

    for (const auto& path : searchPaths.paths) {
        if (seen.emplace(path).second) {
            uniquePaths.push_back(path);
        }
    }

    return uniquePaths;
#endif
}

} // namespace Digitizer

#endif // OPENDIGITIZER_PLUGIN_PATHS_H
