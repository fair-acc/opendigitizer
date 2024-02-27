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
    return T{ string };
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
        if (std::ranges::any_of(std::vector<std::string>{ "0", "false", "False", "none" }, [value](auto a) { return a == std::string{ value }; })) {
            return false;
        } else if (std::ranges::any_of(std::vector<std::string>{ "1", "true", "True", "yes" }, [value](auto a) { return a == std::string{ value }; })) {
            return true;
        }
    }
    return defaultValue;
}

struct Settings {
    std::string  hostname{ "localhost" };
    unsigned int port{ 8080 };
    bool         disableHttps{ false };
    bool         checkCertificates{ true };

    Settings() {
        disableHttps      = getValueFromEnv("DIGITIZER_DISABLE_HTTPS", disableHttps);           // use http instead of https
        checkCertificates = getValueFromEnv("DIGITIZER_CHECK_CERTIFICATES", checkCertificates); // disable checking validity of certificates
        hostname          = getValueFromEnv("DIGITIZER_HOSTNAME", hostname);                    // hostname to set up or connect to
        port              = getValueFromEnv("DIGITIZER_PORT", port);                            //  port      ~     ~    ~    ~
#ifndef EMSCRIPTEN
        opencmw::client::RestClient::CHECK_CERTIFICATES = checkCertificates;
#endif
    }

    std::string serviceUrls() {
        return std::string(disableHttps ? "http" : "https") + "://" + hostname + ":" + std::to_string(port);
    }
};
} // namespace Digitizer

#endif // OPENDIGITIZER_SETTINGS_H
