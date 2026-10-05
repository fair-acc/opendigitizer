#include "blocks/TargetMap.hpp"

#include <string_view>
#include <vector>

#include <boost/ut.hpp>

using namespace boost::ut;

const boost::ut::suite<"target_map"> targetMapTests = [] {
    using DigitizerUi::parseTargetMap;
    using DigitizerUi::TargetEntry;

    "the three examples of #511 parse to their blocks and properties"_test = [] {
        const auto                     perBlock = parseTargetMap("BlockA:sample_rate;BlockB:gain");
        const std::vector<TargetEntry> expectedPerBlock{{{"BlockA"}, false, "sample_rate"}, {{"BlockB"}, false, "gain"}};
        expect(perBlock.has_value() && *perBlock == expectedPerBlock);
        const auto                     twoBlocks = parseTargetMap("BlockA,BlockC:sample_rate");
        const std::vector<TargetEntry> expectedTwoBlocks{{{"BlockA", "BlockC"}, false, "sample_rate"}};
        expect(twoBlocks.has_value() && *twoBlocks == expectedTwoBlocks);
        const auto                     allBlocks = parseTargetMap("*:sample_rate");
        const std::vector<TargetEntry> expectedAllBlocks{{{}, true, "sample_rate"}};
        expect(allBlocks.has_value() && *allBlocks == expectedAllBlocks);
    };

    "blanks around names are ignored and an empty map has no targets"_test = [] {
        const auto                     spaced = parseTargetMap(" BlockA , BlockC : sample_rate ");
        const std::vector<TargetEntry> expectedSpaced{{{"BlockA", "BlockC"}, false, "sample_rate"}};
        expect(spaced.has_value() && *spaced == expectedSpaced);
        expect(parseTargetMap("").has_value() && parseTargetMap("")->empty());
        expect(parseTargetMap("  ").has_value() && parseTargetMap("  ")->empty());
    };

    "entries the grammar does not produce are rejected"_test = [] {
        for (const std::string_view malformed : {"BlockA", "BlockA:", ":gain", "A:b:c", "A:b;", "A:b;;C:d", "A,,B:x", "*,A:x", "A,*:x"}) {
            expect(!parseTargetMap(malformed).has_value()) << malformed;
        }
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
