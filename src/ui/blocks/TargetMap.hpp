#ifndef OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP
#define OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP

#include <expected>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <PluginPaths.hpp>

namespace DigitizerUi {

struct TargetEntry {
    std::vector<std::string> blocks; // empty when allBlocks
    bool                     allBlocks = false;
    std::string              property;

    bool operator==(const TargetEntry&) const = default;
};

namespace detail {
[[nodiscard]] inline std::expected<TargetEntry, std::string> parseTargetEntry(std::string_view entryText) {
    if (entryText.empty()) {
        return std::unexpected(std::string("empty entry between ';'"));
    }
    const std::size_t colon = entryText.find(':');
    if (colon == std::string_view::npos || entryText.find(':', colon + 1UZ) != std::string_view::npos) {
        return std::unexpected(std::format("'{}': expected exactly one ':' between blocks and property", entryText));
    }
    TargetEntry entry;
    entry.property = Digitizer::trimWhitespace(entryText.substr(colon + 1UZ));
    if (entry.property.empty()) {
        return std::unexpected(std::format("'{}': empty property", entryText));
    }
    const std::string blocksText = Digitizer::trimWhitespace(entryText.substr(0UZ, colon));
    if (blocksText.empty()) {
        return std::unexpected(std::format("'{}': no block named", entryText));
    }
    if (blocksText == "*") {
        entry.allBlocks = true;
        return entry;
    }
    for (const auto nameRange : blocksText | std::views::split(',')) {
        std::string name = Digitizer::trimWhitespace(std::string_view(nameRange.begin(), nameRange.end()));
        if (name.empty() || name == "*") {
            return std::unexpected(std::format("'{}': {}", entryText, name.empty() ? "empty block name" : "'*' mixed with block names"));
        }
        entry.blocks.push_back(std::move(name));
    }
    return entry;
}
} // namespace detail

[[nodiscard]] inline std::expected<std::vector<TargetEntry>, std::string> parseTargetMap(std::string_view text) {
    std::vector<TargetEntry> entries;
    if (Digitizer::trimWhitespace(text).empty()) {
        return entries;
    }
    for (const auto entryRange : text | std::views::split(';')) {
        auto entry = detail::parseTargetEntry(Digitizer::trimWhitespace(std::string_view(entryRange.begin(), entryRange.end())));
        if (!entry) {
            return std::unexpected(std::move(entry.error()));
        }
        entries.push_back(std::move(*entry));
    }
    return entries;
}

} // namespace DigitizerUi

#endif
