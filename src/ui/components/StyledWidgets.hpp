#ifndef OPENDIGITIZER_UI_COMPONENTS_STYLED_WIDGETS_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_STYLED_WIDGETS_HPP_

#include <c_resource.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <tuple>

// replacements of ImGui widgets with styling customisation points, which may call the ImGui defaults depending on the theme;
// only the minimum the current styles need: e.g. a custom Selectable would require reimplementing ImGui's Menu
namespace gr::od {

bool                checkbox(const char* label, bool* value);
[[nodiscard]] float toggleSwitchWidth(); // without the label

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
SliderAction sliderScalar(const char* label, const SliderScalarParameters& parameters);

/// combo box in the style of SliderScalar: the value sits on the window background inside a thin rounded outline with a line-drawn chevron
bool                beginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags = 0);
[[nodiscard]] float comboWidthForText(float textWidth); // of a beginCombo() whose value is textWidth wide, capped at LookAndFeel::comboTextMaxWidthEms
using ComboBox = stdex::c_resource<bool, beginCombo, ImGui::EndCombo>;

bool treeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0);

// the colour of text and icons on ImGuiCol_Button, selectables and checkboxes; see Palette::buttonContents
ImVec4 buttonContentsColor();
bool   button(const char* label, const ImVec2& size = ImVec2{0.f, 0.f}); // ImGui::Button() in buttonContentsColor()
bool   smallButton(const char* label);
bool   textButton(const char* label, const ImVec2& size = ImVec2{0.f, 0.f});
ImVec2 calcTextButtonSize(const char* label);

ImVec4 selectableBackgroundColor(bool selected, bool hovered, bool held);
ImVec4 selectableTextColor(bool selected);
bool   selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2{0.f, 0.f});

} // namespace gr::od

#endif
