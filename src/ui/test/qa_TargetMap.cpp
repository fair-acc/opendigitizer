#include "blocks/TargetMap.hpp"

#include <format>
#include <string>
#include <string_view>

#include <boost/ut.hpp>

using namespace boost::ut;

const boost::ut::suite<"target_map"> targetMapTests = [] {
    using DigitizerUi::TargetEntry;
    using DigitizerUi::TargetMap;
    using DigitizerUi::TargetRelationship;

    const auto entriesString = [](std::string_view text) {
        const auto parsed = TargetMap::fromString(text);
        expect(parsed.has_value()) << fatal << text;
        std::string list;
        for (const TargetEntry& entry : parsed->entries()) {
            list += std::format("{}{}/{}", list.empty() ? "" : " ", entry.blockTarget, entry.propertyName);
        }
        return list;
    };

    "verify that parsing seems to produce the same target map as the input string"_test = [&] {
        expect(entriesString("BlockA:sample_rate;BlockB:gain") == "BlockA/sample_rate BlockB/gain");
        expect(entriesString("BlockA,BlockC:sample_rate") == "BlockA/sample_rate BlockC/sample_rate");
        expect(entriesString("*:sample_rate") == "*/sample_rate");
    };

    "test whitespace and empty target_map"_test = [&] {
        expect(entriesString(" BlockA , BlockC : sample_rate ") == "BlockA/sample_rate BlockC/sample_rate");
        expect(entriesString("").empty());
        expect(entriesString("  ").empty());
    };

    "invalid target_maps return an error"_test = [] {
        for (const std::string_view malformed : {"BlockA", "BlockA:", ":gain", "A:b:c", "A:b;", "A:b;;C:d", "A,,B:x", "*,A:x", "A,*:x"}) {
            expect(!TargetMap::fromString(malformed).has_value()) << malformed;
        }
    };

    "test TargetEntry::relationshipToTarget"_test = [] {
        const TargetEntry named{"BlockA", "gain"};
        expect(named.relationshipToTarget("BlockA", "gain") == TargetRelationship::TargetingSpecifically);
        expect(named.relationshipToTarget("BlockB", "gain") == TargetRelationship::NotTargeting);
        expect(named.relationshipToTarget("BlockA", "offset") == TargetRelationship::NotTargeting);

        for (const TargetEntry& glob : {TargetEntry{"*", "gain"}, TargetEntry{"", "gain"}}) {
            expect(glob.isGlob()) << glob.blockTarget;
            expect(glob.relationshipToTarget("BlockA", "gain") == TargetRelationship::TargetingViaGlob);
            expect(glob.relationshipToTarget("BlockB", "gain") == TargetRelationship::TargetingViaGlob);
            expect(glob.relationshipToTarget("BlockA", "offset") == TargetRelationship::NotTargeting);
        }
        const bool globCanBeStarOrEmptyString = TargetEntry{"*", "gain"} == TargetEntry{"", "gain"};
        expect(globCanBeStarOrEmptyString) << "operator== should treat any form of glob as the same";
        const bool notEquals = TargetEntry{"BlockA", "gain"} != TargetEntry{"*", "gain"};
        expect(notEquals) << "operator!= should differentiate named and glob items";
    };

    "TargetMap::relationshipToTarget returns the strongest relationship of its entries"_test = [] {
        const auto targets = TargetMap::fromString("BlockA:gain;*:offset");
        expect(targets.has_value()) << fatal;
        expect(targets->relationshipToTarget("BlockA", "gain") == TargetRelationship::TargetingSpecifically);
        expect(targets->relationshipToTarget("BlockB", "gain") == TargetRelationship::NotTargeting);
        expect(targets->relationshipToTarget("BlockA", "offset") == TargetRelationship::TargetingViaGlob);
        expect(targets->relationshipToTarget("BlockB", "offset") == TargetRelationship::TargetingViaGlob);

        const auto both = TargetMap::fromString("BlockA:gain;*:gain");
        expect(both.has_value()) << fatal;
        expect(both->relationshipToTarget("BlockA", "gain") == TargetRelationship::TargetingSpecifically);
        expect(both->relationshipToTarget("BlockB", "gain") == TargetRelationship::TargetingViaGlob);
    };

    "parsing a target_map should do deduplication"_test = [&] {
        expect(entriesString("BlockA:gain;BlockA:gain") == "BlockA/gain");
        expect(entriesString("BlockA,BlockA:gain") == "BlockA/gain");
        expect(entriesString("*:gain;*:gain") == "*/gain");
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
