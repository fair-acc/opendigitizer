#ifndef OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_TREE_MODEL_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SORT_FILTER_TREE_MODEL_HPP_

#include "SortFilterModel.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace DigitizerUi::components {

/// An iterator or a view of a tree node that you get when evaluating stuff
struct SortFilterTreeModelNode {
    std::string_view             name;
    std::optional<std::size_t>   index; // the index of this item in the actual array, if it is a leaf/item
    std::span<const std::string> path;  // names of the ancestor branches not including this node

    [[nodiscard]] constexpr bool isBranch() const { return !index.has_value(); }
};

/// A comparison function for sorting siblings
struct SortFilterTreeModelComparator {
    virtual ~SortFilterTreeModelComparator()                                                                      = default;
    [[nodiscard]] virtual bool less(const SortFilterTreeModelNode& lhs, const SortFilterTreeModelNode& rhs) const = 0;
};

/// A function which scores a node in order to decide how it should be sorted,
/// which is different from the sorting that happens with
/// SortFilterTreeModelComparator because it is evaluated once per node as
/// opposed to each time a comparison occurs
struct SortFilterTreeModelScoreEvaluator {
    virtual ~SortFilterTreeModelScoreEvaluator()                                 = default;
    [[nodiscard]] virtual float score(const SortFilterTreeModelNode& node) const = 0;
};

/// Creates a SortFilterTreeModelScoreEvaluator with a function and anonymous type
template<typename Callable>
requires std::is_invocable_r_v<float, Callable, const SortFilterTreeModelNode&>
std::unique_ptr<SortFilterTreeModelScoreEvaluator> makeSimpleSortFilterTreeModelScoreEvaluator(Callable callable) {
    struct Evaluator : SortFilterTreeModelScoreEvaluator {
        Callable _callable;
        explicit Evaluator(Callable&& evaluationFunction) : _callable(std::move(evaluationFunction)) {}
        float score(const SortFilterTreeModelNode& node) const override { return std::invoke(_callable, node); }
    };
    return std::make_unique<Evaluator>(std::move(callable));
}

/// Null means output items are in the original item order
using SortFilterTreeModelSortStrategy = std::variant<std::unique_ptr<SortFilterTreeModelComparator>, std::unique_ptr<SortFilterTreeModelScoreEvaluator>>;

struct SortFilterTreeModelParams {
    std::size_t numItems{};
    /// returns something like { "source", "subfolder", "dashboard_name" }
    std::function<std::vector<std::string>(std::size_t)> getItemPathFunction;
    ModelFilters                                         filters;
    SortFilterTreeModelSortStrategy                      sortStrategy;
};

/// A type which, on construction, accepts parameters describing how to
/// construct a tree from some arbitrary elements, and then begins to build the
/// tree. It is like a filesystem hierarchy in that branches are distinguished
/// by strings. It practices a sort of fake concurrency where it only
/// sorts/inserts so many items each time .work() is called. Call .isComplete()
/// to check if work is done, and then call .visitAllItemsDepthFirst() to
/// iterate over all the items in the tree
class SortFilterTreeModel {
public:
    explicit SortFilterTreeModel(SortFilterTreeModelParams params);

    [[nodiscard]] SortFilterTreeModelParams takeParams() &&;

    /// does some work, call isComplete() to see if finished, otherwise draw loading bar
    void work();

    /// limits how many items each work() call processes, mostly useful for tests
    void setMaxWork(std::size_t numItems);

    [[nodiscard]] bool isComplete() const { return _nextItemToProcess >= _params.numItems; }

    /// fraction of items processed so far in [0, 1], for drawing a progress bar
    [[nodiscard]] float progress() const;

    /// beforeVisitor returns true if we should recurse into children.
    /// afterVisitor runs at the end of iterating over all of a branch's
    /// children
    template<typename BeforeCallable, typename AfterCallable>
    requires std::is_invocable_r_v<bool, BeforeCallable&, const SortFilterTreeModelNode&> && std::is_invocable_v<AfterCallable&, const SortFilterTreeModelNode&>
    void visitAllItemsDepthFirst(BeforeCallable&& beforeVisitor, AfterCallable&& afterVisitor) const {
        std::vector<std::string> ancestorNames;
        visitChildren(_root, ancestorNames, beforeVisitor, afterVisitor);
    }

private:
    struct Branch;

    struct AlreadyFilteredMarker {};

    struct Child {
        std::string                                                               name;
        float                                                                     score;
        std::variant<std::size_t, std::unique_ptr<Branch>, AlreadyFilteredMarker> value;

        [[nodiscard]] SortFilterTreeModelNode asNode(std::span<const std::string> parentPath) const;
    };

    struct Branch {
        std::vector<Child> children; // should always be in sorted order
    };

    template<typename BeforeCallable, typename AfterCallable>
    void visitChildren(const Branch& branch, std::vector<std::string>& ancestorNames, BeforeCallable& beforeVisitor, AfterCallable& afterVisitor) const {
        for (const Child& child : branch.children) {
            if (const auto* childBranch = std::get_if<std::unique_ptr<Branch>>(&child.value)) {
                if (std::invoke(beforeVisitor, child.asNode(ancestorNames))) {
                    ancestorNames.emplace_back(child.name);
                    visitChildren(**childBranch, ancestorNames, beforeVisitor, afterVisitor);
                    ancestorNames.pop_back();
                    // might have reallocated ancestorNames so we have to call asNode() again here
                    std::invoke(afterVisitor, child.asNode(ancestorNames));
                }
            } else if (std::holds_alternative<std::size_t>(child.value)) {
                std::invoke(beforeVisitor, child.asNode(ancestorNames));
            }
        }
    }

    void                insertItem(std::size_t itemIndex);
    Child&              insertSorted(Branch& parent, Child child, std::span<const std::string> parentPath);
    [[nodiscard]] float scoreOf(const SortFilterTreeModelNode& node) const;

    Branch                    _root;
    SortFilterTreeModelParams _params;
    std::size_t               _nextItemToProcess = 0UZ;
    std::size_t               _maxWorkPerCall;
};

} // namespace DigitizerUi::components

#endif
