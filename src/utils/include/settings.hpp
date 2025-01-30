#ifndef OPENDIGITIZER_SETTINGS_H
#define OPENDIGITIZER_SETTINGS_H

#include "RestClient.hpp"
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

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
    uint16_t    port{8080};
    bool        disableHttps{false};
    bool        checkCertificates{true};
    std::string wasmServeDir{""};

    Settings() {
        disableHttps      = getValueFromEnv("DIGITIZER_DISABLE_HTTPS", disableHttps);           // use http instead of https
        checkCertificates = getValueFromEnv("DIGITIZER_CHECK_CERTIFICATES", checkCertificates); // disable checking validity of certificates
        hostname          = getValueFromEnv("DIGITIZER_HOSTNAME", hostname);                    // hostname to set up or connect to
        port              = getValueFromEnv("DIGITIZER_PORT", port);                            // port
        wasmServeDir      = getValueFromEnv("DIGITIZER_WASM_SERVE_DIR", wasmServeDir);          // directory to serve wasm from
#ifndef EMSCRIPTEN
        opencmw::client::RestClient::CHECK_CERTIFICATES = checkCertificates;
#else
        auto        finalURLChar = static_cast<char*>(EM_ASM_PTR({
            var finalURL         = window.location.href;
            var lengthBytes      = lengthBytesUTF8(finalURL) + 1;
            var stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(finalURL, stringOnWasmHeap, lengthBytes);
            return stringOnWasmHeap;
               }));
        std::string finalURL{finalURLChar, strlen(finalURLChar)};
        EM_ASM({_free($0)}, finalURLChar);
        auto url     = opencmw::URI<STRICT>(finalURL);
        port         = url.port().value_or(port);
        hostname     = url.hostName().value_or(hostname);
        disableHttps = url.scheme() == "http";
#endif
    }

    opencmw::URI<>::UriFactory serviceUrl() { return opencmw::URI<>::UriFactory().scheme(disableHttps ? "http" : "https").hostName(hostname).port(port); }
};
} // namespace Digitizer

#endif // OPENDIGITIZER_SETTINGS_H
