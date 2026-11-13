#include "StyledStandardWidgets.hpp"

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <imgui_internal.h>
#include <imgui_toggle.h>
#include <imgui_toggle_palette.h>

#include <cstdint>
#include <print>
#include <tuple>
#include <type_traits>
#include <utility>

// this file contains contents adapted from:
// ImGui, https://github.com/ocornut/imgui Copyright (c) 2014-2026 Omar Cornut
// Adobe's imgui-spectrum theme fork https://github.com/adobe/imgui

namespace DigitizerUi {

namespace {

using SpectrumColor = std::uint32_t SpectrumSchemeKeywordColors::*;

constexpr float kSpectrumTreeNodeHighlightAlpha = 0.15f;

ImVec4 spectrumTone(SpectrumColor tone, float alpha = 1.f) {
    ImVec4 color = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(LookAndFeel::instance().spectrumColours().*tone));
    color.w      = alpha;
    return color;
}

ImVec2 buttonFramePadding() { return ImGui::GetStyle().FramePadding * LookAndFeel::instance().buttonFramePaddingMultiplier; }

template<typename T>
auto colorDependingOnInteraction(bool hovered, bool held, T idle, T hover, T press) {
    if (held && hovered) {
        return press;
    } else {
        return hovered ? hover : idle;
    }
}

ImVec4 spectrumSelectedColor(bool hovered, bool held) { return colorDependingOnInteraction(hovered, held, ImGui::GetStyleColorVec4(ImGuiCol_Button), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }

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
    ImGui::RenderArrow(window->DrawList, bb.Min + ImVec2(arrowInset, arrowInset), ImGui::GetColorU32(ButtonContentsColor()), dir, 1.f);

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

// this is a constant copied from the internals of ImGui::SliderBehavior(), it determines how close to the edges of the slider bounding box the grab handle can go
constexpr float kImGuiSliderGrabHandleClampPadding = 2.f;

template<ImGuiNumber T>
[[nodiscard]] std::optional<float> calculateStepCountVisitor(const void* min, const void* max) {
    if constexpr (std::is_integral_v<T>) {
        const T minValue = *static_cast<const T*>(min);
        const T maxValue = *static_cast<const T*>(max);
        return static_cast<float>(minValue < maxValue ? maxValue - minValue : minValue - maxValue);
    } else {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<float> getStepCountIfIntegerType(ImGuiDataType dataType, const void* min, const void* max) {
    return []<typename... Number>(ImGuiDataType matchingDataType, const void* minimum, const void* maximum, std::tuple<Number...>*) {
        std::optional<float> stepCount = 0.f;
        std::ignore                    = ((imguiDataType<Number>() == matchingDataType && (stepCount = calculateStepCountVisitor<Number>(minimum, maximum), true)) || ...);
        return stepCount;
    }(dataType, min, max, static_cast<ImGuiNumberTypes*>(nullptr));
}

/// Draw a slider for editing a numeric value. Only draws the slider and not the text input.
bool sliderImpl([[maybe_unused]] const char* label, ImGuiID id, float width, ImGuiDataType dataType, void* data, const void* min, const void* max, const char* format, ImGuiSliderFlags flags) {
    const ImGuiStyle&  style       = ImGui::GetStyle();
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    const ImRect       frame       = {ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(width, ImGui::GetFrameHeight())};
    ImGui::ItemSize(frame, style.FramePadding.y);
    if (!ImGui::ItemAdd(frame, id)) {
        return false;
    }
    ImGui::ButtonBehavior(frame, id, nullptr, nullptr, ImGuiButtonFlags_PressedOnClick);

    const float floatSliderDragHandleWidth = ImGui::GetFrameHeight() * lookAndFeel.sliderKnobRadiusFrameHeightRatio;
    ImRect      imguiDragRect{}; // where imgui would draw the rectangular drag handle
    const bool  changed = [&] {
        IMW::StyleFloatVar         grabSize(ImGuiStyleVar_GrabMinSize, floatSliderDragHandleWidth * 2.f);
        const float                validDragAreaLength   = frame.GetWidth() - (floatSliderDragHandleWidth * 2.f);
        const std::optional<float> stepCount             = getStepCountIfIntegerType(dataType, min, max);
        const float                grabHandleWidth       = stepCount ? ImMax(validDragAreaLength / *stepCount, floatSliderDragHandleWidth * 2.f) : floatSliderDragHandleWidth * 2.f;
        const float                boundingBoxExtraWidth = ((grabHandleWidth / 2.f) - floatSliderDragHandleWidth) + kImGuiSliderGrabHandleClampPadding;
        const ImRect               boundingBox           = {frame.Min - ImVec2(boundingBoxExtraWidth, 0.f), frame.Max + ImVec2(boundingBoxExtraWidth, 0.f)};
        return ImGui::SliderBehavior(boundingBox, id, dataType, data, min, max, format, flags, &imguiDragRect);
    }();
    if (changed) {
        ImGui::MarkItemEdited(id);
    }

    const bool   isSliderWideEnoughToDrawDragHandle = imguiDragRect.Max.x > imguiDragRect.Min.x; // imgui uses this to indicate invalid or too-small region, so drag handle can't be drawn
    const ImVec2 knobCenter                         = {isSliderWideEnoughToDrawDragHandle ? imguiDragRect.GetCenter().x : frame.Min.x + floatSliderDragHandleWidth, frame.GetCenter().y};
    const float  halfThickness                      = lookAndFeel.sliderTrackThickness / 2.f;
    const ImU32  filledColor                        = ImGui::GetColorU32(ImGuiCol_Button);
    ImDrawList*  drawList                           = ImGui::GetWindowDrawList();

    // draw track lines on either side of drag handle
    if (knobCenter.x - floatSliderDragHandleWidth > frame.Min.x) {
        drawList->AddRectFilled(ImVec2(frame.Min.x, knobCenter.y - halfThickness), ImVec2(knobCenter.x - floatSliderDragHandleWidth, knobCenter.y + halfThickness), filledColor, halfThickness);
    }
    if (knobCenter.x + floatSliderDragHandleWidth < frame.Max.x) {
        drawList->AddRectFilled(ImVec2(knobCenter.x + floatSliderDragHandleWidth, knobCenter.y - halfThickness), ImVec2(frame.Max.x, knobCenter.y + halfThickness), ImGui::GetColorU32(lookAndFeel.palette().widgetTrackInactive), halfThickness);
    }
    ImGui::RenderNavCursor(frame, id);

    drawList->AddCircle(knobCenter, floatSliderDragHandleWidth - (lookAndFeel.sliderKnobRingThickness / 2.f), filledColor, 0, lookAndFeel.sliderKnobRingThickness);

    [[maybe_unused]] ImGuiContext& g = *GImGui;
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, ImGui::GetItemStatusFlags());
    return changed;
}

/// Draw the value of a slider as text, which turns into a text input when right-clicked, Ctrl+clicked, or activated
/// with keyboard navigation. @param requestTextInput turns the text into a text input on the next frame.
/// @return whether the value was changed by typing into the text input
bool sliderNumberLabelOrTextInput(const char* valueLabel, const SliderScalarParameters& parameters, bool allowTextInput, bool requestTextInput) {
    const auto [dataType, data, min, max, format, flags, numberLabelWidth] = parameters;
    ImGuiContext&     g                                                    = *GImGui;
    ImGuiWindow*      window                                               = ImGui::GetCurrentWindow();
    const ImGuiStyle& style                                                = ImGui::GetStyle();
    const ImGuiID     numberLabelId                                        = window->GetID(valueLabel);
    char              text[64];
    ImGui::DataTypeFormatString(text, IM_COUNTOF(text), dataType, data, format);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImRect frame    = {ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + ImVec2(numberLabelWidth, ImGui::GetFrameHeight())};
    ImGui::ItemSize(frame, style.FramePadding.y);
    if (!ImGui::ItemAdd(frame, numberLabelId, nullptr, allowTextInput ? static_cast<ImGuiItemFlags>(ImGuiItemFlags_Inputable) : 0)) {
        return false;
    }

    bool textInputActive = allowTextInput && ImGui::TempInputIsActive(numberLabelId);
    if (allowTextInput && !textInputActive) {
        const bool hovered      = ImGui::ItemHoverable(frame, numberLabelId, g.LastItemData.ItemFlags);
        const bool clickedValue = hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g.IO.KeyCtrl));
        const bool navActivated = g.NavActivateId == numberLabelId && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput) != 0;
        textInputActive         = navActivated;
        if (!navActivated && (requestTextInput || clickedValue)) {
            ImGui::SetKeyboardFocusHere(-1);
        }
    }

    if (textInputActive) {
        // when the text input is focused, it is okay if it gets bigger and covers the slider
        const float  textInputWidth = ImMax(frame.GetWidth(), (LookAndFeel::instance().sliderTextInputMinWidthEms * ImGui::GetFontSize()) + (style.FramePadding.x * 2.f));
        const ImRect textInputFrame = {ImVec2(frame.Max.x - textInputWidth, frame.Min.y), frame.Max};
        if (!ImGui::TempInputScalar(textInputFrame, numberLabelId, valueLabel, dataType, data, format, nullptr, nullptr)) {
            return false;
        }
        ImGui::DataTypeClamp(dataType, data, min, max);
        return true;
    }

    const ImVec2 textPosition = {frame.Min.x + ImMax(0.f, frame.GetWidth() - textSize.x), frame.Min.y + style.FramePadding.y};
    ImGui::RenderTextEllipsis(window->DrawList, textPosition, frame.Max, frame.Max.x, text, nullptr, &textSize);
    ImGui::RenderNavCursor(frame, numberLabelId);
    IMGUI_TEST_ENGINE_ITEM_INFO(numberLabelId, valueLabel, ImGui::GetItemStatusFlags());
    return false;
}

} // namespace

float sliderNumberLabelMaxWidth() { return LookAndFeel::instance().sliderNumberLabelMaxWidthEms * ImGui::GetFontSize(); }

SliderAction SliderScalar(const char* label, const SliderScalarParameters& parameters) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return SliderAction::NotEdited;
    }
    SliderScalarParameters resolved = parameters;
    if (resolved.format == nullptr) {
        resolved.format = ImGui::DataTypeGetInfo(resolved.dataType)->PrintFmt;
    }
    const auto [dataType, data, min, max, format, flags, numberLabelWidth] = resolved;

    const ImGuiStyle& style      = ImGui::GetStyle();
    const ImGuiID     id         = window->GetID(label);
    const float       totalWidth = ImGui::CalcItemWidth();
    const char*       labelEnd   = ImGui::FindRenderedTextEnd(label);

    IMW::Group   group;
    SliderAction action       = sliderImpl(label, id, ImMax(0.f, totalWidth - numberLabelWidth - style.ItemInnerSpacing.x), dataType, data, min, max, format, flags) ? SliderAction::DraggedSlider : SliderAction::NotEdited;
    const bool   rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool   allowTyping  = (flags & ImGuiSliderFlags_NoInput) == 0;

    ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
    {
        IMW::OverrideId scope(id);
        if (sliderNumberLabelOrTextInput("##value", resolved, allowTyping, rightClicked)) {
            action = SliderAction::TypedInTextField;
        }
    }

    if (labelEnd != label) {
        ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
        ImGui::TextUnformatted(label, labelEnd);
    }
    return action;
}

bool Checkbox(const char* label, bool* value) {
    const LookAndFeel&       lookAndFeel            = LookAndFeel::instance();
    const ImVec4             knobColor              = lookAndFeel.palette().toggleKnob;
    const ImVec4             toggleSwitchOffBgColor = lookAndFeel.palette().widgetTrackInactive;
    const ImGuiTogglePalette switchOffPalette       = {
              .Knob        = knobColor,
              .KnobHover   = knobColor,
              .Frame       = toggleSwitchOffBgColor,
              .FrameHover  = ImLerp(toggleSwitchOffBgColor, ImVec4(0.f, 0.f, 0.f, toggleSwitchOffBgColor.w), lookAndFeel.toggleHoveredDarkenPercentage),
              .FrameBorder = {},
              .FrameShadow = {},
              .KnobBorder  = {},
              .KnobShadow  = {},
              .A11yGlyph   = {},
    };
    const ImGuiTogglePalette switchOnPalette = {
        .Knob        = knobColor,
        .KnobHover   = knobColor,
        .Frame       = ImGui::GetStyleColorVec4(ImGuiCol_Button),
        .FrameHover  = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered),
        .FrameBorder = {},
        .FrameShadow = {},
        .KnobBorder  = {},
        .KnobShadow  = {},
        .A11yGlyph   = {},
    };
    return ImGui::Toggle(label, value,
        ImGuiToggleConfig{
            .Flags             = ImGuiToggleFlags_Animated,
            .AnimationDuration = lookAndFeel.toggleAnimationSeconds,
            .WidthRatio        = lookAndFeel.toggleWidthFrameHeightRatio,
            .On                = {.KnobInset = lookAndFeel.toggleKnobInset, .Palette = &switchOnPalette},
            .Off               = {.KnobInset = lookAndFeel.toggleKnobInset, .Palette = &switchOffPalette},
        });
}

float toggleSwitchWidth() { return ImGui::GetFrameHeight() * LookAndFeel::instance().toggleWidthFrameHeightRatio; }

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

ImVec4 ButtonContentsColor() { return LookAndFeel::instance().palette().buttonContents.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)); }

bool Button(const char* label, const ImVec2& size) {
    IMW::StyleColor text(ImGuiCol_Text, ButtonContentsColor());
    return ImGui::Button(label, size);
}

bool SmallButton(const char* label) {
    IMW::StyleColor text(ImGuiCol_Text, ButtonContentsColor());
    return ImGui::SmallButton(label);
}

bool TextButton(const char* label, const ImVec2& size) {
    IMW::StyleVar   framePadding(ImGuiStyleVar_FramePadding, buttonFramePadding());
    IMW::StyleColor text(ImGuiCol_Text, ButtonContentsColor());
    return ImGui::Button(label, size);
}

ImVec2 CalcButtonSize(const char* label) {
    IMW::StyleVar framePadding(ImGuiStyleVar_FramePadding, buttonFramePadding());
    return IMW::CalcButtonSize(label);
}

ImVec4 SelectableBackgroundColor(bool selected, bool hovered, bool held) {
    if (selected && LookAndFeel::instance().theme == LookAndFeel::Theme::Advanced) {
        return spectrumSelectedColor(hovered, held);
    }
    return ImGui::GetStyleColorVec4((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
}

ImVec4 SelectableTextColor(bool selected) { return selected ? ButtonContentsColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text); }

bool Selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
    IMW::StyleColor idle(ImGuiCol_Header, SelectableBackgroundColor(selected, false, false));
    IMW::StyleColor hovered(ImGuiCol_HeaderHovered, SelectableBackgroundColor(selected, true, false));
    IMW::StyleColor held(ImGuiCol_HeaderActive, SelectableBackgroundColor(selected, true, true));
    IMW::StyleColor text(ImGuiCol_Text, SelectableTextColor(selected));
    return ImGui::Selectable(label, selected, flags, size);
}

} // namespace DigitizerUi
