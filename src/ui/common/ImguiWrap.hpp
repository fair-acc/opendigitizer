#ifndef OPENDIGITIZER_IMGUI_WRAPPER_HPP
#define OPENDIGITIZER_IMGUI_WRAPPER_HPP

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <c_resource.hpp>

#include <algorithm>

namespace DigitizerUi::IMW {

namespace detail {
// We are returning a bool telling c_resource if EndDisabled needs
// to be called
inline bool ImGuiBeginDisabled(bool disabled) {
    if (disabled) {
        ImGui::BeginDisabled();
    }
    return disabled;
}

void setItemTooltip(auto&&... args) {
    if (ImGui::IsItemHovered()) {
        if constexpr (sizeof...(args) == 0) {
            ImGui::SetTooltip("");
        } else {
            ImGui::SetTooltip("%s", std::forward<decltype(args)...>(args...));
        }
    }
}

// We use different overloads of BeginChild, we will make separate RAII objects for them
inline auto ImGuiBeginChild(const char* str_id, const ImVec2& size = ImVec2(0, 0), ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0) { return ImGui::BeginChild(str_id, size, child_flags, window_flags); }

inline auto ImGuiBeginIdChild(ImGuiID id, const ImVec2& size = ImVec2(0, 0), ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0) { return ImGui::BeginChild(id, size, child_flags, window_flags); }

template<auto* Fn>
constexpr inline auto singlePop() {
    return [] { Fn(1); };
}
} // namespace detail

using Window      = stdex::c_resource<bool, ImGui::Begin, ImGui::End, true>;
using Child       = stdex::c_resource<bool, detail::ImGuiBeginChild, ImGui::EndChild, true>;
using ChildWithId = stdex::c_resource<bool, detail::ImGuiBeginIdChild, ImGui::EndChild, true>;

using Disabled   = stdex::c_resource<bool, detail::ImGuiBeginDisabled, ImGui::EndDisabled>;
using TabBar     = stdex::c_resource<bool, ImGui::BeginTabBar, ImGui::EndTabBar>;
using TabItem    = stdex::c_resource<bool, ImGui::BeginTabItem, ImGui::EndTabItem>;
using Group      = stdex::c_resource<void, ImGui::BeginGroup, ImGui::EndGroup, true>;
using Popup      = stdex::c_resource<bool, ImGui::BeginPopup, ImGui::EndPopup>;
using Menu       = stdex::c_resource<bool, ImGui::BeginMenu, ImGui::EndMenu>;
using ModalPopup = stdex::c_resource<bool, ImGui::BeginPopupModal, ImGui::EndPopup>;
using Combo      = stdex::c_resource<bool, ImGui::BeginCombo, ImGui::EndCombo>;
using Table      = stdex::c_resource<bool, ImGui::BeginTable, ImGui::EndTable>;
using ListBox    = stdex::c_resource<bool, ImGui::BeginListBox, ImGui::EndListBox>;
using ToolTip    = stdex::c_resource<bool, ImGui::BeginTooltip, ImGui::EndTooltip>;

using OverrideId  = stdex::c_resource<void, ImGui::PushOverrideID, ImGui::PopID, true>;
using ChangeId    = stdex::c_resource<void, static_cast<void (*)(int)>(ImGui::PushID), ImGui::PopID, true>;
using ChangeStrId = stdex::c_resource<void, static_cast<void (*)(const char*)>(ImGui::PushID), ImGui::PopID, true>;

using DragDropSource = stdex::c_resource<bool, ImGui::BeginDragDropSource, ImGui::EndDragDropSource>;
using DragDropTarget = stdex::c_resource<bool, ImGui::BeginDragDropTarget, ImGui::EndDragDropTarget>;

using Font            = stdex::c_resource<void, static_cast<void (*)(ImFont*)>(ImGui::PushFont), ImGui::PopFont, true>;
using ItemWidth       = stdex::c_resource<void, ImGui::PushItemWidth, ImGui::PopItemWidth, true>;
using StyleColor      = stdex::c_resource<void, static_cast<void (*)(ImGuiCol, const ImVec4&)>(ImGui::PushStyleColor), +detail::singlePop<ImGui::PopStyleColor>(), true>;
using StyleNamedColor = stdex::c_resource<void, static_cast<void (*)(ImGuiCol, ImU32)>(ImGui::PushStyleColor), +detail::singlePop<ImGui::PopStyleColor>(), true>;
using StyleVar        = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, const ImVec2&)>(ImGui::PushStyleVar), +detail::singlePop<ImGui::PopStyleVar>(), true>;
using StyleFloatVar   = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, float)>(ImGui::PushStyleVar), +detail::singlePop<ImGui::PopStyleVar>(), true>;

namespace NodeEditor {
using Editor   = stdex::c_resource<void, ax::NodeEditor::Begin, ax::NodeEditor::End, true>;
using Creation = stdex::c_resource<bool, ax::NodeEditor::BeginCreate, ax::NodeEditor::EndCreate, true>;
using Deletion = stdex::c_resource<bool, ax::NodeEditor::BeginDelete, ax::NodeEditor::EndDelete, true>;
using Node     = stdex::c_resource<void, ax::NodeEditor::BeginNode, ax::NodeEditor::EndNode, true>;
} // namespace NodeEditor

struct PushCursorPosition {
    ImVec2 saved;
    PushCursorPosition() { saved = ImGui::GetCursorScreenPos(); }
    ~PushCursorPosition() { ImGui::SetCursorScreenPos(saved); }
};

/// In places such as the edit pane, we want to wrap widgets to a newline when
/// there is not enough space. But ImGui generally has no concept of min size.
/// Checkboxes, for example, are a constant size. Meanwhile TextInputs will
/// take up 65% of the window by default but can shrink to 1px regardless of
/// their text contents. So, to have a rule when to wrap, we make these min
/// and preferred sizes ourselves.
/// Generally, min(strlen(label), 4) characters is considered the min width for
/// widgets with a label.
struct WidgetSize {
    ImVec2 min;
    ImVec2 preferred;
    // SetNextItemWidth does not account for/affect the label, so subtract this
    float labelPreferredWidth{};

    [[nodiscard]] constexpr WidgetSize normalized() const {
        return {
            .min                 = min,
            .preferred           = ImVec2{std::max(preferred.x, min.x), std::max(preferred.y, min.y)},
            .labelPreferredWidth = labelPreferredWidth,
        };
    }
};

/// Calculate the width and height a button will take up with the current style
/// and a given label. Buttons have a fixed size.
ImVec2 CalcButtonSize(const char* label);

/// Calculates how big a series of buttons will be if they are all drawn on the
/// same line
template<std::size_t N>
requires(N > 0)
inline ImVec2 CalcAdjacentButtonSizes(const std::array<const char*, N>& labels) {
    ImVec2 total{};
    for (const char* label : labels) {
        auto size = CalcButtonSize(label);
        total.x += size.x + ImGui::GetStyle().ItemSpacing.x;
        total.y = std::max(total.y, size.y);
    }
    return total;
}

WidgetSize CalcCheckboxSize(const char* label);
/// Minimum size of a labelled color editor, not the color picker popup
WidgetSize CalcColorEditorSize(const char* label, ImGuiColorEditFlags flags);
/// charsNeeded should be something like floating point precision, plus unit/suffix strlen
WidgetSize CalcSliderSize(const char* label, std::size_t charsNeeded);
/// charsNeeded should be something like floating point precision, plus unit/suffix strlen
WidgetSize CalcDragSize(const char* label, std::size_t charsNeeded);
/// Minimum size of a folded ImGui::BeginCombo() (label + arrow + small preview
/// area). previewValue is only used if ImGuiComboFlags_WidthFitPreview is set
WidgetSize CalcComboSize(const char* label, const char* previewValue, ImGuiComboFlags flags);
/// Preferred minimum size of a single-line text input (room for ~4 characters
/// + label).
WidgetSize CalcTextInputSize(const char* label, const char* contents);

} // namespace DigitizerUi::IMW
#endif
