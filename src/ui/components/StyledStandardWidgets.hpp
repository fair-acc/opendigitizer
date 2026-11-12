#ifndef OPENDIGITIZER_UI_COMPONENTS_STYLED_STANDARD_WIDGETS_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_STYLED_STANDARD_WIDGETS_HPP_

#include <c_resource.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <tuple>

/*
 * Functions that replace imgui equivalents and offer customization points for
 * styling the widgets, though they may just call the default imgui
 * implementations depending on the theme.
 *
 * Not all imgui widgets are present because many of them would require
 * reimplementing other (large) imgui widgets that use them (for example, a
 * custom Selectable implementation requires reimplementing imgui Menu). This
 * file just provides the bare minimum replacements required to implement the
 * styles we currently have in opendigitizer
 */
namespace DigitizerUi {

bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, bool active);
bool RadioButton(const char* label, int* v, int vButton);

bool ArrowButton(const char* strId, ImGuiDir dir);

/// A way of converting to and from the variants of enum ImGuiDataType_ and actual types at compile time
using ImGuiNumberTypes = std::tuple<std::int8_t, std::uint8_t, std::int16_t, std::uint16_t, std::int32_t, std::uint32_t, std::int64_t, std::uint64_t, float, double>;

template<typename T>
concept ImGuiNumber = []<typename... Number>(std::tuple<Number...>*) { return (std::same_as<T, Number> || ...); }(static_cast<ImGuiNumberTypes*>(nullptr));

template<ImGuiNumber T>
[[nodiscard]] constexpr ImGuiDataType imguiDataType() {
    return []<std::size_t... index>(std::index_sequence<index...>) { return static_cast<ImGuiDataType>(((std::is_same_v<T, std::tuple_element_t<index, ImGuiNumberTypes>> ? index : 0UZ) + ...)); }(std::make_index_sequence<std::tuple_size_v<ImGuiNumberTypes>>());
}

enum class SliderAction {
    NotEdited,
    DraggedSlider,
    TypedInTextField,
    SteppedWithSpinner,
};

[[nodiscard]] float sliderNumberLabelMaxWidth();

struct SliderScalarParameters {
    ImGuiDataType    dataType;
    void*            data;
    const void*      min;
    const void*      max;
    const char*      format           = nullptr;
    ImGuiSliderFlags flags            = 0;
    float            numberLabelWidth = sliderNumberLabelMaxWidth();
};
SliderAction SliderScalar(const char* label, const SliderScalarParameters& parameters);

bool BeginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags = 0);
using ComboBox = stdex::c_resource<bool, BeginCombo, ImGui::EndCombo>;

bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0);

// the color of text and icons drawn on top of ImGuiCol_Button and selectables and checkboxes. see Palette::buttonContents
ImVec4 ButtonContentsColor();
bool   Button(const char* label, const ImVec2& size = ImVec2{0.f, 0.f}); // ImGui::Button() in ButtonContentsColor()
bool   SmallButton(const char* label);
bool   TextButton(const char* label, const ImVec2& size = ImVec2{0.f, 0.f});
ImVec2 CalcButtonSize(const char* label);

ImVec4 SelectableBackgroundColor(bool selected, bool hovered, bool held);
ImVec4 SelectableTextColor(bool selected);
bool   Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2{0.f, 0.f});

} // namespace DigitizerUi

#endif
