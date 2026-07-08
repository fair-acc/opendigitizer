#ifndef OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace DigitizerUi::components {

/// A filter predicate. It is expected that the user only uses one type and
/// always downcasts to that
struct SortFilterModelFilter {
    virtual ~SortFilterModelFilter()                                = default;
    [[nodiscard]] virtual bool matches(std::size_t itemIndex) const = 0;
};

/// A comparison function for sorting
struct SortFilterModelComparator {
    virtual ~SortFilterModelComparator()                                              = default;
    [[nodiscard]] virtual bool less(std::size_t lhsIndex, std::size_t rhsIndex) const = 0;
};

/// A function which scores an item in order to decide how it should be sorted,
/// which is different from the sorting that happens with
/// SortFilterModelComparator because it is evaluated once per item as opposed
/// to each time a comparison occurs.
struct SortFilterModelScoreEvaluator {
    virtual ~SortFilterModelScoreEvaluator()                       = default;
    [[nodiscard]] virtual float score(std::size_t itemIndex) const = 0;
};

/// Creates a SortFilterModelScoreEvaluator with a function and anonymous type
template<typename Callable>
std::unique_ptr<SortFilterModelScoreEvaluator> makeSimpleSortFilterModelScoreEvaluator(Callable callable) {
    struct Evaluator : SortFilterModelScoreEvaluator {
        Callable _callable;
        explicit Evaluator(Callable&& callable) : _callable(std::move(callable)) {}
        float score(std::size_t index) const override { return std::invoke(_callable, index); }
    };
    return std::make_unique<Evaluator>(std::move(callable));
}

/// Null means output items are in the original item order
using SortFilterModelSortStrategy = std::variant<std::unique_ptr<SortFilterModelComparator>, std::unique_ptr<SortFilterModelScoreEvaluator>>;

struct ModelFilters {
    std::vector<std::unique_ptr<SortFilterModelFilter>> filterObjects;
    std::function<bool(std::size_t)>                    requiredFilter; // this filter is always required even if requiresAll is false
    bool                                                requiresAll = true;

    [[nodiscard]] bool matches(std::size_t userItemIndex) const;
};

struct SortFilterModelParams {
    std::size_t                 numViewedItems{};
    ModelFilters                filters;
    SortFilterModelSortStrategy sortStrategy;
};

/// Accepts a list of filters, a sort strategy and some settings, then each
/// frame will perform some work to filter and sort the list. Eventually it
/// resolves, after which point it does no more work. To change the parameters,
/// call .takeParams() to get the parameters back, modify them, then construct a
/// new SortFilterModel and begin work again
class SortFilterModel {
public:
    using ScoredIndex    = std::pair<float, std::size_t>;
    using const_iterator = std::vector<ScoredIndex>::const_iterator;

    explicit SortFilterModel(SortFilterModelParams params);

    [[nodiscard]] SortFilterModelParams takeParams() &&;

    [[nodiscard]] constexpr bool        requiresAllFilters() const { return _params.filters.requiresAll; }
    [[nodiscard]] constexpr std::size_t numFilters() const { return _params.filters.filterObjects.size(); }

    [[nodiscard]] const SortFilterModelComparator*     comparisonOperator() const { return getIf<SortFilterModelComparator>(); }
    [[nodiscard]] const SortFilterModelScoreEvaluator* scoreEvaluator() const { return getIf<SortFilterModelScoreEvaluator>(); }

    [[nodiscard]] const ModelFilters& filters() const { return _params.filters; }

    // calculate sorting + filtering, and requests another frame if there's more work to do
    void work();

    [[nodiscard]] bool isComplete() const { return _nextItemToProcess >= _params.numViewedItems; }

    /// limits how many items each work() call processes, mostly useful for tests
    void setMaxWork(std::size_t numItems);

    class ItemsView {
    public:
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

    private:
        const SortFilterModel& _model;
    };

    /// get items, always works even if work is not yet complete
    [[nodiscard]] ItemsView items() const { return ItemsView(*this); }

    /// fraction of items processed so far in [0, 1], for drawing a progress bar
    [[nodiscard]] float progress() const;

private:
    struct IndexOrdering {
        const SortFilterModelComparator* comparator = nullptr;

        bool operator()(const ScoredIndex& lhs, const ScoredIndex& rhs) const {
            if (comparator != nullptr) {
                return comparator->less(lhs.second, rhs.second);
            }
            return lhs.first > rhs.first; // highest score first
        }
    };

    template<typename T>
    const T* getIf() const {
        auto* uniquePtrPtr = std::get_if<std::unique_ptr<T>>(&_params.sortStrategy);
        return uniquePtrPtr ? uniquePtrPtr->get() : nullptr;
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
