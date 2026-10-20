#include "StyledStandardWidgets.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <imgui_internal.h>

#include <cstdint>
#include <print>
#include <utility>

// this file contains contents adapted from:
// ImGui, https://github.com/ocornut/imgui Copyright (c) 2014-2026 Omar Cornut
// Adobe's imgui-spectrum theme fork https://github.com/adobe/imgui

namespace DigitizerUi {

namespace {

using SpectrumColor = std::uint32_t SpectrumSchemeKeywordColors::*;

constexpr float kSpectrumCheckboxRounding       = 2.f;
constexpr float kSpectrumCheckboxBorderSize     = 2.f;
constexpr float kSpectrumTreeNodeHighlightAlpha = 0.15f;

ImVec4 spectrumTone(SpectrumColor tone, float alpha = 1.f) {
    ImVec4 color = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(LookAndFeel::instance().spectrumColours().*tone));
    color.w      = alpha;
    return color;
}

ImU32 spectrumToneU32(SpectrumColor tone, float alpha = 1.f) { return ImGui::GetColorU32(spectrumTone(tone, alpha)); } // includes the BeginDisabled() alpha

SpectrumColor colorDependingOnInteraction(bool hovered, bool held, SpectrumColor idle, SpectrumColor hover, SpectrumColor press) { return (held && hovered) ? press : hovered ? hover : idle; }

bool spectrumCheckbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    ImGuiContext&     g         = *GImGui;
    const ImGuiStyle& style     = g.Style;
    const ImGuiID     id        = window->GetID(label);
    const ImVec2      labelSize = ImGui::CalcTextSize(label, nullptr, true);

    const float  squareSize = ImGui::GetFrameHeight();
    const ImVec2 position   = window->DC.CursorPos;
    const ImRect totalBoundingBox(position, position + ImVec2(squareSize + (labelSize.x > 0.f ? style.ItemInnerSpacing.x + labelSize.x : 0.f), labelSize.y + style.FramePadding.y * 2.f));
    ImGui::ItemSize(totalBoundingBox, style.FramePadding.y);
    const bool isVisible     = ImGui::ItemAdd(totalBoundingBox, id);
    const bool isMultiSelect = (g.LastItemData.ItemFlags & ImGuiItemFlags_IsMultiSelect) != 0;
    if (!isVisible && (!isMultiSelect || !g.BoxSelectState.UnclipMode || !g.BoxSelectState.UnclipRect.Overlaps(totalBoundingBox))) {
        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
        return false;
    }

    bool checked = *v;
    if (isMultiSelect) {
        ImGui::MultiSelectItemHeader(id, &checked, nullptr);
    }
    bool hovered = false;
    bool held    = false;
    bool pressed = ImGui::ButtonBehavior(totalBoundingBox, id, &hovered, &held);
    if (isMultiSelect) {
        ImGui::MultiSelectItemFooter(id, &checked, &pressed);
    } else if (pressed) {
        checked = !checked;
    }
    if (*v != checked) {
        *v      = checked;
        pressed = true;
        ImGui::MarkItemEdited(id);
    }

    const ImRect checkBoundingBox(position, position + ImVec2(squareSize, squareSize));
    const ImRect boxBoundingBox(checkBoundingBox.Min + ImVec2(style.FramePadding.y, style.FramePadding.y), checkBoundingBox.Max - ImVec2(style.FramePadding.y, style.FramePadding.y));
    const bool   mixedValue = (g.LastItemData.ItemFlags & ImGuiItemFlags_MixedValue) != 0;
    if (isVisible) {
        ImGui::RenderNavCursor(totalBoundingBox, id);
        const ImU32 fillColor = spectrumToneU32(colorDependingOnInteraction(hovered, held, &SpectrumSchemeKeywordColors::blue500, &SpectrumSchemeKeywordColors::blue600, &SpectrumSchemeKeywordColors::blue700));
        const ImU32 markColor = spectrumToneU32(&SpectrumSchemeKeywordColors::gray50);
        if (mixedValue) {
            window->DrawList->AddRectFilled(boxBoundingBox.Min, boxBoundingBox.Max, fillColor, kSpectrumCheckboxRounding);
            const float pad = ImMax(1.f, IM_TRUNC(squareSize / 3.6f));
            window->DrawList->AddRectFilled(boxBoundingBox.Min + ImVec2(pad, pad), boxBoundingBox.Max - ImVec2(pad, pad), markColor, style.FrameRounding);
        } else if (*v) {
            window->DrawList->AddRectFilled(boxBoundingBox.Min, boxBoundingBox.Max, fillColor, kSpectrumCheckboxRounding);
            const float pad = ImMax(1.f, IM_TRUNC(squareSize / 4.f));
            ImGui::RenderCheckMark(window->DrawList, checkBoundingBox.Min + ImVec2(pad, pad), markColor, squareSize - pad * 2.f);
        } else {
            const ImU32 borderColor = spectrumToneU32(colorDependingOnInteraction(hovered, held, &SpectrumSchemeKeywordColors::gray600, &SpectrumSchemeKeywordColors::gray700, &SpectrumSchemeKeywordColors::gray800));
            window->DrawList->AddRect(boxBoundingBox.Min, boxBoundingBox.Max, borderColor, kSpectrumCheckboxRounding, ImDrawFlags_None, kSpectrumCheckboxBorderSize);
        }
    }
    if (isVisible && labelSize.x > 0.f) {
        ImGui::RenderText(ImVec2(checkBoundingBox.Max.x + style.ItemInnerSpacing.x, checkBoundingBox.Min.y + style.FramePadding.y), label);
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable | (*v ? ImGuiItemStatusFlags_Checked : 0));
    return pressed;
}

bool spectrumRadioButton(const char* label, bool active) {
#ifndef NDEBUG
    // To avoid duplicating stuff from ImGui, this is not provided because we don't use it (yet).
    // But maybe in the future having this is worth it for visual consistency.
    // But radio buttons are kind of a UX anti-pattern anyways... try a combobox?
    std::println("spectrum-themed radio button not implemented");
#endif
    return ImGui::RadioButton(label, active);
}

bool spectrumArrowButton(const char* strId, ImGuiDir dir) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    ImGuiContext& g    = *GImGui;
    const float   size = ImGui::GetFrameHeight();
    const ImGuiID id   = window->GetID(strId);
    const ImRect  bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(size, size));
    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) {
        return false;
    }

    bool        hovered         = false;
    bool        held            = false;
    const bool  pressed         = ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_None);
    const ImU32 backgroundColor = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderNavCursor(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, backgroundColor, true, g.Style.FrameRounding);
    const float arrowInset = ImMax(0.f, (size - g.FontSize) * 0.5f);
    ImGui::RenderArrow(window->DrawList, bb.Min + ImVec2(arrowInset, arrowInset), ImGui::GetColorU32(ImGuiCol_Text), dir, 1.f);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, strId, g.LastItemData.StatusFlags);
    return pressed;
}

bool spectrumTreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) {
    const bool      isFramed    = (flags & ImGuiTreeNodeFlags_Framed) != 0;
    const float     headerAlpha = isFramed ? 1.f : kSpectrumTreeNodeHighlightAlpha;
    IMW::StyleColor header(ImGuiCol_Header, spectrumTone(&SpectrumSchemeKeywordColors::blue400, headerAlpha));
    IMW::StyleColor headerHovered(ImGuiCol_HeaderHovered, spectrumTone(&SpectrumSchemeKeywordColors::blue500, headerAlpha));
    IMW::StyleColor headerActive(ImGuiCol_HeaderActive, spectrumTone(&SpectrumSchemeKeywordColors::blue600, headerAlpha));
    IMW::StyleColor text(ImGuiCol_Text, isFramed ? spectrumTone(&SpectrumSchemeKeywordColors::gray50) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    return ImGui::TreeNodeEx(label, flags);
}

} // namespace

bool Checkbox(const char* label, bool* v) {
    switch (LookAndFeel::instance().theme) {
    case LookAndFeel::Theme::Classic: return ImGui::Checkbox(label, v);
    case LookAndFeel::Theme::Advanced: return spectrumCheckbox(label, v);
    }
    std::unreachable();
}

bool RadioButton(const char* label, bool active) {
    switch (LookAndFeel::instance().theme) {
    case LookAndFeel::Theme::Classic: return ImGui::RadioButton(label, active);
    case LookAndFeel::Theme::Advanced: return spectrumRadioButton(label, active);
    }
    std::unreachable();
}

bool RadioButton(const char* label, int* v, int vButton) {
    const bool pressed = RadioButton(label, *v == vButton);
    if (pressed) {
        *v = vButton;
    }
    return pressed;
}

bool ArrowButton(const char* strId, ImGuiDir dir) {
    switch (LookAndFeel::instance().theme) {
    case LookAndFeel::Theme::Classic: return ImGui::ArrowButton(strId, dir);
    case LookAndFeel::Theme::Advanced: return spectrumArrowButton(strId, dir);
    }
    std::unreachable();
}

bool BeginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags) { return ImGui::BeginCombo(label, previewValue, flags); }

bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) {
    switch (LookAndFeel::instance().theme) {
    case LookAndFeel::Theme::Classic: return ImGui::TreeNodeEx(label, flags);
    case LookAndFeel::Theme::Advanced: return spectrumTreeNodeEx(label, flags);
    }
    std::unreachable();
}

} // namespace DigitizerUi
