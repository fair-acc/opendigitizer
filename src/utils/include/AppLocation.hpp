#ifndef OPENDIGITIZER_APP_LOCATION_HPP
#define OPENDIGITIZER_APP_LOCATION_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Digitizer {

/// Route-style application state carried in the URL fragment, e.g. `#/dashboard/My%20Board?darkMode&editable=true`.
/// Never throws: malformed percent escapes are kept literally.
struct AppLocation {
    enum class Page : std::uint8_t { Chart, Flowgraph, Dashboards };

    static constexpr std::string_view kDashboardSegment  = "dashboard";
    static constexpr std::string_view kFlowgraphSegment  = "flowgraph";
    static constexpr std::string_view kDashboardsSegment = "dashboards";

    std::vector<std::string>                        segments; // decoded path segments, never empty strings
    std::map<std::string, std::string, std::less<>> query;    // decoded; a bare key has an empty value

    [[nodiscard]] static AppLocation parse(std::string_view hashBody) {
        if (hashBody.starts_with('#')) {
            hashBody.remove_prefix(1);
        }
        const std::size_t queryStart = hashBody.find('?');
        AppLocation       location;
        for (const std::string_view segment : split(hashBody.substr(0, queryStart), '/')) {
            location.segments.push_back(percentDecode(segment));
        }
        if (queryStart != std::string_view::npos) {
            for (const std::string_view pair : split(hashBody.substr(queryStart + 1), '&')) {
                const std::size_t equals = pair.find('=');
                location.query.insert_or_assign(percentDecode(pair.substr(0, equals)), equals == std::string_view::npos ? std::string{} : percentDecode(pair.substr(equals + 1)));
            }
        }
        return location;
    }

    /// canonical form without the leading '#'
    [[nodiscard]] std::string toHash() const {
        std::string hash;
        for (const std::string& segment : segments) {
            hash += '/' + percentEncode(segment);
        }
        if (hash.empty()) {
            hash = "/";
        }
        char separator = '?';
        for (const auto& [key, value] : query) {
            hash += separator + percentEncode(key);
            if (!value.empty()) {
                hash += '=' + percentEncode(value);
            }
            separator = '&';
        }
        return hash;
    }

    /// a name that is safe to append to a dashboards path: no separators, query or fragment delimiters, control characters, or dot names
    [[nodiscard]] static bool isValidDashboardName(std::string_view name) {
        constexpr auto isForbidden = [](char c) { return c == '/' || c == '\\' || c == '?' || c == '#' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F; };
        return !name.empty() && name != "." && name != ".." && std::ranges::none_of(name, isForbidden);
    }

    /// only names passing isValidDashboardName: the URL is untrusted input, a name like `https://host/x` must not select a remote file
    [[nodiscard]] std::optional<std::string> dashboardName() const {
        const bool isChart     = segments.size() == 2;
        const bool isFlowgraph = segments.size() == 3 && segments[2] == kFlowgraphSegment;
        if ((isChart || isFlowgraph) && segments[0] == kDashboardSegment && isValidDashboardName(segments[1])) {
            return segments[1];
        }
        return std::nullopt;
    }

    /// `/dashboard/<name>` is the chart view, `/dashboard/<name>/flowgraph` the editor, `/dashboards` the open/save page; anything else has no page
    [[nodiscard]] std::optional<Page> page() const {
        if (segments.size() == 1 && segments[0] == kDashboardsSegment) {
            return Page::Dashboards;
        }
        if (!dashboardName()) {
            return std::nullopt;
        }
        return segments.size() == 2 ? Page::Chart : Page::Flowgraph;
    }

    /// bare key, `key=true` and `key=1` are true; `key=false` and `key=0` are false; anything else is absent
    [[nodiscard]] std::optional<bool> flag(std::string_view key) const {
        const auto it = query.find(key);
        if (it == query.end()) {
            return std::nullopt;
        }
        if (it->second.empty() || it->second == "true" || it->second == "1") {
            return true;
        }
        if (it->second == "false" || it->second == "0") {
            return false;
        }
        return std::nullopt;
    }

    /// `page` selects the chart view or the editor; Page::Dashboards has no dashboard name, use forDashboardList()
    [[nodiscard]] static AppLocation forDashboard(std::string_view name, std::map<std::string, std::string, std::less<>> query = {}, Page page = Page::Chart) {
        AppLocation location{.segments = {std::string(kDashboardSegment), std::string(name)}, .query = std::move(query)};
        if (page == Page::Flowgraph) {
            location.segments.emplace_back(kFlowgraphSegment);
        }
        return location;
    }

    [[nodiscard]] static AppLocation forDashboardList(std::map<std::string, std::string, std::less<>> query = {}) { return {.segments = {std::string(kDashboardsSegment)}, .query = std::move(query)}; }

    [[nodiscard]] bool operator==(const AppLocation&) const = default;

private:
    static std::vector<std::string_view> split(std::string_view text, char separator) {
        std::vector<std::string_view> parts;
        while (!text.empty()) {
            const std::size_t end = std::min(text.find(separator), text.size());
            if (end > 0) {
                parts.push_back(text.substr(0, end));
            }
            text.remove_prefix(std::min(end + 1, text.size()));
        }
        return parts;
    }

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    static std::string percentDecode(std::string_view text) {
        std::string decoded;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const int high = text[i] == '%' && i + 2 < text.size() ? hexValue(text[i + 1]) : -1;
            const int low  = high >= 0 ? hexValue(text[i + 2]) : -1;
            if (low >= 0) {
                decoded += static_cast<char>(high * 16 + low);
                i += 2;
            } else {
                decoded += text[i];
            }
        }
        return decoded;
    }

    static std::string percentEncode(std::string_view text) {
        constexpr std::string_view kHex = "0123456789ABCDEF";
        std::string                encoded;
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            if ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '-' || byte == '.' || byte == '_' || byte == '~') {
                encoded += c;
            } else {
                encoded += '%';
                encoded += kHex[byte >> 4];
                encoded += kHex[byte & 0x0F];
            }
        }
        return encoded;
    }
};

} // namespace Digitizer

#endif // OPENDIGITIZER_APP_LOCATION_HPP
