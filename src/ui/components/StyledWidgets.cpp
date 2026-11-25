#include "StyledWidgets.hpp"

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "SheetPopup.hpp"
#include "SwipeGesture.hpp"

#include <imgui_internal.h>
#include <imgui_toggle.h>
#include <imgui_toggle_palette.h>

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

// this file contains contents adapted from:
// ImGui, https://github.com/ocornut/imgui Copyright (c) 2014-2026 Omar Cornut
// Adobe's imgui-spectrum theme fork https://github.com/adobe/imgui

namespace gr::od {

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

bool spectrumTreeNodeEx(const char* label, ImGuiTreeNodeFlags flags) {
    const bool      isFramed    = (flags & ImGuiTreeNodeFlags_Framed) != 0;
    const float     headerAlpha = isFramed ? 1.f : kSpectrumTreeNodeHighlightAlpha;
    imw::StyleColor header(ImGuiCol_Header, spectrumTone(&SpectrumSchemeKeywordColors::blue400, headerAlpha));
    imw::StyleColor headerHovered(ImGuiCol_HeaderHovered, spectrumTone(&SpectrumSchemeKeywordColors::blue500, headerAlpha));
    imw::StyleColor headerActive(ImGuiCol_HeaderActive, spectrumTone(&SpectrumSchemeKeywordColors::blue600, headerAlpha));
    imw::StyleColor text(ImGuiCol_Text, isFramed ? spectrumTone(&SpectrumSchemeKeywordColors::gray50) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
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

/// the slider only, without the text input
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
        imw::StyleFloatVar         grabSize(ImGuiStyleVar_GrabMinSize, floatSliderDragHandleWidth * 2.f);
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
    if (ImGui::IsItemActive()) {
        suppressSwipeGesture();
    }

    const bool   isSliderWideEnoughToDrawDragHandle = imguiDragRect.Max.x > imguiDragRect.Min.x; // imgui uses this to indicate invalid or too-small region, so drag handle can't be drawn
    const ImVec2 knobCenter                         = {isSliderWideEnoughToDrawDragHandle ? imguiDragRect.GetCenter().x : frame.Min.x + floatSliderDragHandleWidth, frame.GetCenter().y};
    const float  halfThickness                      = lookAndFeel.sliderTrackThickness / 2.f;
    const ImU32  filledColor                        = ImGui::GetColorU32(ImGuiCol_Button);
    ImDrawList*  drawList                           = ImGui::GetWindowDrawList();

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

/// the value as text, which becomes a text input when right-clicked, Ctrl+clicked or activated by keyboard navigation, and with `requestTextInput` on the next frame;
/// true when typing into the text input changed the value
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

float comboChevronWidth() { return LookAndFeel::instance().comboChevronWidthEms * ImGui::GetFontSize(); }

/// since we hide the title bar of popups we use this to just draw the visible text of the popup ID as centered text at the top of the popup
void drawPopupTitle(const char* id) {
    const char* titleEnd = ImGui::FindRenderedTextEnd(id);
    if (titleEnd == id) {
        return;
    }
    const float titleWidth = ImGui::CalcTextSize(id, titleEnd).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, .5f * (ImGui::GetContentRegionAvail().x - titleWidth)));
    ImGui::TextUnformatted(id, titleEnd);
}

} // namespace

float sliderNumberLabelMaxWidth() { return LookAndFeel::instance().sliderNumberLabelMaxWidthEms * ImGui::GetFontSize(); }

SliderAction sliderScalar(const char* label, const SliderScalarParameters& parameters) {
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

    imw::Group   group;
    SliderAction action       = sliderImpl(label, id, ImMax(0.f, totalWidth - numberLabelWidth - style.ItemInnerSpacing.x), dataType, data, min, max, format, flags) ? SliderAction::DraggedSlider : SliderAction::NotEdited;
    const bool   rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool   allowTyping  = (flags & ImGuiSliderFlags_NoInput) == 0;

    ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
    {
        imw::OverrideId scope(id);
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

bool checkbox(const char* label, bool* value) {
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

float comboWidthForText(float textWidth) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImMin(textWidth, LookAndFeel::instance().comboTextMaxWidthEms * ImGui::GetFontSize()) + comboChevronWidth() + style.ItemInnerSpacing.x + 2.f * style.FramePadding.x;
}

namespace {
/// Generate an ID from a label in the same way that ImGui::BeginCombo(const char* label) generates the ID for its popup
[[nodiscard]] ImGuiID comboPopupId(const char* label) {
    imw::OverrideId comboScope(ImGui::GetID(label));
    return ImGui::GetID("##ComboPopup");
}

/// This imitates what BeginCombo does to draw its text and the button that you press to open the dropdown popup, but does not actually open any popup
/// @returns whether the item was clicked
[[nodiscard]] bool openComboTextAndButtonBehavior(const char* label, const ImRect& frame) {
    const ImGuiStyle& style      = ImGui::GetStyle();
    const ImVec2      labelSize  = ImGui::CalcTextSize(label, nullptr, true);
    const float       labelWidth = labelSize.x > 0.f ? style.ItemInnerSpacing.x + labelSize.x : 0.f;
    const bool        isClicked  = ImGui::InvisibleButton(label, ImVec2{frame.GetWidth() + labelWidth, frame.GetHeight()});
    if (labelSize.x > 0.f) {
        ImGui::RenderText(ImVec2{frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y}, label);
    }
    return isClicked;
}
} // namespace

bool beginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags) {
    ImGuiWindow* window          = ImGui::GetCurrentWindow();
    const bool   usesSheetPopups = LookAndFeel::instance().usesSheetPopups();
    if (window->SkipItems) {
        return false;
    }
    if ((flags & ImGuiComboFlags_NoPreview) != 0) {
        if (!usesSheetPopups) {
            return ImGui::BeginCombo(label, previewValue, flags);
        }
        previewValue = nullptr;
    }
    const ImGuiStyle&  style       = ImGui::GetStyle();
    const LookAndFeel& lookAndFeel = LookAndFeel::instance();
    const char*        previewEnd  = previewValue ? ImGui::FindRenderedTextEnd(previewValue) : nullptr;
    if ((flags & ImGuiComboFlags_WidthFitPreview) != 0) {
        ImGui::SetNextItemWidth(comboWidthForText(previewValue ? ImGui::CalcTextSize(previewValue, previewEnd).x : 0.f));
    }
    const ImRect frame    = {window->DC.CursorPos, window->DC.CursorPos + ImVec2(ImGui::CalcItemWidth(), ImGui::GetFrameHeight())};
    const float  rounding = ImGui::GetFrameHeight() * lookAndFeel.sliderKnobRadiusFrameHeightRatio;

    const bool open = [&] {
        if (usesSheetPopups) {
            const ImGuiID popupId = comboPopupId(label);
            if (openComboTextAndButtonBehavior(label, frame)) {
                ImGui::OpenPopup(popupId);
            }
            return beginPopupSheetEx(popupId, PopupFlags::WantsExclusiveFocus);
        }
        // remove everything besides the text and behavior because we draw the rest of the combobox ourselves
        imw::StyleColor    frameBackground(ImGuiCol_FrameBg, ImVec4{});
        imw::StyleColor    frameBackgroundHovered(ImGuiCol_FrameBgHovered, ImVec4{});
        imw::StyleColor    popupBorder(ImGuiCol_Border, lookAndFeel.palette().widgetTrackInactive);
        imw::StyleFloatVar frameBorder(ImGuiStyleVar_FrameBorderSize, 0.f);
        imw::StyleFloatVar popupBorderSize(ImGuiStyleVar_PopupBorderSize, lookAndFeel.comboOutlineThickness);
        imw::StyleFloatVar popupRounding(ImGuiStyleVar_PopupRounding, rounding);
        return ImGui::BeginCombo(label, nullptr, (flags & ~ImGuiComboFlags_WidthFitPreview) | ImGuiComboFlags_NoArrowButton);
    }();
    if (!open && !ImGui::IsItemVisible()) {
        return false;
    }
    const bool  hovered      = !open && ImGui::IsItemHovered();
    const ImU32 outlineColor = open ? ImGui::GetColorU32(ImGuiCol_Button) : hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) : ImGui::GetColorU32(lookAndFeel.palette().widgetTrackInactive);
    const float outlineInset = lookAndFeel.comboOutlineThickness / 2.f;
    window->DrawList->AddRect(frame.Min + ImVec2(outlineInset, outlineInset), frame.Max - ImVec2(outlineInset, outlineInset), outlineColor, rounding, ImDrawFlags_None, lookAndFeel.comboOutlineThickness);

    const float  chevronHalfWidth = comboChevronWidth() / 2.f;
    const ImVec2 chevronCenter    = {frame.Max.x - style.FramePadding.x - chevronHalfWidth, frame.GetCenter().y};
    const ImVec2 chevron[]        = {chevronCenter + ImVec2(-chevronHalfWidth, -chevronHalfWidth / 2.f), chevronCenter + ImVec2(0.f, chevronHalfWidth / 2.f), chevronCenter + ImVec2(chevronHalfWidth, -chevronHalfWidth / 2.f)};
    window->DrawList->AddPolyline(chevron, IM_COUNTOF(chevron), ImGui::GetColorU32(ImGuiCol_Button), ImDrawFlags_None, lookAndFeel.comboChevronThickness);

    if (previewValue) {
        const float valueMaxX = chevronCenter.x - chevronHalfWidth - style.ItemInnerSpacing.x;
        ImGui::RenderTextEllipsis(window->DrawList, frame.Min + style.FramePadding, ImVec2(valueMaxX, frame.Max.y), valueMaxX, previewValue, previewEnd, nullptr);
    }
    return open;
}

void endCombo() {
    if (LookAndFeel::instance().usesSheetPopups()) {
        endPopupSheet();
    } else {
        ImGui::EndCombo();
    }
}

bool treeNodeEx(const char* label, ImGuiTreeNodeFlags flags) {
    switch (LookAndFeel::instance().theme) {
    case LookAndFeel::Theme::Classic: return ImGui::TreeNodeEx(label, flags);
    case LookAndFeel::Theme::Advanced: return spectrumTreeNodeEx(label, flags);
    }
    std::unreachable();
}

ImVec4 buttonContentsColor() { return LookAndFeel::instance().palette().buttonContents.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)); }

bool button(const char* label, const ImVec2& size) {
    imw::StyleColor text(ImGuiCol_Text, buttonContentsColor());
    return ImGui::Button(label, size);
}

bool smallButton(const char* label) {
    imw::StyleColor text(ImGuiCol_Text, buttonContentsColor());
    return ImGui::SmallButton(label);
}

bool textButton(const char* label, const ImVec2& size) {
    imw::StyleVar   framePadding(ImGuiStyleVar_FramePadding, buttonFramePadding());
    imw::StyleColor text(ImGuiCol_Text, buttonContentsColor());
    return ImGui::Button(label, size);
}

ImVec2 calcTextButtonSize(const char* label) {
    imw::StyleVar framePadding(ImGuiStyleVar_FramePadding, buttonFramePadding());
    return imw::calcButtonSize(label);
}

ImVec4 selectableBackgroundColor(bool selected, bool hovered, bool held) {
    if (selected && LookAndFeel::instance().theme == LookAndFeel::Theme::Advanced) {
        return spectrumSelectedColor(hovered, held);
    }
    return ImGui::GetStyleColorVec4((held && hovered) ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
}

ImVec4 selectableTextColor(bool selected) { return selected ? buttonContentsColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text); }

bool selectable(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size) {
    imw::StyleColor idle(ImGuiCol_Header, selectableBackgroundColor(selected, false, false));
    imw::StyleColor hovered(ImGuiCol_HeaderHovered, selectableBackgroundColor(selected, true, false));
    imw::StyleColor held(ImGuiCol_HeaderActive, selectableBackgroundColor(selected, true, true));
    imw::StyleColor text(ImGuiCol_Text, selectableTextColor(selected));
    const bool      isClicked     = ImGui::Selectable(label, selected, flags, size);
    const bool      isAutoClosing = (flags & ImGuiSelectableFlags_NoAutoClosePopups) == 0 && (ImGui::GetItemFlags() & ImGuiItemFlags_AutoClosePopups) != 0;
    if (isClicked && isAutoClosing && isCurrentWindowSheetContents()) {
        closeCurrentPopupSheet();
    }
    return isClicked;
}

bool menuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    const bool isClicked     = ImGui::MenuItem(label, shortcut, selected, enabled);
    const bool isAutoClosing = (ImGui::GetItemFlags() & ImGuiItemFlags_AutoClosePopups) != 0;
    if (isClicked && isAutoClosing && isCurrentWindowSheetContents()) {
        closeCurrentPopupSheet();
    }
    return isClicked;
}

namespace {
[[nodiscard]] ImGuiID popupCloseRequestStorageId(ImGuiID popupId) {
    imw::OverrideId popupScope(popupId);
    return ImGui::GetID("##isCloseRequested");
}

bool beginDesktopPopup(const char* id, const PopupOptions& options) {
    const bool isCloseRequested = detail::takePopupCloseRequest(ImGui::GetID(id));
    const bool isOpen           = hasFlag(options.flags, PopupFlags::Modal) ? ImGui::BeginPopupModal(id, nullptr, options.desktopWindowFlags | ImGuiWindowFlags_NoTitleBar) : ImGui::BeginPopup(id, options.desktopWindowFlags);
    if (isOpen && isCloseRequested) {
        ImGui::CloseCurrentPopup();
    }
    return isOpen;
}
} // namespace

bool beginPopup(const char* id, const PopupOptions& options) {
    const bool isModal = hasFlag(options.flags, PopupFlags::Modal);
    const bool isOpen  = LookAndFeel::instance().usesSheetPopups() ? beginPopupSheet(id, options.flags) : beginDesktopPopup(id, options);
    if (isOpen && isModal) {
        drawPopupTitle(id);
    }
    return isOpen;
}

void endPopup() {
    if (LookAndFeel::instance().usesSheetPopups()) {
        endPopupSheet();
    } else {
        ImGui::EndPopup();
    }
}

void closeCurrentPopup() {
    if (isCurrentPopupASheet()) {
        closeCurrentPopupSheet();
    } else {
        ImGui::CloseCurrentPopup();
    }
}

bool beginMenu(const char* label, const char* icon) {
    // ImGui has no public API for the item flags of the next item afaict
    const bool isEnabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0;
    if (LookAndFeel::instance().usesSheetPopups()) {
        return beginPopupSheetMenu(label, icon, isEnabled);
    }
    if (icon != nullptr) {
        {
            imw::Font iconFont(LookAndFeel::instance().fontIconsSolid);
            ImGui::TextUnformatted(icon);
        }
        ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
    }
    return ImGui::BeginMenu(label, isEnabled);
}

void endMenu() {
    if (LookAndFeel::instance().usesSheetPopups()) {
        endPopupSheetMenu();
    } else {
        ImGui::EndMenu();
    }
}

void closePopup(const char* id) { ImGui::GetStateStorage()->SetBool(popupCloseRequestStorageId(ImGui::GetID(id)), true); }

bool colorEdit3(const char* label, float color[3], ImGuiColorEditFlags flags) {
    if (!LookAndFeel::instance().usesSheetPopups()) {
        return ImGui::ColorEdit3(label, color, flags);
    }
    constexpr const char* kPickerPopupId = "picker";
    imw::ChangeStrId      colorEditScope(label);
    // reimplement a bit of ColorEdit3 since we need to customize the latter portion (the popup)
    {
        imw::Group colorEdit;
        if (ImGui::ColorButton("##ColorButton", ImVec4{color[0], color[1], color[2], 1.f}, flags | ImGuiColorEditFlags_NoAlpha)) {
            ImGui::OpenPopup(kPickerPopupId);
        }
        if (const char* labelEnd = ImGui::FindRenderedTextEnd(label); labelEnd != label) {
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(label, labelEnd);
        }
    }
    constexpr ImGuiColorEditFlags kForwardedPickerFlags = ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_ | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoAlpha;
    const ImGuiColorEditFlags     pickerFlags           = (flags & kForwardedPickerFlags) | ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_NoLabel;
    bool                          isChanged             = false;
    if (auto popup = Popup(kPickerPopupId, PopupOptions{.flags = PopupFlags::None})) {
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * LookAndFeel::instance().colorPickerWidthFrameHeightRatio);
        isChanged = ImGui::ColorPicker3("##picker", color, pickerFlags);
        if (ImGui::IsItemActive()) {
            suppressSwipeGesture();
        }
    }
    return isChanged;
}

namespace detail {
bool isPopupAnimating(const char* id) { return LookAndFeel::instance().usesSheetPopups() && isPopupSheetAnimating(id); }

bool takePopupCloseRequest(ImGuiID popupId) {
    ImGuiStorage& storage          = *ImGui::GetStateStorage();
    const ImGuiID storageId        = popupCloseRequestStorageId(popupId);
    const bool    isCloseRequested = storage.GetBool(storageId, false);
    storage.SetBool(storageId, false);
    return isCloseRequested;
}
} // namespace detail

} // namespace gr::od
