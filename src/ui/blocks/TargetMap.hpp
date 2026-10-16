#ifndef OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP
#define OPENDIGITIZER_UI_BLOCKS_TARGETMAP_HPP

#include <algorithm>
#include <array>
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
        return std::unexpected("empty entry between ';'");
    }
    const std::size_t colon = entryText.rfind(':');
    if (colon == std::string_view::npos) {
        return std::unexpected(std::format("'{}': expected a ':' between blocks and property", entryText));
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

/// If these units are used on a std::uint32_t, the editor will display as a color picker
inline constexpr std::array<std::string_view, 2> kColorUnits{"Color", "RGB"};

[[nodiscard]] inline bool isColorUnit(std::string_view unit) { return std::ranges::contains(kColorUnits, unit); }

/// Parse a string representing a list of types and units, of the form "float64;float32;unit:Hz;unit:kHz", and return
/// whether typeName is one of the listed types and unit is one of the listed units. If either types or units are
/// missing, it is interpreted as allowing any type or unit, respectively.
[[nodiscard]] inline bool isCompatibleType(std::string_view compatibleTypeMap, std::string_view typeName, std::string_view unit = {}) {
    constexpr std::string_view kUnitPrefix = "unit:";

    std::vector<std::string> types;
    std::vector<std::string> units;
    for (const auto entryRange : compatibleTypeMap | std::views::split(';')) {
        std::string entry = Digitizer::trimWhitespace(std::string_view(entryRange.begin(), entryRange.end()));
        if (entry.starts_with(kUnitPrefix)) {
            units.push_back(Digitizer::trimWhitespace(std::string_view(entry).substr(kUnitPrefix.size())));
        } else if (!entry.empty()) {
            types.push_back(std::move(entry));
        }
    }

    const bool typeMatches = types.empty() || std::ranges::any_of(types, [typeName](const std::string& type) { return type == "*" || type == typeName; });
    const bool unitMatches = units.empty() || std::ranges::contains(units, unit);
    return typeMatches && unitMatches;
}

} // namespace DigitizerUi

#endif
