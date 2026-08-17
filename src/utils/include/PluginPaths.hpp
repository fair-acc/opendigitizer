#ifndef OPENDIGITIZER_PLUGIN_PATHS_H
#define OPENDIGITIZER_PLUGIN_PATHS_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
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

inline std::vector<std::string> splitPluginPathList(std::string_view pathList) {
#ifdef _WIN32
    constexpr char kPathSeparator = ';';
#else
    constexpr char kPathSeparator = ':';
#endif

    std::vector<std::string> paths;
    std::size_t              offset = 0;

    while (offset <= pathList.size()) {
        const auto separator = pathList.find(kPathSeparator, offset);
        const auto tokenEnd  = separator == std::string_view::npos ? pathList.size() : separator;
        auto       token     = trimWhitespace(pathList.substr(offset, tokenEnd - offset));
        if (!token.empty()) {
            paths.push_back(std::filesystem::path(token).lexically_normal().string());
        }

        if (separator == std::string_view::npos) {
            break;
        }
        offset = separator + 1;
    }

    return paths;
}

inline std::vector<std::string> resolvePluginSearchPaths(std::span<const std::string> additionalPaths = {}) {
#if defined(__EMSCRIPTEN__)
    (void)additionalPaths;
    return {};
#else
    std::vector<std::string> resolvedPaths;

    auto appendPath = [&resolvedPaths](std::string_view path) {
        if (!path.empty()) {
            resolvedPaths.push_back(std::filesystem::path(path).lexically_normal().string());
        }
    };

    for (const auto& path : additionalPaths) {
        appendPath(path);
    }

    constexpr std::array<std::string_view, 3> kPluginPathEnvVars = {
        "OPENDIGITIZER_PLUGIN_PATHS",
        "GR_PLUGIN_PATH",
        "GNURADIO_PLUGIN_PATH",
    };
    for (const auto envVar : kPluginPathEnvVars) {
        if (const auto* value = std::getenv(std::string(envVar).c_str()); value != nullptr) {
            for (const auto& path : splitPluginPathList(value)) {
                appendPath(path);
            }
        }
    }

    appendPath((std::filesystem::current_path() / "plugins").string());

    constexpr std::array<std::string_view, 5> kDefaultPluginDirectories = {
        "/opt/gnuradio4/plugins",
        "/opt/gnuradio4/lib/gnuradio/plugins",
        "/opt/gnuradio/plugins",
        "/usr/local/lib/gnuradio/plugins",
        "/usr/lib/gnuradio/plugins",
    };
    for (const auto defaultDir : kDefaultPluginDirectories) {
        appendPath(defaultDir);
    }

    std::vector<std::string> uniquePaths;
    std::set<std::string>    seen;
    uniquePaths.reserve(resolvedPaths.size());

    for (const auto& path : resolvedPaths) {
        if (seen.emplace(path).second) {
            uniquePaths.push_back(path);
        }
    }

    return uniquePaths;
#endif
}

} // namespace Digitizer

#endif // OPENDIGITIZER_PLUGIN_PATHS_H
