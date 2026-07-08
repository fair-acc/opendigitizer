#include "SortFilterModel.hpp"

#include <algorithm>
#include <cassert>

namespace DigitizerUi::components {

namespace {

constexpr std::size_t kDefaultMaxWorkPerCall = 2048UZ;

const SortFilterModelComparator* comparatorOf(const SortFilterModelSortStrategy& strategy) {
    const auto* comparisonOperator = std::get_if<std::unique_ptr<SortFilterModelComparator>>(&strategy);
    return comparisonOperator != nullptr ? comparisonOperator->get() : nullptr;
}

} // namespace

bool ModelFilters::matches(std::size_t userItemIndex) const {
    if (requiredFilter && !requiredFilter(userItemIndex)) {
        return false;
    }
    if (filterObjects.empty()) {
        return true;
    }
    const auto matches = [userItemIndex](const std::unique_ptr<SortFilterModelFilter>& filter) { return filter->matches(userItemIndex); };
    return requiresAll ? std::ranges::all_of(filterObjects, matches) : std::ranges::any_of(filterObjects, matches);
}

SortFilterModel::SortFilterModel(SortFilterModelParams params)      //
    : _params(std::move(params)),                                   //
      _ordering(IndexOrdering{comparatorOf(_params.sortStrategy)}), //
      _maxWorkPerCall(kDefaultMaxWorkPerCall)                       //
{}

SortFilterModelParams SortFilterModel::takeParams() && {
    assert(!_isIterating);
    // the ordering references the comparison operator we are about to give away
    _shownItems.clear();
    return std::move(_params);
}

void SortFilterModel::work() {
    assert(!_isIterating);
    const auto* scoreEvaluator = this->scoreEvaluator();

    const std::size_t chunkEnd = std::min(_params.numViewedItems, _nextItemToProcess + _maxWorkPerCall);
    for (; _nextItemToProcess < chunkEnd; ++_nextItemToProcess) {
        if (_params.filters.matches(_nextItemToProcess)) {
            const float       score = scoreEvaluator ? scoreEvaluator->score(_nextItemToProcess) : 0.f;
            const ScoredIndex item{score, _nextItemToProcess};
            // another option here could be to just append and then sort at the end, but it seems preferable
            // to have more predictable performance on a given frame as opposed to less work overall, or else
            // move this off to another thread
            _shownItems.insert(std::ranges::upper_bound(_shownItems, item, _ordering), item);
        }
    }
}

void SortFilterModel::setMaxWork(std::size_t numItems) {
    assert(numItems > 0UZ);
    _maxWorkPerCall = numItems;
}

float SortFilterModel::progress() const { return _params.numViewedItems == 0UZ ? 1.f : static_cast<float>(_nextItemToProcess) / static_cast<float>(_params.numViewedItems); }

} // namespace DigitizerUi::components
