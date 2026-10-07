#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLNUMBER_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLNUMBER_HPP

#include "../components/Keypad.hpp"
#include "ImControl.hpp"

#include <imgui_internal.h>

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace DigitizerUi {

enum class NumberStyle { field, spinBox, slider, sliderSpinner };

namespace control {
using NumberTypes = std::tuple<std::int8_t, std::uint8_t, std::int16_t, std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t, float, double>; // in ImGuiDataType order

[[nodiscard]] bool withNumberType(std::string_view valueType, auto&& fn) {
    return []<typename... T>(std::string_view name, auto& call, std::tuple<T...>*) { return ((name == gr::meta::type_name<T>() && (call.template operator()<T>(), true)) || ...); }(valueType, fn, static_cast<NumberTypes*>(nullptr));
}

template<typename T>
[[nodiscard]] constexpr ImGuiDataType imguiDataType() {
    return []<std::size_t... index>(std::index_sequence<index...>) { return static_cast<ImGuiDataType>(((std::is_same_v<T, std::tuple_element_t<index, NumberTypes>> ? index : 0UZ) + ...)); }(std::make_index_sequence<std::tuple_size_v<NumberTypes>>());
}

template<typename T>
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

    double      value      = 0.;
    std::string value_type = "float64"; // float32, float64, int8 … int64, uint8 … uint64
    std::string unit;
    double      min   = 0.;
    double      max   = 1.;
    double      step  = 0.; // 0: the last significant digit of value
    NumberStyle style = NumberStyle::field;

    GR_MAKE_REFLECTABLE(ImControlNumber, value, value_type, unit, min, max, step, style);

private:
    std::optional<double> _stepOfEnteredValue;

public:
    explicit ImControlNumber(gr::property_map initParameters = {}) : ImControl<ImControlNumber>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto [natural, minimum] = naturalAndMinimumWidgetWidth();
        const SizeAndLayout layout    = prepareDraw(config, natural, minimum, labelWidth());
        const bool          known     = control::withNumberType(value_type, [&]<typename T>() { drawNumber<T>(layout); });
        if (!known) {
            ImGui::TextUnformatted(std::format("{}: unknown value_type '{}'", label, value_type).c_str());
        }
        return gr::work::Status::OK;
    }

private:
    [[nodiscard]] static float spinnerWidth() { return 2.f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x); }

    [[nodiscard]] static double significantStep(double number) { return components::computeIncrementNonZero(std::format("{:.4f}", number)); }

    [[nodiscard]] std::pair<float, float> naturalAndMinimumWidgetWidth() const {
        switch (style) {
        case NumberStyle::field: return {control::kImControlTextInputWidth, control::kImControlMinimumWidth};
        case NumberStyle::spinBox: return {control::kImControlTextInputWidth + spinnerWidth(), control::kImControlMinimumWidth + spinnerWidth()};
        case NumberStyle::slider: return {kSliderWidth, control::kImControlMinimumWidth};
        case NumberStyle::sliderSpinner: return {kSliderWidth + spinnerWidth(), control::kImControlMinimumWidth + spinnerWidth()};
        }
        std::unreachable();
    }

    template<typename T>
    void drawNumber(const SizeAndLayout& layout) {
        if (!_stepOfEnteredValue) {
            _stepOfEnteredValue = significantStep(value);
        }
        const bool        hasRange    = style == NumberStyle::slider || style == NumberStyle::sliderSpinner;
        const double      derivedStep = hasRange ? std::min(*_stepOfEnteredValue, significantStep((max - min) / 10.)) : *_stepOfEnteredValue;
        T                 number      = control::clampedTo<T>(value);
        const T           lower       = control::clampedTo<T>(min);
        const T           upper       = control::clampedTo<T>(max);
        const T           stepSize    = std::max(control::clampedTo<T>(step != 0. ? step : derivedStep), std::is_integral_v<T> ? T{1} : T{});
        const auto        dataType    = control::imguiDataType<T>();
        const std::string format      = control::printfFormat<T>(unit);
        const std::string id          = "##" + label;
        const auto        onStepGrid  = [gridStep = static_cast<double>(stepSize)](double candidate) { return control::clampedTo<T>(std::is_floating_point_v<T> ? std::round(candidate / gridStep) * gridStep : candidate); };
        bool              typed       = false;
        bool              changed     = false;
        const auto        field       = [&](float fieldWidth) {
            ImGui::SetNextItemWidth(fieldWidth);
            typed = ImGui::InputScalar(id.c_str(), dataType, &number, nullptr, nullptr, format.c_str());
            changed |= typed;
        };
        const auto slider = [&](float sliderWidth) {
            ImGui::SetNextItemWidth(sliderWidth);
            if (ImGui::SliderScalar(id.c_str(), dataType, &number, &lower, &upper, format.c_str())) {
                typed = ImGui::TempInputIsActive(ImGui::GetItemID()); // Ctrl+click turns the slider into a text field
                if (!typed) {
                    number = std::clamp(onStepGrid(static_cast<double>(number)), lower, upper);
                }
                changed = true;
            }
        };
        const auto spinner = [&](T spinLower, T spinUpper) {
            IMW::ChangeStrId spinnerScope(id.c_str());
            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            const ImVec2 buttonSize(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("-", buttonSize)) {
                number  = std::clamp(onStepGrid(static_cast<double>(number) - static_cast<double>(stepSize)), spinLower, spinUpper);
                changed = true;
            }
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("+", buttonSize)) {
                number  = std::clamp(onStepGrid(static_cast<double>(number) + static_cast<double>(stepSize)), spinLower, spinUpper);
                changed = true;
            }
            ImGui::PopItemFlag();
        };
        drawLabelled(layout, [&](float width) {
            switch (style) {
            case NumberStyle::field: field(width); break;
            case NumberStyle::spinBox:
                field(width - spinnerWidth());
                spinner(std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max());
                break;
            case NumberStyle::slider: slider(width); break;
            case NumberStyle::sliderSpinner:
                slider(width - spinnerWidth());
                spinner(lower, upper);
                break;
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

    static constexpr float kSliderWidth = 160.f;
};

} // namespace DigitizerUi

#endif
