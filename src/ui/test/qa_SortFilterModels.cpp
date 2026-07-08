#include "../components/SortFilterModel.hpp"
#include "../components/SortFilterTreeModel.hpp"

#include <gnuradio-4.0/meta/indirect.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <numeric>
#include <string>
#include <variant>
#include <vector>

using namespace boost::ut;
using DigitizerUi::components::SortFilterModel;
using DigitizerUi::components::SortFilterModelComparator;
using DigitizerUi::components::SortFilterModelFilter;
using DigitizerUi::components::SortFilterModelParams;
using DigitizerUi::components::SortFilterModelScoreEvaluator;
using DigitizerUi::components::SortFilterTreeModel;
using DigitizerUi::components::SortFilterTreeModelComparator;
using DigitizerUi::components::SortFilterTreeModelNode;
using DigitizerUi::components::SortFilterTreeModelParams;

namespace {

const std::vector<std::string> sampleWords{                                          //
    "beam", "injection", "extraction", "spectrum", "magnet", "vacuum", "rf", "cryo", //
    "dipole", "cavity", "kicker", "Spectrum", "orbit", "ramp", "cycle", "archive",   //
    "Cavity", "backup", "storage", "ring", "linac", "dipole", "Dipole"};

/// Comparator which will say a lot of slightly different strings are equal,
/// this way the sort order being stable can be tested
struct BadComparator : SortFilterModelComparator {
    const std::vector<std::string>& items;
    explicit BadComparator(const std::vector<std::string>& viewedItems) : items(viewedItems) {}

    bool less(std::size_t lhsIndex, std::size_t rhsIndex) const override {
        const std::string& lhs = items[lhsIndex];
        const std::string& rhs = items[rhsIndex];
        if (lhs.size() != rhs.size()) {
            return lhs.size() < rhs.size();
        }
        const auto toLower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        return std::ranges::lexicographical_compare(lhs, rhs, {}, toLower, toLower);
    }
};

/// Score evaluator which scores a lot of different words the same.
/// Like BadComparator, this is so that stability can be tested
struct BadScoreEvaluator : SortFilterModelScoreEvaluator {
    const std::vector<std::string>& items;
    explicit BadScoreEvaluator(const std::vector<std::string>& viewedItems) : items(viewedItems) {}

    float score(std::size_t itemIndex) const override { return static_cast<float>(items[itemIndex].size()); }
};

/// BadScoreEvaluator which additionally counts how often it was invoked
struct CountingScoreEvaluator : BadScoreEvaluator {
    std::size_t& numEvaluations;

    CountingScoreEvaluator(const std::vector<std::string>& viewedItems, std::size_t& evaluationCounter) : BadScoreEvaluator(viewedItems), numEvaluations(evaluationCounter) {}

    float score(std::size_t itemIndex) const override {
        ++numEvaluations;
        return BadScoreEvaluator::score(itemIndex);
    }
};

struct EvenIndexFilter : SortFilterModelFilter {
    bool matches(std::size_t itemIndex) const override { return itemIndex % 2UZ == 0UZ; }
};

std::vector<std::size_t> stableSortedIndices(std::size_t numItems, const std::function<bool(std::size_t, std::size_t)>& less) {
    std::vector<std::size_t> indices(numItems);
    std::iota(indices.begin(), indices.end(), 0UZ);
    std::stable_sort(indices.begin(), indices.end(), less);
    return indices;
}

std::vector<std::size_t> resolveSortFilterOutput(SortFilterModel& model) {
    while (!model.isComplete()) {
        model.work();
    }
    const auto items = model.items();
    return std::ranges::subrange(items.begin(), items.end()) | std::views::values | std::ranges::to<std::vector>();
}

using PathList = std::vector<std::vector<std::string>>;

const PathList samplePaths{
    {"local", "beam", "orbit"},
    {"local", "beam", "injection"},
    {"local", "spectrum"},
    {"remote", "magnet"},
    {"remote", "cryo", "vacuum"},
    {"local", "beam", "extraction"},
};

SortFilterTreeModelParams paramsFor(const PathList& paths) {
    return {
        .numItems            = paths.size(),
        .getItemPathFunction = [&paths](std::size_t index) { return paths[index]; },
    };
}

/// this is used in the tests to describe how a tree should look. it is a vector of pairs
/// so that the sorted order can be described
struct TreeNode;
using TreeChildren = std::vector<std::pair<std::string, gr::meta::indirect<TreeNode>>>;
struct TreeNode {
    // either a leaf's item index, or a branch's children. uses int so it accepts int literals in the initializer
    std::variant<int, TreeChildren> value;
    bool                            operator==(const TreeNode&) const = default;
};
bool operator==(const gr::meta::indirect<TreeNode>& lhs, const gr::meta::indirect<TreeNode>& rhs) { return *lhs == *rhs; }

/// Convert a SortFilterTreeModel into the TreeChildren above so we can compare it with == to the expected value
TreeChildren resolveSortingAndReturnTree(SortFilterTreeModel& model) {
    while (!model.isComplete()) {
        model.work();
    }
    std::vector<TreeChildren> childrenStack(1UZ);
    const auto                before = [&childrenStack](const SortFilterTreeModelNode& node) {
        if (node.isBranch()) {
            childrenStack.emplace_back();
            return true;
        }
        childrenStack.back().emplace_back(std::string{node.name}, static_cast<int>(*node.index));
        return false;
    };
    const auto after = [&childrenStack](const SortFilterTreeModelNode& node) {
        TreeChildren completedBranch = std::move(childrenStack.back());
        childrenStack.pop_back();
        childrenStack.back().emplace_back(std::string{node.name}, std::move(completedBranch));
    };
    model.visitAllItemsDepthFirst(before, after);
    return std::move(childrenStack.front());
}

struct NameComparator : SortFilterTreeModelComparator {
    bool less(const SortFilterTreeModelNode& lhs, const SortFilterTreeModelNode& rhs) const override { return lhs.name < rhs.name; }
};

struct RejectSomeNamesFromSamplePathsFilter : SortFilterModelFilter {
    using NameGetter = std::function<std::string(std::size_t)>;
    std::vector<std::string>    rejected;
    std::map<std::string, int>* invocationCounts = nullptr;

    explicit RejectSomeNamesFromSamplePathsFilter(std::vector<std::string> rejectedNames, std::map<std::string, int>* counts = nullptr) //
        : rejected(std::move(rejectedNames)), invocationCounts(counts) {}

    bool matches(std::size_t index) const override {
        if (invocationCounts != nullptr) {
            ++(*invocationCounts)[samplePaths.at(index).back()];
        }
        return !std::ranges::contains(rejected, samplePaths.at(index).back());
    }
};

} // namespace

const static boost::ut::suite<"SortFilterModel"> sortFilterModelTests = [] {
    constexpr static auto badComparatorFunction = [](std::size_t lhs, std::size_t rhs) { return BadComparator{sampleWords}.less(lhs, rhs); };

    "SortFilterModel matches std::stable_sort"_test = [] {
        SortFilterModel model({.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadComparator>(sampleWords)});

        expect(resolveSortFilterOutput(model) == stableSortedIndices(sampleWords.size(), badComparatorFunction));
    };

    "SortFilterModel matches std::stable_sort even with small work count"_test = [] {
        SortFilterModel model({.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadComparator>(sampleWords)});
        model.setMaxWork(1UZ);

        expect(resolveSortFilterOutput(model) == stableSortedIndices(sampleWords.size(), badComparatorFunction));
    };

    constexpr static auto badScoreEvaluatorFunction = [](std::size_t lhs, std::size_t rhs) { return BadScoreEvaluator{sampleWords}.score(lhs) > BadScoreEvaluator{sampleWords}.score(rhs); };

    "SortFilterModelScoreEvaluator matches std::stable_sort with the same comparison function"_test = [] {
        SortFilterModel model({.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadScoreEvaluator>(sampleWords)});

        expect(resolveSortFilterOutput(model) == stableSortedIndices(sampleWords.size(), badScoreEvaluatorFunction));
    };

    "ScoreEvaluator works the same with small work size"_test = [] {
        SortFilterModel model({.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadScoreEvaluator>(sampleWords)});
        model.setMaxWork(1UZ);

        expect(resolveSortFilterOutput(model) == stableSortedIndices(sampleWords.size(), badScoreEvaluatorFunction));
    };

    "ScoreEvaluator is only evaluated once per item"_test = [] {
        std::size_t           numEvaluations = 0UZ;
        SortFilterModelParams params{.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<CountingScoreEvaluator>(sampleWords, numEvaluations)};
        params.filters.filterObjects.emplace_back(std::make_unique<EvenIndexFilter>());
        SortFilterModel model(std::move(params));

        const std::vector<std::size_t> shown = resolveSortFilterOutput(model);
        expect(eq(numEvaluations, shown.size()));
    };

    "Null sort strategy means items just stay in the same order"_test = [] {
        SortFilterModel model({.numViewedItems = sampleWords.size()});

        std::vector<std::size_t> expected(sampleWords.size());
        std::iota(expected.begin(), expected.end(), 0UZ);
        expect(resolveSortFilterOutput(model) == expected);
    };

    "Filtering and sorting together works, and is the same as std::erase_if and std::stable_sort"_test = [] {
        SortFilterModelParams params{.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadComparator>(sampleWords)};
        params.filters.filterObjects.emplace_back(std::make_unique<EvenIndexFilter>());
        SortFilterModel model(std::move(params));

        auto expected = stableSortedIndices(sampleWords.size(), badComparatorFunction);
        std::erase_if(expected, [](std::size_t index) { return index % 2UZ != 0UZ; });
        expect(resolveSortFilterOutput(model) == expected);
    };

    "Number of work() calls looks like the expected amount"_test = [] {
        SortFilterModel       model({.numViewedItems = sampleWords.size(), .sortStrategy = std::make_unique<BadComparator>(sampleWords)});
        constexpr std::size_t maxWork = 3UZ;
        model.setMaxWork(maxWork);

        const std::size_t expectedWorkCalls = (sampleWords.size() + maxWork - 1UZ) / maxWork;
        for (std::size_t call = 0UZ; call < expectedWorkCalls; ++call) {
            expect(!model.isComplete()) << "work should not be done after " << call << " calls";
            expect(le(model.progress(), 1.f));
            model.work();
        }
        expect(model.isComplete()) << "work should be done after " << expectedWorkCalls << " calls";
        expect(eq(model.progress(), 1.f));
    };

    "an empty model resolves immediately"_test = [] {
        SortFilterModel model({.numViewedItems = 0UZ});
        expect(model.isComplete());
        expect(eq(model.items().size(), 0UZ));
        expect(eq(model.progress(), 1.f));
    };
};

const static boost::ut::suite<"SortFilterTreeModel"> sortFilterTreeModelTests = [] {
    "items are in insertion order when there is no sort strategy"_test = [] {
        SortFilterTreeModel model(paramsFor(samplePaths));

        const TreeChildren expected{
            {"local",
                TreeChildren{
                    {"beam", TreeChildren{{"orbit", 0}, {"injection", 1}, {"extraction", 5}}},
                    {"spectrum", 2},
                }},
            {"remote",
                TreeChildren{
                    {"magnet", 3},
                    {"cryo", TreeChildren{{"vacuum", 4}}},
                }},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "comparator orders leaves and branches with a small work size"_test = [] {
        auto params         = paramsFor(samplePaths);
        params.sortStrategy = std::make_unique<NameComparator>();
        SortFilterTreeModel model(std::move(params));
        model.setMaxWork(1UZ);

        // branches and leaves are interleaved since the sorting compares indiscriminately
        const TreeChildren expected{
            {"local",
                TreeChildren{
                    {"beam", TreeChildren{{"extraction", 5}, {"injection", 1}, {"orbit", 0}}},
                    {"spectrum", 2},
                }},
            {"remote",
                TreeChildren{
                    {"cryo", TreeChildren{{"vacuum", 4}}},
                    {"magnet", 3},
                }},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "score evaluator sibling ordering"_test = [] {
        auto params         = paramsFor(samplePaths);
        params.sortStrategy = DigitizerUi::components::makeSimpleSortFilterTreeModelScoreEvaluator([](const SortFilterTreeModelNode& node) { return static_cast<float>(node.name.size()); });
        SortFilterTreeModel model(std::move(params));

        // longest names first always
        const TreeChildren expected{
            {"remote",
                TreeChildren{
                    {"magnet", 3},
                    {"cryo", TreeChildren{{"vacuum", 4}}},
                }},
            {"local",
                TreeChildren{
                    {"spectrum", 2},
                    {"beam", TreeChildren{{"extraction", 5}, {"injection", 1}, {"orbit", 0}}},
                }},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "a filtered leaf is not inserted"_test = [] {
        auto params = paramsFor(samplePaths);
        params.filters.filterObjects.emplace_back(std::make_unique<RejectSomeNamesFromSamplePathsFilter>(std::vector<std::string>{"spectrum", "magnet"}));
        SortFilterTreeModel model(std::move(params));

        const TreeChildren expected{
            {"local", TreeChildren{{"beam", TreeChildren{{"orbit", 0}, {"injection", 1}, {"extraction", 5}}}}},
            {"remote", TreeChildren{{"cryo", TreeChildren{{"vacuum", 4}}}}},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "a branch with no leaves is not shown"_test = [] {
        auto params = paramsFor(samplePaths);
        params.filters.filterObjects.emplace_back(std::make_unique<RejectSomeNamesFromSamplePathsFilter>(std::vector<std::string>{"vacuum"}));
        SortFilterTreeModel model(std::move(params));

        // "remote/cryo" only contained "vacuum" so it must not appear as an empty branch
        const TreeChildren expected{
            {"local", TreeChildren{{"beam", TreeChildren{{"orbit", 0}, {"injection", 1}, {"extraction", 5}}}, {"spectrum", 2}}},
            {"remote", TreeChildren{{"magnet", 3}}},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "any empty string in a path causes the item to be filtered"_test = [] {
        const PathList paths{
            {"local", "orbit"},
            {"", "injection"},
            {"local", ""},
            {"remote", "", "magnet"},
        };
        SortFilterTreeModel model(paramsFor(paths));

        const TreeChildren expected{{"local", TreeChildren{{"orbit", 0}}}};
        const bool         matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
    };

    "pruning a branch only requires one evaluation of the filter predicate"_test = [] {
        std::map<std::string, int> invocationCounts;
        auto                       params = paramsFor(samplePaths);
        params.filters.filterObjects.emplace_back(std::make_unique<RejectSomeNamesFromSamplePathsFilter>(std::vector<std::string>{"beam"}, &invocationCounts));
        SortFilterTreeModel model(std::move(params));

        const TreeChildren expected{
            {"local", TreeChildren{{"spectrum", 2}}},
            {"remote", TreeChildren{{"magnet", 3}, {"cryo", TreeChildren{{"vacuum", 4}}}}},
        };
        const bool matchesExpected = resolveSortingAndReturnTree(model) == expected;
        expect(matchesExpected);
        expect(eq(invocationCounts["beam"], 1)) << "pruning a branch should only require one call, after that the tree should remember that the branch is pruned";
    };

    "work is spread over the expected number of calls"_test = [] {
        SortFilterTreeModel   model(paramsFor(samplePaths));
        constexpr std::size_t maxWork = 2UZ;
        model.setMaxWork(maxWork);

        const std::size_t expectedWorkCalls = (samplePaths.size() + maxWork - 1UZ) / maxWork;
        for (std::size_t call = 0UZ; call < expectedWorkCalls; ++call) {
            expect(!model.isComplete()) << "work should not be done after " << call << " calls";
            expect(le(model.progress(), 1.f));
            model.work();
        }
        expect(model.isComplete());
        expect(eq(model.progress(), 1.f));
    };

    "an empty model resolves immediately"_test = [] {
        const PathList      noPaths;
        SortFilterTreeModel model(paramsFor(noPaths));
        expect(model.isComplete());
        expect(eq(model.progress(), 1.f));
        const bool isEmpty = resolveSortingAndReturnTree(model) == TreeChildren{};
        expect(isEmpty);
    };

    "node paths"_test = [] {
        SortFilterTreeModel model(paramsFor(samplePaths));
        while (!model.isComplete()) {
            model.work();
        }

        std::vector<std::vector<std::string>> leafPaths;
        const auto                            before = [&leafPaths](const SortFilterTreeModelNode& node) {
            if (!node.isBranch()) {
                std::vector<std::string> fullPath{node.path.begin(), node.path.end()};
                fullPath.emplace_back(node.name);
                leafPaths.push_back(std::move(fullPath));
            }
            return true;
        };
        const auto after = [](const SortFilterTreeModelNode&) {};
        model.visitAllItemsDepthFirst(before, after);

        expect(eq(leafPaths.size(), samplePaths.size()));
        for (const auto& path : samplePaths) {
            expect(std::ranges::contains(leafPaths, path)) << "we should be able to reconstruct the tree from the paths provided by the visitor function";
        }
    };
};

int main() { return 0; }
