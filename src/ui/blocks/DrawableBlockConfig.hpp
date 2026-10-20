#ifndef OPENDIGITIZER_UI_DRAWABLE_BLOCK_CONFIG_HPP
#define OPENDIGITIZER_UI_DRAWABLE_BLOCK_CONFIG_HPP

#include <gnuradio-4.0/Tag.hpp>

#include <magic_enum.hpp>

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace DigitizerUi {

/// Constraints on the values a block property accepts
struct BlockPropertyHints {
    // we have no validation to stop min from being greater than max
    std::optional<double>    min     = {};
    std::optional<double>    max     = {};
    std::vector<std::string> choices = {}; // empty is the same as not specified

    [[nodiscard]] constexpr bool     has_value() const { return min.has_value() || max.has_value() || !choices.empty(); }
    [[nodiscard]] constexpr explicit operator bool() const { return has_value(); }
    [[nodiscard]] constexpr bool     operator==(const BlockPropertyHints&) const = default;

    /// Returns the hints which satisfy both this and @param other, or nullopt if the ranges or choices do not overlap at all
    [[nodiscard]] constexpr std::optional<BlockPropertyHints> intersection(const BlockPropertyHints& other) const {
        constexpr static auto selectRangeBound = [](std::optional<double> a, std::optional<double> b, auto pick) -> std::optional<double> {
            if (a.has_value() && b.has_value()) {
                return pick(*a, *b);
            }
            return a.has_value() ? a : b;
        };
        BlockPropertyHints result{.min = selectRangeBound(min, other.min, [](double a, double b) { return std::max(a, b); }), .max = selectRangeBound(max, other.max, [](double a, double b) { return std::min(a, b); })};
        if (result.min.has_value() && result.max.has_value() && *result.min > *result.max) {
            return std::nullopt;
        }
        if (choices.empty() || other.choices.empty()) {
            result.choices = choices.empty() ? other.choices : choices;
            return result;
        }
        result.choices = choices | std::views::filter([&other](const std::string& choice) { return std::ranges::contains(other.choices, choice); }) | std::ranges::to<std::vector>();
        if (result.choices.empty()) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] constexpr static BlockPropertyHints fromMap(const gr::property_map& hintsMap) {
        BlockPropertyHints result;

        using namespace std::string_view_literals;
        const auto* min = hintsMap.get_if<double>("min"sv);
        const auto* max = hintsMap.get_if<double>("max"sv);
        result.min      = min ? std::optional{*min} : std::nullopt;
        result.max      = max ? std::optional{*max} : std::nullopt;

        const auto choicesValue = hintsMap.find_value("choices"sv).value_or(gr::pmt::Value{});
        const auto choices      = choicesValue.get_if<gr::TensorView<gr::pmt::Value>>();
        if (!choices) {
            return result;
        }

        // ignore non-string values but accept empty string as a choice. remove duplicates
        constexpr static auto valueToString    = [](auto value) { return value.value_or(std::string{}); };
        result.choices                         = *choices | std::views::filter(&gr::pmt::Value::is_string) | std::views::transform(valueToString) | std::ranges::to<std::vector>();
        const auto [firstRemoved, lastRemoved] = std::ranges::unique(result.choices);
        result.choices.erase(firstRemoved, lastRemoved);

        return result;
    }

    [[nodiscard]] constexpr std::optional<gr::property_map> toMap() const {
        std::optional<gr::property_map> map;
        if (!has_value()) {
            return map;
        }
        map.emplace();

        using namespace std::string_view_literals;
        if (!choices.empty()) {
            // std::ranges::to<gr::Tensor<gr::Value>>() on strings converts the whole vector into one gr::Value and yields an empty tensor
            constexpr static auto stringToValue = [](const std::string& choice) { return gr::pmt::Value{choice}; };
            map->emplace("choices"sv, gr::Tensor<gr::pmt::Value>(gr::data_from, choices | std::views::transform(stringToValue) | std::ranges::to<std::vector>()));
        }
        if (min.has_value()) {
            map->emplace("min", *min);
        }
        if (max.has_value()) {
            map->emplace("max", *max);
        }
        return map;
    }
};

/// DrawableBlockConfig is a strongly typed representation of the values that
/// opendigitizer will pass to a drawable UI block's draw() function. This
/// defines the expected schema and parsing rules.
///
/// An example config:
/// {"first_controlled_property": {"value": 30.f, "hints": {"min": 10., "max": 100.}}, "aggregate_controlled_property_hints": {"min": 10., "max": 50.}}
///
/// Or:
/// {"max_width": 30.f, show_label: false, "drawing_context": "FlowgraphView", "first_controlled_property": { "value": true } }
///
/// Note that parsing uses strict type checking.
/// max_width must be a float, and min/max hints must be doubles.
struct DrawableBlockConfig {
    enum class DrawingContext { Toolbar, FlowgraphView };

    struct ControlledProperty {
        [[nodiscard]] static constexpr ControlledProperty fromMap(const gr::property_map& configMap) {
            ControlledProperty result;
            using namespace std::string_view_literals;
            if (const auto controlledProperty = configMap.get_if<gr::property_map>("first_controlled_property"sv)) {
                result.value = controlledProperty->find_value("value"sv).value_or(gr::pmt::Value{});
                if (const auto hints = controlledProperty->get_if<gr::property_map>("hints"sv)) {
                    result.hints = BlockPropertyHints::fromMap(*hints);
                }
                if (const auto targetBlockName = controlledProperty->get_if<std::string_view>("target_block_name"sv)) {
                    result.targetBlockName = *targetBlockName;
                }
                if (const auto targetPropertyName = controlledProperty->get_if<std::string_view>("target_property_name"sv)) {
                    result.targetPropertyName = *targetPropertyName;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr std::optional<gr::property_map> toMap() const {
            std::optional<gr::property_map> map;
            if (!has_value()) {
                return map;
            }
            map.emplace();

            using namespace std::string_view_literals;
            map->emplace("value"sv, value);
            if (auto hintsMap = hints.toMap()) {
                map->emplace("hints"sv, std::move(*hintsMap));
            }
            if (!targetBlockName.empty()) {
                map->emplace("target_block_name"sv, targetBlockName);
            }
            if (!targetPropertyName.empty()) {
                map->emplace("target_property_name"sv, targetPropertyName);
            }
            return map;
        }

        // "value" is a required field so we overload not having a value to be the same as not specifying first_controlled_property at all
        [[nodiscard]] constexpr bool     has_value() const { return value.has_value(); }
        [[nodiscard]] constexpr explicit operator bool() const { return this->has_value(); }
        [[nodiscard]] constexpr bool     operator==(const ControlledProperty&) const = default;

        gr::pmt::Value     value = {};
        BlockPropertyHints hints = {};
        // sometimes empty for testing purposes
        std::string targetBlockName    = {};
        std::string targetPropertyName = {};
    };

    [[nodiscard]] static constexpr DrawableBlockConfig fromMap(const gr::property_map& configMap) {
        using namespace std::string_view_literals;
        DrawableBlockConfig result{.firstControlledProperty = ControlledProperty::fromMap(configMap)};
        if (auto aggregateHints = configMap.get_if<gr::property_map>("aggregate_controlled_property_hints"sv)) {
            result.aggregateControlledPropertyHints = BlockPropertyHints::fromMap(*aggregateHints);
        }
        if (auto maxWidth = configMap.get_if<float>("max_width"sv)) {
            result.maxWidth = *maxWidth;
        }
        if (auto showLabel = configMap.get_if<bool>("show_label"sv)) {
            result.showLabel = *showLabel;
        }
        if (auto drawingContext = configMap.get_if<std::string_view>("drawing_context"sv)) {
            result.drawingContext = magic_enum::enum_cast<DrawingContext>(*drawingContext);
        }
        return result;
    }

    [[nodiscard]] constexpr std::optional<gr::property_map> toMap() const {
        std::optional<gr::property_map> map;
        if (!has_value()) {
            return map;
        }
        map.emplace();

        using namespace std::string_view_literals;
        if (auto controlledPropertyMap = firstControlledProperty.toMap()) {
            map->emplace("first_controlled_property"sv, std::move(*controlledPropertyMap));
        }
        if (auto aggregateHintsMap = aggregateControlledPropertyHints.toMap()) {
            map->emplace("aggregate_controlled_property_hints"sv, std::move(*aggregateHintsMap));
        }
        if (maxWidth.has_value()) {
            map->emplace("max_width"sv, *maxWidth);
        }
        if (showLabel.has_value()) {
            map->emplace("show_label"sv, *showLabel);
        }
        if (drawingContext.has_value()) {
            map->emplace("drawing_context"sv, std::string(magic_enum::enum_name(*drawingContext)));
        }
        return map;
    }

    [[nodiscard]] constexpr bool     has_value() const { return firstControlledProperty || aggregateControlledPropertyHints || maxWidth.has_value() || showLabel.has_value() || drawingContext.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const { return this->has_value(); }
    [[nodiscard]] constexpr bool     operator==(const DrawableBlockConfig&) const = default;

    ControlledProperty            firstControlledProperty          = {};
    BlockPropertyHints            aggregateControlledPropertyHints = {}; // constraints satisfying every property in the target_map, not just the first
    std::optional<float>          maxWidth                         = {};
    std::optional<bool>           showLabel                        = {};
    std::optional<DrawingContext> drawingContext                   = {};
};

} // namespace DigitizerUi

#endif
