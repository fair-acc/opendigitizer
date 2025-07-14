#ifndef OPENDIGITIZER_SETTINGS_H
#define OPENDIGITIZER_SETTINGS_H

#include <URI.hpp>

#include <algorithm>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

namespace Digitizer {

namespace detail {

template<typename T>
inline auto toType(std::string string) {
    return T{string};
}
template<typename T>
requires std::is_arithmetic_v<T>
inline T toType(std::string string) {
    return static_cast<T>(std::stod(string.c_str()));
}
} // namespace detail

template<typename T>
inline T getValueFromEnv(std::string variableName, T defaultValue) {
    if (auto valueString = std::getenv(variableName.c_str()); valueString) {
        return detail::toType<T>(valueString);
    } else {
        return defaultValue;
    }
}

template<>
inline bool getValueFromEnv(std::string variableName, bool defaultValue) {
    if (auto value = std::getenv(variableName.c_str()); value) {
        if (std::ranges::any_of(std::vector<std::string>{"0", "false", "False", "none"}, [value](auto a) { return a == std::string{value}; })) {
            return false;
        } else if (std::ranges::any_of(std::vector<std::string>{"1", "true", "True", "yes"}, [value](auto a) { return a == std::string{value}; })) {
            return true;
        }
    }
    return defaultValue;
}

struct Settings {
    std::string hostname{"localhost"};
    uint16_t    port{8443};
    uint16_t    portPlain{8080};
    std::string basePath;
    bool        disableHttps{false};
    bool        checkCertificates{true};
    bool        darkMode{false};
#ifdef EMSCRIPTEN
    bool editableMode{false};
#else
    bool editableMode{true}; // running natively
#endif
    std::string wasmServeDir{""};
    std::string defaultDashboard{"RemoteStream"};
    std::string remoteDashboards{"../dashboard/defaultDashboards"};

private:
    Settings() {
        using std::operator""sv;
        disableHttps      = getValueFromEnv("DIGITIZER_DISABLE_HTTPS", disableHttps);           // use http instead of https
        darkMode          = getValueFromEnv("DIGITIZER_DARK_MODE", darkMode);                   // enable 'dark mode'
        editableMode      = getValueFromEnv("DIGITIZER_EDIT_MODE", editableMode);               // enable 'editable mode'
        checkCertificates = getValueFromEnv("DIGITIZER_CHECK_CERTIFICATES", checkCertificates); // disable checking validity of certificates
        hostname          = getValueFromEnv("DIGITIZER_HOSTNAME", hostname);                    // hostname to set up or connect to
        port              = getValueFromEnv("DIGITIZER_PORT", port);                            // port
        portPlain         = getValueFromEnv("DIGITIZER_PORT_PLAIN", portPlain);                 // port for http
        basePath          = getValueFromEnv("DIGITIZER_PATH", basePath);                        // path
        wasmServeDir      = getValueFromEnv("DIGITIZER_WASM_SERVE_DIR", wasmServeDir);          // directory to serve wasm from
        defaultDashboard  = getValueFromEnv("DIGITIZER_DEFAULT_DASHBOARD", defaultDashboard);   // Default dashboard to load from the service
        remoteDashboards  = getValueFromEnv("DIGITIZER_REMOTE_DASHBOARDS", remoteDashboards);   // Directory the dashboard worker loads the dashboards from
#ifdef EMSCRIPTEN
        auto        finalURLChar = static_cast<char*>(EM_ASM_PTR({
            var finalURL         = window.location.href;
            var lengthBytes      = lengthBytesUTF8(finalURL) + 1;
            var stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(finalURL, stringOnWasmHeap, lengthBytes);
            return stringOnWasmHeap;
               }));
        std::string finalURL{finalURLChar, strlen(finalURLChar)};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
        EM_ASM({_free($0)}, finalURLChar);
#pragma GCC diagnostic push
        auto url = opencmw::URI<opencmw::STRICT>(finalURL);
        if (url.port().has_value()) {
            port = url.port().value();
        } else {
            if (url.scheme().has_value()) {
                if (url.scheme().value() == "https") {
                    port = 443;
                } else if (url.scheme().value() == "http") {
                    port = 80;
                }
            }
        }
        hostname                 = url.hostName().value_or(hostname);
        basePath                 = url.path().value_or(basePath);
        auto extractPrefixBefore = [](std::string_view uri, std::string_view trigger) -> std::string_view {
            if (!uri.ends_with(trigger) || uri.size() <= trigger.size()) {
                return {};
            }

            auto prefix = uri.substr(0, uri.size() - trigger.size());
            if (prefix.starts_with('/')) {
                prefix.remove_prefix(1);
            }
            return prefix;
        };
        basePath = extractPrefixBefore(basePath, "web/index.html"); // TODO: temporary fix to be compatible with proxy forwarding like: https://my.proxy.com/prefix/web/index.html -> https://my.localdomain.com//web/index.html'. All paths (incl. dashboard need to be relative to the 'prefix' path.

        auto fragment = url.fragment().value_or("");
        for (auto param : std::ranges::split_view(fragment, "&"sv)) {
            auto sv = std::string_view(param.begin(), param.end());
            if (sv.starts_with("dashboard=")) {
                defaultDashboard = sv.substr("dashboard="sv.length());
            } else if (sv.starts_with("darkMode=")) {
                darkMode = sv.substr("darkMode="sv.length()) == "true"sv;
            } else if (sv.starts_with("darkMode")) {
                darkMode = true;
            } else if (sv.starts_with("editable")) {
                editableMode = true;
            }
        }
        disableHttps = url.scheme() == "http";
#endif
        std::println("settings loaded: disableHttps={}, darkMode={}, editable={}, checkCertificates={}, hostname={}, port={}, portPlain={}, basePath='{}', wasmServeDir={}, defaultDashboard={}, remoteDashboards={}", //
            disableHttps, darkMode, editableMode, checkCertificates, hostname, port, portPlain, basePath, wasmServeDir, defaultDashboard, remoteDashboards);
    }

public:
    static Settings& instance() {
        static Settings settings;
        return settings;
    }

    opencmw::URI<>::UriFactory serviceUrl() { return opencmw::URI<>::UriFactory().scheme(disableHttps ? "http" : "https").hostName(hostname).port(port); }
    opencmw::URI<>::UriFactory serviceUrlPlain() { return opencmw::URI<>::UriFactory().scheme("http").hostName(hostname).port(portPlain); }
};
} // namespace Digitizer

#endif // OPENDIGITIZER_SETTINGS_H
