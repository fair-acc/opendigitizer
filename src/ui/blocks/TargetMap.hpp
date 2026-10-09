#ifndef OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP
#define OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP

#include <algorithm>
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <PluginPaths.hpp>

namespace DigitizerUi {

enum class TargetRelationship { NotTargeting, TargetingSpecifically, TargetingViaGlob };

/// An entry in target_map for a UI control
struct TargetEntry {
    std::string blockTarget;
    std::string propertyName;

    [[nodiscard]] bool isGlob() const { return blockTarget.empty() || blockTarget == "*"; }

    [[nodiscard]] TargetRelationship relationshipToTarget(std::string_view specificBlockName, std::string_view blockPropertyName) const {
        if (propertyName != blockPropertyName) {
            return TargetRelationship::NotTargeting;
        }
        if (isGlob()) {
            return TargetRelationship::TargetingViaGlob;
        }
        return blockTarget == specificBlockName ? TargetRelationship::TargetingSpecifically : TargetRelationship::NotTargeting;
    }

    [[nodiscard]] bool operator==(const TargetEntry& other) const { //
        return propertyName == other.propertyName && (blockTarget == other.blockTarget || (isGlob() && other.isGlob()));
    }
};

namespace detail {
[[nodiscard]] inline std::expected<std::vector<TargetEntry>, std::string> parseTargetEntries(std::string_view entryText) {
    if (entryText.empty()) {
        return std::unexpected(std::string("empty entry between ';'"));
    }
    const std::size_t colon = entryText.find(':');
    if (colon == std::string_view::npos || entryText.find(':', colon + 1UZ) != std::string_view::npos) {
        return std::unexpected(std::format("'{}': expected exactly one ':' between blocks and property", entryText));
    }
    const std::string property = Digitizer::trimWhitespace(entryText.substr(colon + 1UZ));
    if (property.empty()) {
        return std::unexpected(std::format("'{}': empty property", entryText));
    }
    const std::string blocksText = Digitizer::trimWhitespace(entryText.substr(0UZ, colon));
    if (blocksText.empty()) {
        return std::unexpected(std::format("'{}': no block named", entryText));
    }
    if (blocksText == "*") {
        return std::vector{TargetEntry{.blockTarget = blocksText, .propertyName = property}};
    }
    std::vector<TargetEntry> entries;
    for (const auto nameRange : blocksText | std::views::split(',')) {
        std::string name = Digitizer::trimWhitespace(std::string_view(nameRange.begin(), nameRange.end()));
        if (name.empty() || name == "*") {
            return std::unexpected(std::format("'{}': {}", entryText, name.empty() ? "empty block name" : "'*' mixed with block names"));
        }
        entries.push_back(TargetEntry{.blockTarget = std::move(name), .propertyName = property});
    }
    return entries;
}
} // namespace detail

/// A parsed version of target_map which supports finding the strongest connection, and adding and removing items with deduplication
class TargetMap {
    std::vector<TargetEntry> _entries;

public:
    [[nodiscard]] static std::expected<TargetMap, std::string> fromString(std::string_view stringRepresentation) {
        TargetMap map;
        if (Digitizer::trimWhitespace(stringRepresentation).empty()) {
            return map;
        }
        for (const auto entryRange : stringRepresentation | std::views::split(';')) {
            auto entries = detail::parseTargetEntries(Digitizer::trimWhitespace(std::string_view(entryRange.begin(), entryRange.end())));
            if (!entries) {
                return std::unexpected(std::move(entries.error()));
            }
            for (TargetEntry& entry : *entries) {
                map.addTarget(std::move(entry));
            }
        }
        return map;
    }

    [[nodiscard]] std::string toString() const {
        // deduplicate TargetEntries which are targeting the same property of different blocks
        std::vector<std::pair<std::string /* comma separated list of block names */, std::string /* property */>> groups;
        for (const TargetEntry& entry : _entries) {
            const auto sameProperty = std::ranges::find_if(groups, [&entry](const auto& group) { return group.second == entry.propertyName && group.first != "*"; });
            if (entry.isGlob() || sameProperty == groups.end()) {
                groups.emplace_back(entry.isGlob() ? "*" : entry.blockTarget, entry.propertyName);
            } else {
                sameProperty->first += ',' + entry.blockTarget;
            }
        }
        return groups | std::views::transform([](const auto& group) { return std::format("{}:{}", group.first, group.second); }) | std::views::join_with(';') | std::ranges::to<std::string>();
    }

    [[nodiscard]] std::span<const TargetEntry> entries() const { return _entries; }

    [[nodiscard]] bool contains(const TargetEntry& maybeContained) const { return std::ranges::contains(_entries, maybeContained); }

    /// If any of our target_map entries are specifically targeting the given block, return that relationship. if none, then NotTargeting. otherwise TargetingViaGlob
    [[nodiscard]] TargetRelationship relationshipToTarget(std::string_view specificBlockName, std::string_view blockPropertyName) const {
        TargetRelationship strongestRelationship = TargetRelationship::NotTargeting;
        for (const TargetEntry& entry : _entries) {
            switch (entry.relationshipToTarget(specificBlockName, blockPropertyName)) {
            case TargetRelationship::TargetingSpecifically: return TargetRelationship::TargetingSpecifically;
            case TargetRelationship::TargetingViaGlob: strongestRelationship = TargetRelationship::TargetingViaGlob; break;
            case TargetRelationship::NotTargeting: break;
            }
        }
        return strongestRelationship;
    }

    void addTarget(TargetEntry entry) {
        if (!contains(entry)) {
            _entries.push_back(std::move(entry));
        }
    }

    void removeTarget(const TargetEntry& maybeContained) { std::erase(_entries, maybeContained); }
};

} // namespace DigitizerUi

#endif
