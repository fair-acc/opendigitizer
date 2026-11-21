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

namespace gr::od::components {

struct SortFilterTreeModelNode {
    std::string_view             name;
    std::optional<std::size_t>   index; // the index of this item in the actual array, if it is a leaf/item
    std::span<const std::string> path;  // names of the ancestor branches not including this node

    [[nodiscard]] constexpr bool isBranch() const { return !index.has_value(); }
};

struct SortFilterTreeModelParams {
    std::size_t numItems{};
    /// returns something like { "source", "subfolder", "dashboard_name" }
    std::function<std::vector<std::string>(std::size_t)>                                getItemPathFunction;
    std::function<bool(std::size_t)>                                                    filter; // empty: every item
    std::function<bool(const SortFilterTreeModelNode&, const SortFilterTreeModelNode&)> less;   // empty: by score
    std::function<float(const SortFilterTreeModelNode&)>                                score;  // highest first; neither: original order
};

struct SortFilterTreeModel {
    static constexpr std::size_t kDefaultMaxWorkPerCall = 512UZ;

    struct Branch;

    struct Child {
        std::string                                        name;
        float                                              score;
        std::variant<std::size_t, std::unique_ptr<Branch>> value;

        [[nodiscard]] SortFilterTreeModelNode asNode(std::span<const std::string> parentPath) const {
            const auto* leafIndex = std::get_if<std::size_t>(&value);
            return {.name = name, .index = leafIndex != nullptr ? std::optional{*leafIndex} : std::nullopt, .path = parentPath};
        }
    };

    struct Branch {
        std::vector<Child> children; // should always be in sorted order
    };

    Branch                    _root;
    SortFilterTreeModelParams _params;
    std::size_t               _nextItemToProcess = 0UZ;
    std::size_t               _maxWorkPerCall;

    explicit SortFilterTreeModel(SortFilterTreeModelParams params)
        : _params(std::move(params)),             //
          _maxWorkPerCall(kDefaultMaxWorkPerCall) //
    {}

    void work() {
        const std::size_t chunkEnd = std::min(_params.numItems, _nextItemToProcess + _maxWorkPerCall);
        for (; _nextItemToProcess < chunkEnd; ++_nextItemToProcess) {
            insertItem(_nextItemToProcess);
        }

        if (!isComplete()) {
            EventLoop::instance().executeLater([] { globalFramePacer().requestFrame(); });
        }
    }

    [[nodiscard]] bool isComplete() const { return _nextItemToProcess >= _params.numItems; }

    /// fraction of items processed so far in [0, 1]
    [[nodiscard]] float progress() const { //
        return _params.numItems == 0UZ     //
                   ? 1.f
                   : static_cast<float>(_nextItemToProcess) / static_cast<float>(_params.numItems);
    }

    /// `beforeVisitor` returns whether to recurse into the children, `afterVisitor` runs after a branch's children
    template<typename BeforeCallable, typename AfterCallable>
    requires std::is_invocable_r_v<bool, BeforeCallable&, const SortFilterTreeModelNode&> && std::is_invocable_v<AfterCallable&, const SortFilterTreeModelNode&>
    void visitAllItemsDepthFirst(BeforeCallable&& beforeVisitor, AfterCallable&& afterVisitor) const {
        std::vector<std::string> ancestorNames;
        visitChildren(_root, ancestorNames, beforeVisitor, afterVisitor);
    }

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

    void insertItem(std::size_t itemIndex) {
        assert(_params.getItemPathFunction);
        const std::vector<std::string> path = _params.getItemPathFunction(itemIndex);
        if (path.empty() || std::ranges::contains(path, std::string{})) {
            return;
        }

        // filter before creating branch elements, that way we only create branches that have children
        if (_params.filter && !_params.filter(itemIndex)) {
            return;
        }

        const std::span<const std::string> fullPath{path};
        const std::span<const std::string> leafParentPath = fullPath.first(path.size() - 1UZ);
        const SortFilterTreeModelNode      leafNode{.name = path.back(), .index = itemIndex, .path = leafParentPath};

        Branch* parent = &_root;
        for (std::size_t depth = 0UZ; depth + 1UZ < path.size(); ++depth) {
            const std::string&                 branchName = path[depth];
            const std::span<const std::string> parentPath = fullPath.first(depth);

            if (const auto existing = std::ranges::find(parent->children, branchName, &Child::name); existing != parent->children.end()) {
                const auto* childBranch = std::get_if<std::unique_ptr<Branch>>(&existing->value);
                if (childBranch == nullptr) {
                    return; // branch already filtered
                }
                parent = childBranch->get();
                continue;
            }

            const SortFilterTreeModelNode branchNode{.name = branchName, .index = std::nullopt, .path = parentPath};
            // no ability to prune branches
            Child& insertedBranch = insertSorted(*parent, Child{.name = branchName, .score = scoreOf(branchNode), .value = std::make_unique<Branch>()}, parentPath);
            parent                = std::get<std::unique_ptr<Branch>>(insertedBranch.value).get();
        }

        insertSorted(*parent, Child{.name = path.back(), .score = scoreOf(leafNode), .value = itemIndex}, leafParentPath);
    }

    Child& insertSorted(Branch& parent, Child child, std::span<const std::string> parentPath) {
        const auto lessThan = [this, parentPath](const Child& lhs, const Child& rhs) { return _params.less ? _params.less(lhs.asNode(parentPath), rhs.asNode(parentPath)) : lhs.score > rhs.score; };
        const auto position = std::ranges::upper_bound(parent.children, child, lessThan);
        return *parent.children.insert(position, std::move(child));
    }

    [[nodiscard]] float scoreOf(const SortFilterTreeModelNode& node) const { return _params.score ? _params.score(node) : 0.f; }

    /// used by tests
    void setMaxWork(std::size_t numItems) {
        assert(numItems > 0UZ);
        _maxWorkPerCall = numItems;
    }
};

} // namespace gr::od::components

#endif
