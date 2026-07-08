#ifndef OPENDIGITIZER_UI_COMPONENTS_SEARCH_AND_FILTER_COMPONENTS_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SEARCH_AND_FILTER_COMPONENTS_HPP_

#include "SortFilterModel.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace DigitizerUi::components {

struct TagFilterRow {
    std::string pendingInput;

    [[nodiscard]] std::optional<std::string> draw(const char* strId, const char* label);
};

struct KeyValueFilterRow {
    struct Committed {
        std::string key;
        std::string value;
    };

    std::string pendingKey;
    std::string pendingValue;

    [[nodiscard]] std::optional<Committed> draw(const char* strId);
};

struct DateFilterRow {
    enum class Direction { Before, After };

    struct Committed {
        Direction                                          direction;
        std::chrono::time_point<std::chrono::system_clock> date;
    };

    Direction                                          pendingDirection = Direction::Before;
    std::chrono::time_point<std::chrono::system_clock> pendingDate      = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

    [[nodiscard]] std::optional<Committed> draw(const char* strId, const char* rowLabel);

    [[nodiscard]] static float rowWidth(const char* rowLabel);
};

[[nodiscard]] std::optional<bool> drawMatchAnyOrAllFiltersCombo(const char* strId, bool requiresAll);

struct SortOption {
    const char* label;
    bool        enabled = true;
};

[[nodiscard]] std::optional<std::size_t> drawSortByCombo(const char* strId, const char* rowLabel, std::span<const SortOption> options, std::size_t currentIndex);

struct SearchSortInput {
    enum class Event {
        none,
        wantsRelevanceSort,
        wantsDefaultSort,
    };

    std::string text;

    [[nodiscard]] Event draw(const char* strId, const char* hint, bool sortingBySearch);
};

[[nodiscard]] std::optional<std::size_t> drawFilterTags(const char* strId, std::span<const std::unique_ptr<SortFilterModelFilter>> filters, const std::function<std::string(const SortFilterModelFilter&)>& labelFor);

} // namespace DigitizerUi::components

#endif
