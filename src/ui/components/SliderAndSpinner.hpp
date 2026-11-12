#ifndef OPENDIGITIZER_UI_COMPONENTS_SLIDER_AND_SPINNER_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SLIDER_AND_SPINNER_HPP_

#include "../common/ImguiWrap.hpp"
#include "StyledStandardWidgets.hpp"

#include <format>

namespace DigitizerUi {

template<typename T>
requires std::is_arithmetic_v<T>
[[nodiscard]] T clampedTo(double number) {
    if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(std::clamp(number, static_cast<double>(std::numeric_limits<T>::lowest()), static_cast<double>(std::numeric_limits<T>::max())));
    } else {
        if (std::isnan(number) || number <= static_cast<double>(std::numeric_limits<T>::lowest())) {
            return std::numeric_limits<T>::lowest();
        }
        if (number >= static_cast<double>(std::numeric_limits<T>::max())) {
            return std::numeric_limits<T>::max();
        }
        return static_cast<T>(number);
    }
}

template<ImGuiNumber T>
[[nodiscard]] T roundDoubleToMultipleOfNumber(double toRound, T number) {
    return clampedTo<T>(std::is_floating_point_v<T> ? std::round(toRound / static_cast<double>(number)) * static_cast<double>(number) : toRound);
}

[[nodiscard]] inline float calculateSpinnerWidth() { return 2.f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x); }

/// @returns whether the - or + button changed the value
template<ImGuiNumber T>
bool drawSpinnerButtons(const char* label, T* value, T lower, T upper, T stepSize) {
    IMW::ChangeStrId spinnerScope(label);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    const ImVec2 buttonSize(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    bool         changed = false;
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (DigitizerUi::Button("-", buttonSize)) {
        *value  = std::clamp(roundDoubleToMultipleOfNumber(static_cast<double>(*value) - static_cast<double>(stepSize), stepSize), lower, upper);
        changed = true;
    }
    ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (DigitizerUi::Button("+", buttonSize)) {
        *value  = std::clamp(roundDoubleToMultipleOfNumber(static_cast<double>(*value) + static_cast<double>(stepSize), stepSize), lower, upper);
        changed = true;
    }
    ImGui::PopItemFlag();
    return changed;
}

template<ImGuiNumber T>
[[nodiscard]] int decimalPlacesOfStep(T stepSize) {
    if constexpr (std::is_integral_v<T>) {
        return 0;
    } else {
        const double tolerance     = 4. * static_cast<double>(std::numeric_limits<T>::epsilon());
        double       scaledStep    = static_cast<double>(stepSize);
        int          decimalPlaces = 0;
        while (decimalPlaces < std::numeric_limits<T>::digits10 && std::abs(scaledStep - std::round(scaledStep)) > tolerance * std::abs(scaledStep)) {
            scaledStep *= 10.;
            ++decimalPlaces;
        }
        return decimalPlaces;
    }
}

/// How much space to reserve for the number label of a slider given the min value, max value, and step size (these are used
/// to figure out the number of significant digits) and the format (to include any truncation of the number, and the unit)
template<ImGuiNumber T>
[[nodiscard]] float calculateNumberLabelWidthForRange(T min, T max, T stepSize, const char* format) {
    const char*       numberStart      = ImParseFormatFindStart(format);
    const char*       numberEnd        = ImParseFormatFindEnd(numberStart);
    const std::string fixedPointFormat = std::is_integral_v<T> ? std::string(format) : std::format("{}%.{}f{}", std::string_view(format, static_cast<std::size_t>(numberStart - format)), decimalPlacesOfStep(stepSize), numberEnd);
    const auto        textWidth        = [&fixedPointFormat](T number) {
        char text[64];
        ImGui::DataTypeFormatString(text, IM_COUNTOF(text), imguiDataType<T>(), &number, fixedPointFormat.c_str());
        return ImGui::CalcTextSize(text).x;
    };
    return std::min(std::max(textWidth(min), textWidth(max)), sliderNumberLabelMaxWidth());
}

template<ImGuiNumber T>
struct SliderAndSpinnerParameters {
    T           min;
    T           max;
    T           stepSize;
    const char* format      = nullptr;
    bool        showSpinner = true;
};

template<ImGuiNumber T>
SliderAction sliderAndSpinner(const char* label, T* value, const SliderAndSpinnerParameters<T>& parameters) {
    if (ImGui::GetCurrentWindow()->SkipItems) {
        return SliderAction::NotEdited;
    }
    const char* format = parameters.format != nullptr ? parameters.format : ImGui::DataTypeGetInfo(imguiDataType<T>())->PrintFmt;
    T           lower  = parameters.min;
    T           upper  = std::max(parameters.min, parameters.max);

    IMW::Group group;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - (parameters.showSpinner ? calculateSpinnerWidth() : 0.f));
    const SliderScalarParameters sliderParameters{
        .dataType         = imguiDataType<T>(),
        .data             = value,
        .min              = &lower,
        .max              = &upper,
        .format           = format,
        .numberLabelWidth = calculateNumberLabelWidthForRange(lower, upper, parameters.stepSize, format),
    };
    SliderAction action = SliderScalar(label, sliderParameters);
    if (action == SliderAction::DraggedSlider) {
        *value = std::clamp(roundDoubleToMultipleOfNumber(static_cast<double>(*value), parameters.stepSize), lower, upper);
    }
    if (parameters.showSpinner && drawSpinnerButtons(label, value, lower, upper, parameters.stepSize)) {
        action = SliderAction::SteppedWithSpinner;
    }
    return action;
}

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_COMPONENTS_SLIDER_AND_SPINNER_HPP_
