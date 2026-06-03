#include "../components/NewBlockSelectorFuzzySearch.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <array>
#include <string_view>

using namespace boost::ut;
using namespace std::string_view_literals;
using DigitizerUi::components::filterTypename;
using DigitizerUi::components::wordDistance;

namespace {

double scoreOf(std::string_view block, std::string_view filter) { return filterTypename(block, filter).score; }

template<std::size_t N>
auto sortByScore(std::array<std::string_view, N> items, std::string_view filter) {
    std::ranges::stable_sort(items, std::ranges::greater(), [filter](std::string_view item) { return scoreOf(item, filter); });
    return items;
}

} // namespace

const static boost::ut::suite<"Levenshtein word distance"> wordDistanceTests = [] {
    "identical strings have zero distance"_test = [] {
        expect(eq(wordDistance("hello", "hello"), 0uz));
        expect(eq(wordDistance("", ""), 0uz));
        expect(eq(wordDistance("a", "a"), 0uz));
    };

    "distance to empty string is the string length"_test = [] {
        expect(eq(wordDistance("abc", ""), 3uz));
        expect(eq(wordDistance("", "abcd"), 4uz));
    };

    "single character substitution"_test = [] {
        expect(eq(wordDistance("cat", "car"), 1uz));
        expect(eq(wordDistance("cat", "bat"), 1uz));
    };

    "single character insertion or deletion"_test = [] {
        expect(eq(wordDistance("cat", "cats"), 1uz));
        expect(eq(wordDistance("cats", "cat"), 1uz));
        expect(eq(wordDistance("at", "cat"), 1uz));
    };

    "completely different strings"_test = [] { //
        expect(eq(wordDistance("abc", "xyz"), 3uz));
    };

    "distance is symmetric"_test = [] {
        expect(eq(wordDistance("kitten", "sitting"), wordDistance("sitting", "kitten")));
        expect(eq(wordDistance("flaw", "lawn"), wordDistance("lawn", "flaw")));
    };

    "kitten/sitting example"_test = [] { //
        expect(eq(wordDistance("kitten", "sitting"), 3uz));
    };
};

const static boost::ut::suite<"fuzzy search scoring"> fuzzySearchTests = [] {
    "progressively worse matches sort in expected order"_test = [] {
        constexpr std::string_view                filter = "Multiply";
        constexpr std::array<std::string_view, 4> items  = {"Multiply", "Multipl", "Multaaa", "Zzzzzzz"};

        const auto sorted = sortByScore(items, filter);
        expect(eq(sorted[0], "Multiply"sv));
        expect(eq(sorted[1], "Multipl"sv));
        expect(eq(sorted[2], "Multaaa"sv));
        expect(eq(sorted[3], "Zzzzzzz"sv));
    };

    "exact substring match outranks close edit distance"_test = [] {
        constexpr std::string_view filter = "Sink";

        constexpr std::string_view exactMatch = "gr::basic::DataSink";
        constexpr std::string_view closeA     = "Sank";
        constexpr std::string_view closeB     = "Silk";
        constexpr std::string_view closeC     = "Sint";

        const auto exactScore = scoreOf(exactMatch, filter);
        expect(gt(exactScore, scoreOf(closeA, filter)));
        expect(gt(exactScore, scoreOf(closeB, filter)));
        expect(gt(exactScore, scoreOf(closeC, filter)));
    };

    "best component of a namespaced identifier wins"_test = [] {
        constexpr std::string_view filter = "Selector";

        constexpr std::string_view goodComponent = "zz::xx::Selector";
        constexpr std::string_view mediocre      = "Selecbar::Selectom::sekector";

        expect(gt(scoreOf(goodComponent, filter), scoreOf(mediocre, filter)));
    };

    "search is case insensitive"_test = [] {
        constexpr std::string_view filter = "sink";

        const auto lower = scoreOf("DataSink", filter);
        const auto upper = scoreOf("DATASINK", filter);
        expect(eq(lower, upper));
    };

    "empty filter gives zero score"_test = [] { //
        expect(eq(scoreOf("anything", ""), 0.0));
    };

    "search happens sections, split by namespace delimiters"_test = [] {
        constexpr std::string_view filter = "basic::Selector";

        constexpr std::string_view perfect  = "gr::basic::Selector";
        constexpr std::string_view halfGood = "gr::other::Selector";

        expect(gt(scoreOf(perfect, filter), scoreOf(halfGood, filter)));
    };
};

int main() { return 0; }
