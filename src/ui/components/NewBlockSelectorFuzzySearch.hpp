#ifndef OPENDIGITIZER_COMPONENTS_NEW_BLOCK_SELECTOR_FUZZY_SEARCH_HPP
#define OPENDIGITIZER_COMPONENTS_NEW_BLOCK_SELECTOR_FUZZY_SEARCH_HPP

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace DigitizerUi::components {

std::size_t wordDistance(std::string_view a, std::string_view b);

struct FilterResult {
    std::vector<std::string_view> exactMatches;
    double                        score = 0.0;

    void addExactMatchesInString(std::string_view string, std::string_view filter);
};

struct NamespaceDelimitedSearchIterator {
    std::string_view contents;

    constexpr std::optional<std::string_view> next() {
        while (!contents.empty() && (contents.front() == ':' || contents.front() == ' ')) {
            contents = contents.substr(1);
        }

        std::optional<std::string_view> result;
        if (!contents.empty()) {
            const std::size_t maybeNextIndex = std::min(contents.find(':'), contents.find(' '));
            if (maybeNextIndex >= contents.size()) {
                return std::exchange(contents, {});
            }

            result   = contents.substr(0, maybeNextIndex);
            contents = contents.substr(maybeNextIndex);
        }
        return result;
    }
};

[[nodiscard]] FilterResult filterTypename(std::string_view typenameString, std::string_view filter);

} // namespace DigitizerUi::components

#endif
