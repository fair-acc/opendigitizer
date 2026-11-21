#ifndef OPENDIGITIZER_UI_COMMON_IMW_HPP_
#define OPENDIGITIZER_UI_COMMON_IMW_HPP_

#include <imgui.h>
#include <imgui_internal.h>

#include <c_resource.hpp>

#include <algorithm>
#include <string_view>

namespace gr::od::imw {

namespace detail {
// true when EndDisabled is due
inline bool beginDisabled(bool disabled) {
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

inline auto beginChild(const char* strId, const ImVec2& size = ImVec2(0, 0), ImGuiChildFlags childFlags = 0, ImGuiWindowFlags windowFlags = 0) { return ImGui::BeginChild(strId, size, childFlags, windowFlags); }

template<auto* Fn>
constexpr inline auto singlePop() {
    return [] { Fn(1); };
}
} // namespace detail

using Window = stdex::c_resource<bool, &ImGui::Begin, &ImGui::End, true>;
using Child  = stdex::c_resource<bool, &detail::beginChild, &ImGui::EndChild, true>;

using Disabled   = stdex::c_resource<bool, &detail::beginDisabled, &ImGui::EndDisabled>;
using TabBar     = stdex::c_resource<bool, &ImGui::BeginTabBar, &ImGui::EndTabBar>;
using TabItem    = stdex::c_resource<bool, &ImGui::BeginTabItem, &ImGui::EndTabItem>;
using Group      = stdex::c_resource<void, &ImGui::BeginGroup, &ImGui::EndGroup, true>;
using Popup      = stdex::c_resource<bool, &ImGui::BeginPopup, &ImGui::EndPopup>;
using Menu       = stdex::c_resource<bool, &ImGui::BeginMenu, &ImGui::EndMenu>;
using ModalPopup = stdex::c_resource<bool, &ImGui::BeginPopupModal, &ImGui::EndPopup>;
using Table      = stdex::c_resource<bool, &ImGui::BeginTable, &ImGui::EndTable>;
using ListBox    = stdex::c_resource<bool, &ImGui::BeginListBox, &ImGui::EndListBox>;
using ToolTip    = stdex::c_resource<bool, &ImGui::BeginTooltip, &ImGui::EndTooltip>;

using OverrideId  = stdex::c_resource<void, &ImGui::PushOverrideID, &ImGui::PopID, true>;
using ChangeId    = stdex::c_resource<void, static_cast<void (*)(int)>(ImGui::PushID), &ImGui::PopID, true>;
using ChangeStrId = stdex::c_resource<void, static_cast<void (*)(const char*)>(ImGui::PushID), &ImGui::PopID, true>;

using DragDropSource = stdex::c_resource<bool, &ImGui::BeginDragDropSource, &ImGui::EndDragDropSource>;

using Font            = stdex::c_resource<void, static_cast<void (*)(ImFont*)>(&ImGui::PushFont), &ImGui::PopFont, true>;
using FontWithSize    = stdex::c_resource<void, static_cast<void (*)(ImFont*, float)>(&ImGui::PushFont), &ImGui::PopFont, true>;
using ItemWidth       = stdex::c_resource<void, &ImGui::PushItemWidth, &ImGui::PopItemWidth, true>;
using StyleColor      = stdex::c_resource<void, static_cast<void (*)(ImGuiCol, const ImVec4&)>(ImGui::PushStyleColor), +detail::singlePop<ImGui::PopStyleColor>(), true>;
using StyleNamedColor = stdex::c_resource<void, static_cast<void (*)(ImGuiCol, ImU32)>(ImGui::PushStyleColor), +detail::singlePop<ImGui::PopStyleColor>(), true>;
using StyleVar        = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, const ImVec2&)>(ImGui::PushStyleVar), +detail::singlePop<ImGui::PopStyleVar>(), true>;
using StyleFloatVar   = stdex::c_resource<void, static_cast<void (*)(ImGuiStyleVar, float)>(ImGui::PushStyleVar), +detail::singlePop<ImGui::PopStyleVar>(), true>;

struct PushCursorPosition {
    ImVec2 saved;
    PushCursorPosition() { saved = ImGui::GetCursorScreenPos(); }
    ~PushCursorPosition() { ImGui::SetCursorScreenPos(saved); }
};

/// minimum and preferred widget sizes, which ImGui lacks, decide when the edit pane wraps a widget to a new line:
/// checkboxes have a constant size, text inputs take 65 % of the window by default but shrink to 1 px;
/// a labelled widget needs at least min(strlen(label), 4) characters
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

ImVec2 calcButtonSize(const char* label);

/// the area covered by the drawn pixels of the UTF-8 `glyph`, relative to the position passed to AddText();
/// unlike ImGui::CalcTextSize(), without the advance and the font's ascent and descent
ImRect calcGlyphBounds(ImFont* font, float fontSize, std::string_view glyph);

template<std::size_t N>
requires(N > 0)
inline ImVec2 calcAdjacentButtonSizes(const std::array<const char*, N>& labels, ImVec2 (*calcSize)(const char*) = calcButtonSize) {
    ImVec2     total{};
    const auto spacing = ImGui::GetStyle().ItemSpacing.x;
    for (std::size_t i = 0; i < N; ++i) {
        auto size = calcSize(labels[i]);
        total.x += size.x + (i > 0 ? spacing : 0.f);
        total.y = std::max(total.y, size.y);
    }
    return total;
}

WidgetSize calcCheckboxSize(const char* label);
/// a folded BeginCombo(): label, arrow and a small preview; `previewValue` counts only with ImGuiComboFlags_WidthFitPreview
WidgetSize calcComboSize(const char* label, const char* previewValue, ImGuiComboFlags flags);

} // namespace gr::od::imw
#endif
