#include "blocks/DrawableBlockConfig.hpp"
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

    "block names containing colons are parsed correctly"_test = [&] {
        expect(entriesString("sub:BlockA:gain") == "sub:BlockA/gain");
        constexpr static auto* namespacedBlockMap = "gr::basic::SignalGenerator<float32>:frequency";
        expect(entriesString(namespacedBlockMap) == "gr::basic::SignalGenerator<float32>/frequency");
        expect(entriesString("sub:BlockA,BlockB::x:gain") == "sub:BlockA/gain BlockB::x/gain");
        constexpr static auto* singleColonMap = "sub:BlockA,BlockB:gain";
        expect(TargetMap::fromString(singleColonMap)->toString() == singleColonMap);
        expect(TargetMap::fromString(namespacedBlockMap)->toString() == namespacedBlockMap);
    };

    "test whitespace and empty target_map"_test = [&] {
        expect(entriesString(" BlockA , BlockC : sample_rate ") == "BlockA/sample_rate BlockC/sample_rate");
        expect(entriesString("").empty());
        expect(entriesString("  ").empty());
    };

    "invalid target_maps return an error"_test = [] {
        for (const std::string_view malformed : {"BlockA", "BlockA:", ":gain", "A:b:", "A:b;", "A:b;;C:d", "A,,B:x", "*,A:x", "A,*:x"}) {
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

    "toString() <-> fromString() rountrip restores the original value"_test = [] {
        for (const std::string_view text : {"BlockA:sample_rate;BlockB:gain", "BlockA,BlockC:sample_rate", "*:sample_rate", "A:b;*:c"}) {
            const auto targets = TargetMap::fromString(text);
            expect(targets.has_value()) << fatal << text;
            expect(targets->toString() == text) << text;
        }
        expect(TargetMap::fromString("BlockA:gain;BlockB:gain")->toString() == "BlockA,BlockB:gain");
        expect(TargetMap::fromString("")->toString().empty());
    };

    "addTarget groups by property and deduplicates"_test = [] {
        auto targets = TargetMap::fromString("BlockA:gain").value();
        targets.addTarget({"BlockB", "gain"});
        expect(targets.toString() == "BlockA,BlockB:gain");
        targets.addTarget({"BlockB", "gain"});
        expect(targets.toString() == "BlockA,BlockB:gain");
        targets.addTarget({"BlockA", "offset"});
        expect(targets.toString() == "BlockA,BlockB:gain;BlockA:offset");
        targets.addTarget({"*", "gain"});
        expect(targets.toString() == "BlockA,BlockB:gain;BlockA:offset;*:gain");
    };

    "removeTarget with a specific connection will remove the specific one and not a glob"_test = [] {
        auto targets = TargetMap::fromString("BlockA,BlockB:gain;*:gain;BlockA:offset").value();
        targets.removeTarget({"BlockA", "gain"});
        expect(targets.toString() == "BlockB:gain;*:gain;BlockA:offset");
        targets.removeTarget({"*", "gain"});
        expect(targets.toString() == "BlockB:gain;BlockA:offset");
        targets.removeTarget({"BlockC", "gain"});
        expect(targets.toString() == "BlockB:gain;BlockA:offset");
        targets.removeTarget({"BlockB", "gain"});
        targets.removeTarget({"BlockA", "offset"});
        expect(targets.entries().empty());
    };

    "parsing a target_map should do deduplication"_test = [&] {
        expect(entriesString("BlockA:gain;BlockA:gain") == "BlockA/gain");
        expect(entriesString("BlockA,BlockA:gain") == "BlockA/gain");
        expect(entriesString("*:gain;*:gain") == "*/gain");
    };

    "isCompatibleType() with specific types"_test = [] {
        using DigitizerUi::isCompatibleType;
        expect(isCompatibleType("float32", "float32"));
        expect(!isCompatibleType("float32", "float64")) << "no implicit widening";
        expect(!isCompatibleType("float32", "float3")) << "whole names, not prefixes";
        expect(isCompatibleType("float64;float32;int64", "int64"));
        expect(isCompatibleType(" float64 ; int64 ", "int64")) << "whitespace around entries is ignored";
        expect(!isCompatibleType("float64;float32;int64", "bool"));
    };

    "isCompatibleType() specifying empty string or \"*\" for a glob"_test = [] {
        using DigitizerUi::isCompatibleType;
        for (const std::string_view typeName : {"float32", "bool", "string", "enum"}) {
            expect(isCompatibleType("", typeName)) << typeName;
            expect(isCompatibleType("  ", typeName)) << typeName;
            expect(isCompatibleType("*", typeName)) << typeName;
            expect(isCompatibleType("bool;*", typeName)) << typeName;
        }
    };

    "passing a map that only constrains based on units to isCompatibleType()"_test = [] {
        using DigitizerUi::isCompatibleType;
        expect(isCompatibleType("unit:Color;unit:RGB", "uint32", "Color"));
        expect(isCompatibleType("unit:Color;unit:RGB", "float32", "RGB"));
        expect(!isCompatibleType("unit:Color;unit:RGB", "uint32", "Hz"));
        expect(!isCompatibleType("unit:Color;unit:RGB", "uint32", "")) << "a property without a unit";
        expect(!isCompatibleType("unit:Color", "uint32", "color")) << "units are case sensitive";
        expect(isCompatibleType(" unit: Color ;", "uint32", "Color")) << "whitespace and empty entries are ignored";
    };

    "passing a map with both types and units to isCompatibleType()"_test = [] {
        using DigitizerUi::isCompatibleType;
        expect(isCompatibleType("unit:Color;unit:RGB;uint32", "uint32", "RGB"));
        expect(!isCompatibleType("unit:Color;unit:RGB;uint32", "float32", "Color")) << "unit but not type";
        expect(!isCompatibleType("unit:Color;unit:RGB;uint32", "uint32", "Hz")) << "type but not unit";
        expect(!isCompatibleType("unit:Color;unit:RGB;uint32", "uint32", "")) << "type but no unit";
        expect(isCompatibleType("float32;float64", "float64", "Hz")) << "if not unit is in the map then it works on all units";
        expect(isCompatibleType("*;unit:Hz", "bool", "Hz")) << "type glob selector and a unit works on any type";
    };

    "DrawableBlockConfig toMap and fromMap are each others' inverse"_test = [] {
        using DigitizerUi::DrawableBlockConfig;
        using ControlledProperty = DrawableBlockConfig::ControlledProperty;
        const std::vector<DrawableBlockConfig> configs{
            DrawableBlockConfig{},
            DrawableBlockConfig{.maxWidth = 30.f, .showLabel = false},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = true}},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = 30.f, .hints = {.min = 10., .max = 100.}}, .maxWidth = 120.f},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = std::int32_t{3}, .hints = {.min = -5.}}},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = std::string("Cos"), .hints = {.choices = {"Sin", "Cos", "Square"}}}, .showLabel = true},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = 2.5, .targetBlockName = "sineSink1"}},
            DrawableBlockConfig{.aggregateControlledPropertyHints = {.max = 50.}},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = 30.f, .hints = {.min = 10., .max = 100.}}, .aggregateControlledPropertyHints = {.min = 10., .max = 50.}},
            DrawableBlockConfig{.firstControlledProperty = ControlledProperty{.value = std::string("Cos"), .hints = {.choices = {"Sin", "Cos", "Square"}}}, .aggregateControlledPropertyHints = {.choices = {"Sin", "Cos"}}},
        };
        for (const auto& config : configs) {
            expect(DrawableBlockConfig::fromMap(config.toMap().value_or(gr::property_map{})) == config);
        }
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
