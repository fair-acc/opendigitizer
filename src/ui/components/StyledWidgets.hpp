#ifndef OPENDIGITIZER_UI_COMPONENTS_STYLED_WIDGETS_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_STYLED_WIDGETS_HPP_

#include <stdex/c_resource.hpp>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

// replacements of ImGui widgets with styling customisation points, which may call the ImGui defaults depending on the theme;
// only the minimum the current styles need: e.g. a custom Selectable would require reimplementing ImGui's Menu
namespace gr::od {

bool                checkbox(const char* label, bool* value);
[[nodiscard]] float toggleSwitchWidth(); // without the label

/// in ImGuiDataType_ order: maps between ImGuiDataType_ and the C++ types at compile time
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
void                endCombo();
[[nodiscard]] float comboWidthForText(float textWidth); // of a beginCombo() whose value is textWidth wide, capped at LookAndFeel::comboTextMaxWidthEms
using ComboBox = stdex::c_resource<bool, &beginCombo, &endCombo>;

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
bool   menuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);

enum class PopupFlags : std::uint8_t {
    None = 0,
    // darken the background outside the popup, ie. the user doesn't need to be focusing on the background
    WantsExclusiveFocus = 1 << 0,
    // a modal popup can't be closed by clicking outside of it
    Modal = 1 << 1,
    // if this is specified then a sheet style popup will just take up LookAndFeel::sheetMaxLengthFractionOfDisplay, useful for popup contents that are dependent on the size of their parent
    WantsMaxSize = 1 << 2,
};
[[nodiscard]] constexpr PopupFlags operator|(PopupFlags lhs, PopupFlags rhs) { return static_cast<PopupFlags>(std::to_underlying(lhs) | std::to_underlying(rhs)); }
[[nodiscard]] constexpr bool       hasFlag(PopupFlags flags, PopupFlags flag) { return (std::to_underlying(flags) & std::to_underlying(flag)) != 0; }

struct PopupOptions {
    PopupFlags       flags              = PopupFlags::WantsExclusiveFocus;
    ImGuiWindowFlags desktopWindowFlags = ImGuiWindowFlags_None; // additional flags for when the desktop style is in use
};

bool beginPopup(const char* id, const PopupOptions& options);
void endPopup();
using Popup = stdex::c_resource<bool, &beginPopup, &endPopup>;

/// Closes the popup that is currently being drawn, ie. it must be called within the scope of beginPopup and endPopup
void closeCurrentPopup();

bool colorEdit3(const char* label, float color[3], ImGuiColorEditFlags flags = ImGuiColorEditFlags_None);

bool beginMenu(const char* label, const char* icon = nullptr);
void endMenu();
using Menu = stdex::c_resource<bool, &beginMenu, &endMenu>;

/// Stages a request to close the popup specified by the given ID, from *outside* the popup (unlike ImGui::CloseCurrentPopup(), which is called during the drawing of the popup and force-closes it)
void closePopup(const char* id);

namespace detail {
/// Whether an open/close animation is playing for any popup, useful for testing
[[nodiscard]] bool isPopupAnimating(const char* id);

/// Whether closePopup() was called for the popup since it was last begun. Clears the request
[[nodiscard]] bool takePopupCloseRequest(ImGuiID popupId);
} // namespace detail

} // namespace gr::od

#endif
