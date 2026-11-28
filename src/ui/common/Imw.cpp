#include "Imw.hpp"
#include "LookAndFeel.hpp"

#include <string>

namespace gr::od::imw {
static ImVec2 framePad() { return ImGui::GetStyle().FramePadding * 2.f; }
static ImVec2 framePadHeight() { return {0.f, ImGui::GetStyle().FramePadding.y * 2.f}; }

// imgui will insert ItemInnerSpacing.x to the total width if a label is present in a frame
static ImVec2 labelPlusSpacing(const ImVec2& labelSize) { //
    return labelSize.x > 0.f ? ImVec2{ImGui::GetStyle().ItemInnerSpacing.x + labelSize.x, labelSize.y} : labelSize;
}

static WidgetSize calculateLabelMinAndPreferredSize(const std::string& label) {
    std::string  visible{label.c_str(), ImGui::FindRenderedTextEnd(label.c_str())};
    const ImVec2 labelSize     = ImGui::CalcTextSize(visible.c_str(), nullptr, true);
    const auto   minlabelWidth = ImGui::CalcTextSize(visible.data(), visible.data() + std::min(visible.size(), LookAndFeel::instance().minVisibleLabelCharacters)).x;
    return WidgetSize{.min = ImVec2{minlabelWidth, labelSize.y}, .preferred = labelSize}.normalized();
}

static WidgetSize calcFramedLabelSize(const std::string& label) {
    const auto [labelMinSize, labelSize, _] = calculateLabelMinAndPreferredSize(label);
    return WidgetSize{
        .min       = labelPlusSpacing(labelMinSize) + framePad(),
        .preferred = labelPlusSpacing(labelSize) + framePad(),
    }
        .normalized();
}

ImVec2 calcButtonSize(const char* label) { return ImGui::CalcTextSize(label) + framePad(); }

ImRect calcGlyphBounds(ImFont* font, float fontSize, std::string_view glyph) {
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, glyph.data(), glyph.data() + glyph.size());
    ImFontBaked*       baked     = font->GetFontBaked(fontSize);
    const ImFontGlyph* fontGlyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
    const float        scale     = fontSize / baked->Size;
    return ImRect{ImVec2{fontGlyph->X0, fontGlyph->Y0} * scale, ImVec2{fontGlyph->X1, fontGlyph->Y1} * scale};
}

WidgetSize calcCheckboxSize(const char* label) {
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

WidgetSize calcComboSize(const char* label, const char* previewValue, ImGuiComboFlags flags) {
    WidgetSize previewSize{};
    if (!(flags & ImGuiComboFlags_NoPreview) && previewValue) {
        previewSize = calcFramedLabelSize(previewValue);
    }

    const float arrowSize                   = (flags & ImGuiComboFlags_NoArrowButton) ? 0.f : ImGui::GetFrameHeight();
    const auto [labelMinSize, labelSize, _] = calculateLabelMinAndPreferredSize(label);
    return WidgetSize{
        .min                 = ImVec2{arrowSize + previewSize.min.x, 0.f} + labelPlusSpacing(labelMinSize) + framePadHeight(),
        .preferred           = ImVec2{arrowSize + previewSize.preferred.x, 0.f} + labelPlusSpacing(labelSize) + framePadHeight(),
        .labelPreferredWidth = labelSize.x,
    }
        .normalized();
}

} // namespace gr::od::imw
