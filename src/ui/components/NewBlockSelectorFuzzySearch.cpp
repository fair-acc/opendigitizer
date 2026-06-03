#include "NewBlockSelectorFuzzySearch.hpp"

#include "tolower.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ranges>

namespace {
bool compareChars(char a, char b) { return Digitizer::utils::safe_tolower(a) == Digitizer::utils::safe_tolower(b); }
} // namespace

namespace DigitizerUi::components {
std::size_t wordDistance(std::string_view a, std::string_view b) {
    if (a.size() == 0) {
        return b.size();
    }
    if (b.size() == 0) {
        return a.size();
    }

    std::vector<std::size_t> prev(b.size() + 1);
    std::vector<std::size_t> curr(b.size() + 1);

    for (std::size_t x = 0; x <= b.size(); ++x) {
        prev[x] = x;
    }

    for (std::size_t y = 1; y <= a.size(); ++y) {
        curr[0] = y;
        for (std::size_t x = 1; x <= b.size(); ++x) {
            const std::size_t cost     = (compareChars(b[x - 1], a[y - 1])) ? 0 : 1;
            const std::size_t above    = prev[x] + 1;
            const std::size_t left     = curr[x - 1] + 1;
            const std::size_t diagonal = prev[x - 1];
            curr[x]                    = std::min(std::min(above, left), diagonal + cost);
        }
        std::swap(prev, curr);
    }

    return prev[b.size()];
}

void FilterResult::addExactMatchesInString(std::string_view string, std::string_view filter) {
    std::string_view substring = string;
    while (!substring.empty()) {
        const auto tolower = [](char c) { return Digitizer::utils::safe_tolower(c); };
        if (auto match = std::ranges::search(substring, filter, std::ranges::equal_to{}, tolower, tolower); !match.empty()) {
            assert(match.data() >= substring.data() && match.data() < substring.data() + substring.size());

            exactMatches.emplace_back(match);
            const auto matchStart = match.data() - substring.data();
            assert(matchStart >= 0);
            substring = substring.substr(static_cast<std::size_t>(matchStart) + match.size());
        } else {
            break;
        }
    }
}

FilterResult filterTypename(std::string_view typenameString, std::string_view filter) {
    FilterResult result{};
    if (filter.empty()) {
        return result;
    }

    double bestDistanceBasedScore = 0;

    NamespaceDelimitedSearchIterator typenameIterator{typenameString};
    while (auto typenameSubString = typenameIterator.next()) {
        NamespaceDelimitedSearchIterator filterIterator{filter};
        while (auto filterSubString = filterIterator.next()) {
            result.addExactMatchesInString(*typenameSubString, *filterSubString);

            const auto distance           = wordDistance(*typenameSubString, *filterSubString);
            const auto guaranteedDistance = static_cast<std::size_t>(std::abs(static_cast<std::int64_t>(typenameSubString->size()) - static_cast<std::int64_t>(filterSubString->size())));
            assert(distance >= guaranteedDistance);

            const auto numReplacements = static_cast<double>(distance - guaranteedDistance);
            const auto inverseWeight   = static_cast<double>(std::min(filterSubString->size(), typenameSubString->size()));
            const auto scaledDistance  = numReplacements / inverseWeight;
            assert(numReplacements <= inverseWeight);
            assert(!filterSubString->empty() && !typenameSubString->empty());
            bestDistanceBasedScore = std::max(bestDistanceBasedScore, 1.0 - scaledDistance);
        }
    }

    result.score = bestDistanceBasedScore + static_cast<double>(result.exactMatches.size());

    return result;
}

} // namespace DigitizerUi::components
