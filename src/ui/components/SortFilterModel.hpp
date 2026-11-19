#ifndef OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_MODEL_HPP_

#include "../common/Events.hpp"
#include "../common/FramePacer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace DigitizerUi::components {

struct SortFilterModelParams {
    std::size_t                                   numViewedItems{};
    std::function<bool(std::size_t)>              filter; // empty: every item
    std::function<bool(std::size_t, std::size_t)> less;   // empty: by score
    std::function<float(std::size_t)>             score;  // highest first; neither: original order
};

struct SortFilterModel {
    using ScoredIndex                                   = std::pair<float, std::size_t>;
    using const_iterator                                = std::vector<ScoredIndex>::const_iterator;
    static constexpr std::size_t kDefaultMaxWorkPerCall = 2048UZ;

    explicit SortFilterModel(SortFilterModelParams params) : _params(std::move(params)), _maxWorkPerCall(kDefaultMaxWorkPerCall) {}

    void work() {
        assert(!_isIterating);
        const auto ordering = [this](const ScoredIndex& lhs, const ScoredIndex& rhs) { return _params.less ? _params.less(lhs.second, rhs.second) : lhs.first > rhs.first; };

        const std::size_t chunkEnd = std::min(_params.numViewedItems, _nextItemToProcess + _maxWorkPerCall);
        for (; _nextItemToProcess < chunkEnd; ++_nextItemToProcess) {
            if (!_params.filter || _params.filter(_nextItemToProcess)) {
                const float       score = _params.score ? _params.score(_nextItemToProcess) : 0.f;
                const ScoredIndex item{score, _nextItemToProcess};
                // another option here could be to just append and then sort at the end, but it seems preferable
                // to have more predictable performance on a given frame as opposed to less work overall, or else
                // move this off to another thread
                _shownItems.insert(std::ranges::upper_bound(_shownItems, item, ordering), item);
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

    /// used by tests
    void setMaxWork(std::size_t numItems) {
        assert(numItems > 0UZ);
        _maxWorkPerCall = numItems;
    }

    SortFilterModelParams    _params;
    std::vector<ScoredIndex> _shownItems;
    std::size_t              _nextItemToProcess = 0UZ;
    std::size_t              _maxWorkPerCall;

    // for debugging
    mutable bool _isIterating = false;
};

} // namespace DigitizerUi::components

#endif
