#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLNUMBER_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLNUMBER_HPP

#include "../components/Keypad.hpp"
#include "../components/SliderAndSpinner.hpp"
#include "../components/StyledStandardWidgets.hpp"
#include "ImControl.hpp"

#include <imgui_internal.h>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/formatter/ValueFormatter.hpp>

namespace DigitizerUi {

enum class NumberStyle { field, spinBox, slider, sliderSpinner };

namespace control {

[[nodiscard]] bool withNumberType(std::string_view valueType, auto&& fn) {
    return []<typename... T>(std::string_view name, auto& call, std::tuple<T...>*) { return ((name == gr::meta::type_name<T>() && (call.template operator()<T>(), true)) || ...); }(valueType, fn, static_cast<ImGuiNumberTypes*>(nullptr));
}

[[nodiscard]] inline std::string numberTypeNames() {
    return []<typename... T>(std::tuple<T...>*) { return std::array{std::string(gr::meta::type_name<T>())...} | std::views::join_with(';') | std::ranges::to<std::string>(); }(static_cast<ImGuiNumberTypes*>(nullptr));
}

template<typename T>
[[nodiscard]] std::string printfFormat(std::string_view unit) {
    std::string text = std::is_floating_point_v<T> ? "%g" : (std::is_signed_v<T> ? (sizeof(T) == 8UZ ? "%lld" : "%d") : (sizeof(T) == 8UZ ? "%llu" : "%u"));
    if (!unit.empty()) {
        text += ' ';
        for (const char c : unit) {
            text += c == '%' ? std::string_view("%%") : std::string_view(&c, 1UZ);
        }
    }
    return text;
}
} // namespace control

struct ImControlNumber : ImControl<ImControlNumber> {
    using Description = gr::Doc<"UI control that edits a number and sets it on the block settings named by target_map">;

    double            value               = 0.;
    const std::string compatible_type_map = control::numberTypeNames();
    std::string       unit;
    double            min   = std::numeric_limits<double>::lowest(); // allow further constraining of numeric range allowable by a property
    double            max   = std::numeric_limits<double>::max();    // allow further constraining of numeric range allowable by a property
    double            step  = 0.;
    NumberStyle       style = NumberStyle::field;

    GR_MAKE_REFLECTABLE(ImControlNumber, value, compatible_type_map, unit, min, max, step, style);

private:
    std::optional<double> _stepOfEnteredValue;

public:
    explicit ImControlNumber(gr::property_map initParameters = {}) : ImControl<ImControlNumber>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto   parsedConfig = DrawableBlockConfig::fromMap(config);
        const double lowerLimit   = std::max(parsedConfig.aggregateControlledPropertyHints.min.value_or(std::numeric_limits<double>::lowest()), min);
        const double upperLimit   = std::min(parsedConfig.aggregateControlledPropertyHints.max.value_or(std::numeric_limits<double>::max()), max);
        const auto   drawnStyle   = getBestStyle(style, lowerLimit, upperLimit);
        loadValueIfConnectionChanged(value, parsedConfig);
        const std::string type  = numberTypeName(parsedConfig.firstControlledProperty);
        const bool        known = control::withNumberType(type, [&]<typename T>() { drawNumber<T>(parsedConfig, drawnStyle, lowerLimit, upperLimit); });
        if (!known) {
            ImGui::TextUnformatted(std::format("{}: unknown number type '{}'", label, type).c_str());
        }
        return gr::work::Status::OK;
    }

private:
    [[nodiscard]] static std::string numberTypeName(const DrawableBlockConfig::ControlledProperty& controlledProperty) {
        const std::string controlledType = controlledProperty ? gr::pmt::detail::type_name(controlledProperty.value) : std::string{};
        const bool        isNumber       = control::withNumberType(controlledType, []<typename T>() { /* just checking if any type is correct */ });
        return isNumber ? controlledType : std::string("float64");
    }

    [[nodiscard]] static double significantStep(double number) { return components::computeIncrementNonZero(std::format("{:.4f}", number)); }

    /// Given the constraints on the control, decide on the best style that is possible to use.
    /// While slider may be desired, ImGui does not support sliders for extremely large numeric ranges
    static NumberStyle getBestStyle(NumberStyle desiredStyle, double lowerLimit, double upperLimit) {
        using enum NumberStyle;
        constexpr auto limit = std::numeric_limits<double>::max() / 2.;
        if ((desiredStyle == slider || desiredStyle == sliderSpinner) && (lowerLimit <= -limit || upperLimit >= limit)) {
            return field;
        }
        return desiredStyle;
    }

    [[nodiscard]] static std::pair<float, float> naturalAndMinimumWidgetWidth(NumberStyle style, float numberLabelWidth) {
        const DashboardStyle& dashboardStyle             = LookAndFeel::instance().dashboardStyle;
        const float           numberLabelAndSpacingWidth = ImGui::GetStyle().ItemInnerSpacing.x + numberLabelWidth;
        const float           sliderNatural              = dashboardStyle.toolbarControlSliderTrackWidth + numberLabelAndSpacingWidth;
        const float           sliderMinimum              = dashboardStyle.toolbarControlMinimumWidth + numberLabelAndSpacingWidth;
        switch (style) {
        case NumberStyle::field: return {dashboardStyle.toolbarControlInputWidth, dashboardStyle.toolbarControlMinimumWidth};
        case NumberStyle::spinBox: return {dashboardStyle.toolbarControlInputWidth + calculateSpinnerWidth(), dashboardStyle.toolbarControlMinimumWidth + calculateSpinnerWidth()};
        case NumberStyle::slider: return {sliderNatural, sliderMinimum};
        case NumberStyle::sliderSpinner: return {sliderNatural + calculateSpinnerWidth(), sliderMinimum + calculateSpinnerWidth()};
        }
        std::unreachable();
    }

    template<typename T>
    void drawNumber(const DrawableBlockConfig& parsedConfig, NumberStyle drawnStyle, double lowerLimit, double upperLimit) {
        if (!_stepOfEnteredValue) {
            _stepOfEnteredValue = significantStep(value);
        }
        const bool        isSlider    = drawnStyle == NumberStyle::slider || drawnStyle == NumberStyle::sliderSpinner;
        const double      derivedStep = isSlider ? std::min(*_stepOfEnteredValue, significantStep((upperLimit - lowerLimit) / 10.)) : *_stepOfEnteredValue;
        T                 number      = clampedTo<T>(value);
        const T           lower       = clampedTo<T>(lowerLimit);
        const T           upper       = std::max(lower, clampedTo<T>(upperLimit));
        const T           stepSize    = std::max(clampedTo<T>(step != 0. ? step : derivedStep), std::is_integral_v<T> ? T{1} : T{});
        const auto        dataType    = imguiDataType<T>();
        const std::string format      = control::printfFormat<T>(unit);
        const std::string id          = "##" + label;
        const auto [natural, minimum] = naturalAndMinimumWidgetWidth(drawnStyle, calculateNumberLabelWidthForRange(lower, upper, stepSize, format.c_str()));
        const SizeAndLayout layout    = prepareDraw(parsedConfig, natural, minimum, labelWidth());
        bool                typed     = false;
        bool                changed   = false;
        const auto          field     = [&](float fieldWidth) {
            ImGui::SetNextItemWidth(fieldWidth);
            typed  = ImGui::InputScalar(id.c_str(), dataType, &number, nullptr, nullptr, format.c_str());
            number = std::clamp(number, lower, upper);
            changed |= typed;
        };
        const auto slider = [&](float sliderWidth, bool showSpinner) {
            ImGui::SetNextItemWidth(sliderWidth);
            const SliderAction action = sliderAndSpinner(id.c_str(), &number, {.min = lower, .max = upper, .stepSize = stepSize, .format = format.c_str(), .showSpinner = showSpinner});
            typed                     = action == SliderAction::TypedInTextField;
            changed                   = action != SliderAction::NotEdited;
        };
        drawWithLabelAndTooltip(layout, [&](float width) {
            switch (drawnStyle) {
            case NumberStyle::field: field(width); break;
            case NumberStyle::spinBox:
                field(width - calculateSpinnerWidth());
                changed |= drawSpinnerButtons(id.c_str(), &number, lower, upper, stepSize);
                break;
            case NumberStyle::slider: slider(width, false); break;
            case NumberStyle::sliderSpinner: slider(width, true); break;
            }
        });
        if (typed) {
            _stepOfEnteredValue = significantStep(static_cast<double>(number));
        }
        if (changed) {
            setNumber(number);
        }
    }

    template<typename T>
    void setNumber(T number) {
        value       = static_cast<double>(number);
        std::ignore = settings().setStaged({{"value", value}});
        sendToTargets(number);
    }
};

} // namespace DigitizerUi

#endif
