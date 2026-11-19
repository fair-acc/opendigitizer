#include "ImguiWrap.hpp"

#include <array>
#include <string>

namespace DigitizerUi::IMW {
static ImVec2 framePad() { return ImGui::GetStyle().FramePadding * 2.f; }
static ImVec2 framePadHeight() { return {0.f, ImGui::GetStyle().FramePadding.y * 2.f}; }

// imgui will insert ItemInnerSpacing.x to the total width if a label is present in a frame
static ImVec2 labelPlusSpacing(const ImVec2& labelSize) { //
    return labelSize.x > 0.f ? ImVec2{ImGui::GetStyle().ItemInnerSpacing.x + labelSize.x, labelSize.y} : labelSize;
}

static WidgetSize calculateLabelMinAndPreferredSize(const std::string& label) {
    std::string         visible{label.c_str(), ImGui::FindRenderedTextEnd(label.c_str())};
    const ImVec2        labelSize = ImGui::CalcTextSize(visible.c_str(), nullptr, true);
    std::array<char, 5> slice     = {};
    using diff_t                  = decltype(slice.end() - slice.begin());
    const size_t copyLength       = std::min(slice.size() - 1, visible.size());
    std::ranges::copy_n(visible.begin(), static_cast<diff_t>(copyLength), slice.begin());
    assert(slice.back() == '\0');
    const auto minlabelWidth = ImGui::CalcTextSize(slice.data()).x;
    return WidgetSize{.min = ImVec2{minlabelWidth, labelSize.y}, .preferred = labelSize}.normalized();
}

static WidgetSize CalcFramedLabelSize(const std::string& label) {
    const auto [labelMinSize, labelSize, _] = calculateLabelMinAndPreferredSize(label);
    return WidgetSize{
        .min       = labelPlusSpacing(labelMinSize) + framePad(),
        .preferred = labelPlusSpacing(labelSize) + framePad(),
    }
        .normalized();
}

ImVec2 CalcButtonSize(const char* label) { return ImGui::CalcTextSize(label) + framePad(); }

ImRect calcGlyphBounds(ImFont* font, float fontSize, std::string_view glyph) {
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, glyph.data(), glyph.data() + glyph.size());
    ImFontBaked*       baked     = font->GetFontBaked(fontSize);
    const ImFontGlyph* fontGlyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
    const float        scale     = fontSize / baked->Size;
    return ImRect{ImVec2{fontGlyph->X0, fontGlyph->Y0} * scale, ImVec2{fontGlyph->X1, fontGlyph->Y1} * scale};
}

WidgetSize CalcCheckboxSize(const char* label) {
    const auto [labelMinSize, labelSize, _] = calculateLabelMinAndPreferredSize(label);
    const float boxSize                     = ImGui::GetFrameHeight();
    const auto  addSizeToConstantSize       = [boxSize](ImVec2 size) { //
        return ImVec2{boxSize + labelPlusSpacing(size).x, size.y + framePad().y};
    };
    return WidgetSize{
        .min                 = addSizeToConstantSize(labelMinSize),
        .preferred           = addSizeToConstantSize(labelSize),
        .labelPreferredWidth = labelSize.x,
    }
        .normalized();
}

static WidgetSize CalcSliderOrDragSize(const char* label, std::size_t charsNeeded) {
    auto size                      = CalcFramedLabelSize(label);
    auto dragLabelWidth            = ImGui::CalcTextSize("W").x * static_cast<float>(std::max(charsNeeded, std::size_t{1}));
    size.labelPreferredWidth       = size.preferred.x;
    const auto dragAdditionalWidth = dragLabelWidth + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().FramePadding.x * 2.f;
    size.min.x += dragAdditionalWidth;
    size.preferred.x += dragAdditionalWidth;
    return size.normalized();
}

WidgetSize CalcDragSize(const char* label, std::size_t charsNeeded) { return CalcSliderOrDragSize(label, charsNeeded); }

WidgetSize CalcComboSize(const char* label, const char* previewValue, ImGuiComboFlags flags) {
    WidgetSize previewSize{};
    if (!(flags & ImGuiComboFlags_NoPreview) && previewValue) {
        // preview is just a framed label
        previewSize = CalcFramedLabelSize(previewValue);
    }

    // combo size is arrow plus preview box plus label
    const float arrowSize                   = (flags & ImGuiComboFlags_NoArrowButton) ? 0.f : ImGui::GetFrameHeight();
    const auto [labelMinSize, labelSize, _] = calculateLabelMinAndPreferredSize(label);
    return WidgetSize{
        .min                 = ImVec2{arrowSize + previewSize.min.x, 0.f} + labelPlusSpacing(labelMinSize) + framePadHeight(),
        .preferred           = ImVec2{arrowSize + previewSize.preferred.x, 0.f} + labelPlusSpacing(labelSize) + framePadHeight(),
        .labelPreferredWidth = labelSize.x,
    }
        .normalized();
}

} // namespace DigitizerUi::IMW
