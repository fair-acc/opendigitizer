#ifndef OPENDIGITIZER_UI_UNIQUENAMEREGISTRY_HPP
#define OPENDIGITIZER_UI_UNIQUENAMEREGISTRY_HPP

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "utils/TransparentStringHash.hpp"

namespace gr::od {

/// block names in use or handed out for a pending request; unnamed blocks are not tracked
struct UniqueNameRegistry {
    std::size_t                                                             lastUsedId = 0;
    std::unordered_set<std::string, TransparentStringHash, std::equal_to<>> usedNames;

    /// true when the name was not in use yet
    bool markBlockNameUsed(std::string_view nonUniquePrefix) {
        if (nonUniquePrefix.empty()) {
            return true;
        }
        return usedNames.emplace(nonUniquePrefix).second;
    }

    /// `nonUniquePrefix` when unused, else a variant with a " (N)" suffix; marked used
    [[nodiscard]] std::string ensureNameIsUnique(std::string_view nonUniquePrefix) {
        if (nonUniquePrefix.empty()) {
            return {};
        }
        std::string name(nonUniquePrefix);
        if (usedNames.contains(name)) {
            const std::string_view base = withoutNumericSuffix(nonUniquePrefix);
            name                        = std::string(base);
            while (usedNames.contains(name)) {
                // use format with parentheses instead of # in order to improve likelihood of differentiation from blocks' unique names
                name = std::format("{} ({})", base, lastUsedId++);
            }
        }
        markBlockNameUsed(name);
        return name;
    }

    /// strips a trailing " (NNN)" or "(NNN)"
    [[nodiscard]] static std::string_view withoutNumericSuffix(std::string_view name) {
        if (!name.ends_with(')')) {
            return name;
        }
        const std::size_t openingParenthesis = name.rfind('(');
        if (openingParenthesis == std::string_view::npos) {
            return name;
        }
        const std::string_view digits = name.substr(openingParenthesis + 1UZ, name.size() - openingParenthesis - 2UZ);
        if (digits.empty() || !std::ranges::all_of(digits, [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return name;
        }
        std::string_view prefix = name.substr(0UZ, openingParenthesis);
        if (prefix.ends_with(' ')) {
            prefix.remove_suffix(1UZ);
        }
        return prefix;
    }
};

} // namespace gr::od

#endif
