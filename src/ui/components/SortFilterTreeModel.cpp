#include "SortFilterTreeModel.hpp"

#include <algorithm>
#include <cassert>

namespace DigitizerUi::components {

namespace {

constexpr std::size_t kDefaultMaxWorkPerCall = 512UZ;

const SortFilterTreeModelComparator* comparatorOf(const SortFilterTreeModelSortStrategy& strategy) {
    const auto* comparisonOperator = std::get_if<std::unique_ptr<SortFilterTreeModelComparator>>(&strategy);
    return comparisonOperator != nullptr ? comparisonOperator->get() : nullptr;
}

const SortFilterTreeModelScoreEvaluator* scoreEvaluatorOf(const SortFilterTreeModelSortStrategy& strategy) {
    const auto* evaluator = std::get_if<std::unique_ptr<SortFilterTreeModelScoreEvaluator>>(&strategy);
    return evaluator != nullptr ? evaluator->get() : nullptr;
}

} // namespace

SortFilterTreeModelNode SortFilterTreeModel::Child::asNode(std::span<const std::string> parentPath) const {
    const auto* leafIndex = std::get_if<std::size_t>(&value);
    return {.name = name, .index = leafIndex != nullptr ? std::optional{*leafIndex} : std::nullopt, .path = parentPath};
}

SortFilterTreeModel::SortFilterTreeModel(SortFilterTreeModelParams params) //
    : _params(std::move(params)),                                          //
      _maxWorkPerCall(kDefaultMaxWorkPerCall)                              //
{}

SortFilterTreeModelParams SortFilterTreeModel::takeParams() && {
    _root.children.clear();
    return std::move(_params);
}

void SortFilterTreeModel::work() {
    const std::size_t chunkEnd = std::min(_params.numItems, _nextItemToProcess + _maxWorkPerCall);
    for (; _nextItemToProcess < chunkEnd; ++_nextItemToProcess) {
        insertItem(_nextItemToProcess);
    }
}

void SortFilterTreeModel::setMaxWork(std::size_t numItems) {
    assert(numItems > 0UZ);
    _maxWorkPerCall = numItems;
}

float SortFilterTreeModel::progress() const { return _params.numItems == 0UZ ? 1.f : static_cast<float>(_nextItemToProcess) / static_cast<float>(_params.numItems); }

float SortFilterTreeModel::scoreOf(const SortFilterTreeModelNode& node) const {
    const auto* evaluator = scoreEvaluatorOf(_params.sortStrategy);
    return evaluator != nullptr ? evaluator->score(node) : 0.f;
}

SortFilterTreeModel::Child& SortFilterTreeModel::insertSorted(Branch& parent, Child child, std::span<const std::string> parentPath) {
    const auto* comparator = comparatorOf(_params.sortStrategy);
    const auto  lessThan   = [comparator, parentPath](const Child& lhs, const Child& rhs) {
        if (comparator != nullptr) {
            return comparator->less(lhs.asNode(parentPath), rhs.asNode(parentPath));
        }
        return lhs.score > rhs.score;
    };
    const auto position = std::ranges::upper_bound(parent.children, child, lessThan);
    return *parent.children.insert(position, std::move(child));
}

void SortFilterTreeModel::insertItem(std::size_t itemIndex) {
    assert(_params.getItemPathFunction);
    const std::vector<std::string> path = _params.getItemPathFunction(itemIndex);
    if (path.empty() || std::ranges::contains(path, std::string{})) {
        return;
    }

    // filter before creating branch elements that way we only create branches that have children
    if (!_params.filters.matches(itemIndex)) {
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

} // namespace DigitizerUi::components
