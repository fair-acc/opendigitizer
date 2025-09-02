#ifndef OPENDIGITIZER_IMGUI_WRAPPER_HPP
#define OPENDIGITIZER_IMGUI_WRAPPER_HPP

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <c_resource.hpp>

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

using Font            = stdex::c_resource<void, ImGui::PushFont, ImGui::PopFont, true>;
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

} // namespace DigitizerUi::IMW
#endif
