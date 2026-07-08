#ifndef OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_

#include "../common/Events.hpp"
#include "../common/FramePacer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace DigitizerUi::components {

struct SortFilterModelFilter {
    virtual ~SortFilterModelFilter()                                = default;
    [[nodiscard]] virtual bool matches(std::size_t itemIndex) const = 0;
};

struct SortFilterModelSortByComparisonStrategy {
    virtual ~SortFilterModelSortByComparisonStrategy()                                = default;
    [[nodiscard]] virtual bool less(std::size_t lhsIndex, std::size_t rhsIndex) const = 0;
};

struct SortFilterModelSortByScoreStrategy {
    virtual ~SortFilterModelSortByScoreStrategy()                  = default;
    [[nodiscard]] virtual float score(std::size_t itemIndex) const = 0;
};

template<typename Callable>
std::unique_ptr<SortFilterModelSortByScoreStrategy> makeSimpleSortFilterModelSortByScoreStrategy(Callable callable) {
    struct Evaluator : SortFilterModelSortByScoreStrategy {
        Callable _callable;
        explicit Evaluator(Callable&& callable) : _callable(std::move(callable)) {}
        float score(std::size_t index) const override { return std::invoke(_callable, index); }
    };
    return std::make_unique<Evaluator>(std::move(callable));
}

/// Null means output items are in the original item order
using SortFilterModelSortStrategy = std::variant<std::unique_ptr<SortFilterModelSortByComparisonStrategy>, std::unique_ptr<SortFilterModelSortByScoreStrategy>>;

struct ModelFilters {
    std::vector<std::unique_ptr<SortFilterModelFilter>> filterObjects;
    std::function<bool(std::size_t)>                    requiredFilter; // this filter is always required even if requiresAll is false
    bool                                                requiresAll = true;

    [[nodiscard]] bool matches(std::size_t userItemIndex) const {
        if (requiredFilter && !requiredFilter(userItemIndex)) {
            return false;
        }
        if (filterObjects.empty()) {
            return true;
        }
        const auto matches = [userItemIndex](const std::unique_ptr<SortFilterModelFilter>& filter) { return filter->matches(userItemIndex); };
        return requiresAll ? std::ranges::all_of(filterObjects, matches) : std::ranges::any_of(filterObjects, matches);
    }
};

struct SortFilterModelParams {
    std::size_t                 numViewedItems{};
    ModelFilters                filters;
    SortFilterModelSortStrategy sortStrategy;
};

struct SortFilterModel {
    using ScoredIndex                                   = std::pair<float, std::size_t>;
    using const_iterator                                = std::vector<ScoredIndex>::const_iterator;
    static constexpr std::size_t kDefaultMaxWorkPerCall = 2048UZ;

    explicit SortFilterModel(SortFilterModelParams params)
        : _params(std::move(params)),                                              //
          _ordering(IndexOrdering{getIfComparatorStrategy(_params.sortStrategy)}), //
          _maxWorkPerCall(kDefaultMaxWorkPerCall) {}

    [[nodiscard]] SortFilterModelParams takeParams() && {
        assert(!_isIterating);
        // the ordering references the comparison operator we are about to give away
        _shownItems.clear();
        return std::move(_params);
    }

    [[nodiscard]] constexpr bool        requiresAllFilters() const { return _params.filters.requiresAll; }
    [[nodiscard]] constexpr std::size_t numFilters() const { return _params.filters.filterObjects.size(); }

    [[nodiscard]] const SortFilterModelSortByComparisonStrategy* comparisonOperator() const { return getIf<SortFilterModelSortByComparisonStrategy>(); }
    [[nodiscard]] const SortFilterModelSortByScoreStrategy*      scoreEvaluator() const { return getIf<SortFilterModelSortByScoreStrategy>(); }

    [[nodiscard]] const ModelFilters& filters() const { return _params.filters; }

    void work() {
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

        // request more work if we need it
        if (_nextItemToProcess < _params.numViewedItems) {
            EventLoop::instance().executeLater([] { globalFramePacer().requestFrame(); });
        }
    }

    [[nodiscard]] bool isComplete() const { return _nextItemToProcess >= _params.numViewedItems; }

    struct ItemsView {
        explicit ItemsView(const SortFilterModel& model) : _model(model) {
            assert(!_model._isIterating);
            _model._isIterating = true;
        }
        ~ItemsView() { _model._isIterating = false; }
        ItemsView(const ItemsView&)            = delete;
        ItemsView& operator=(const ItemsView&) = delete;

        [[nodiscard]] const_iterator     begin() const { return _model._shownItems.begin(); }
        [[nodiscard]] const_iterator     end() const { return _model._shownItems.end(); }
        [[nodiscard]] std::size_t        size() const { return _model._shownItems.size(); }
        [[nodiscard]] const ScoredIndex& operator[](std::size_t row) const { return _model._shownItems[row]; }

        const SortFilterModel& _model;
    };

    /// Returns items as they are- note that the sort may be incomplete or inaccurate before isComplete() returns true
    [[nodiscard]] ItemsView items() const { return ItemsView(*this); }

    /// fraction of items processed so far as a percentage [0, 1]
    [[nodiscard]] float progress() const {   //
        return _params.numViewedItems == 0UZ //
                   ? 1.f
                   : static_cast<float>(_nextItemToProcess) / static_cast<float>(_params.numViewedItems);
    }

    struct IndexOrdering {
        const SortFilterModelSortByComparisonStrategy* comparator = nullptr;

        bool operator()(const ScoredIndex& lhs, const ScoredIndex& rhs) const {
            if (comparator != nullptr) {
                return comparator->less(lhs.second, rhs.second);
            }
            return lhs.first > rhs.first; // highest score first
        }
    };

    static const SortFilterModelSortByComparisonStrategy* getIfComparatorStrategy(const SortFilterModelSortStrategy& strategy) {
        const auto* comparisonOperator = std::get_if<std::unique_ptr<SortFilterModelSortByComparisonStrategy>>(&strategy);
        return comparisonOperator != nullptr ? comparisonOperator->get() : nullptr;
    }

    template<typename T>
    const T* getIf() const {
        auto* uniquePtrPtr = std::get_if<std::unique_ptr<T>>(&_params.sortStrategy);
        return uniquePtrPtr ? uniquePtrPtr->get() : nullptr;
    }

    /// used by tests
    void setMaxWork(std::size_t numItems) {
        assert(numItems > 0UZ);
        _maxWorkPerCall = numItems;
    }

    SortFilterModelParams    _params;
    std::vector<ScoredIndex> _shownItems;
    IndexOrdering            _ordering;
    std::size_t              _nextItemToProcess = 0UZ;
    std::size_t              _maxWorkPerCall;

    // for debugging
    mutable bool _isIterating = false;
};

} // namespace DigitizerUi::components

#endif
