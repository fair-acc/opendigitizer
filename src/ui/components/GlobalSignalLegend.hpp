#ifndef OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
#define OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP

#include "../GraphModel.hpp"
#include "../charts/Chart.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include "AutoHide.hpp"

#include <implot.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::od {

/// every SinkRegistry sink: click toggles, right-click edits colour and settings, drag to charts and drop from charts
struct GlobalSignalLegend {
    enum class ClickResult { None, Left, RightColor, RightName };

    /// the buttons for every signal are submitted inside this ID scope (but the Add Signal button is not)
    static constexpr const char* kIdScope = "GlobalSignalLegend";

    struct Layout {
        float                      fontSize      = 0.f;
        float                      maxLabelWidth = std::numeric_limits<float>::max();
        std::vector<ImRect>        itemRects{};
        ImVec2                     size{0.f, 0.f};
        std::vector<UiGraphBlock*> sinks{}; // the signals, in the order of itemRects

        [[nodiscard]] bool isSmallerThan(ImVec2 maxSize) const noexcept { return size.x <= maxSize.x && size.y <= maxSize.y; }
    };

    ImVec2      _legendSize{0.f, 0.f};
    std::string _editingSinkUniqueName;
    bool        _openColorPopup  = false;
    bool        _dragDropEnabled = true;

    void setDragDropEnabled(bool isNowEnabled) { _dragDropEnabled = isNowEnabled; }

    [[nodiscard]] ImVec2 legendSize() const noexcept { return _legendSize; }

    [[nodiscard]] static ImVec2 legendInnerPadding(float scale) { return ImPlot::GetStyle().LegendInnerPadding * scale; } // mimic implot legends
    [[nodiscard]] static float  legendItemHeight(float scale = 1.f) { return ImGui::GetTextLineHeight() + 2.f * legendInnerPadding(scale).y; }
    [[nodiscard]] static ImVec2 legendColorRectSize() { return {ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()}; }

    [[nodiscard]] static float labelWidth(std::string_view text, float maxLabelWidth) { return std::min(ImGui::CalcTextSize(text.data(), text.data() + text.size()).x, maxLabelWidth); }

    [[nodiscard]] static ImVec2 legendItemSize(std::string_view text, float maxLabelWidth, float scale) {
        const ImVec2 innerPad = legendInnerPadding(scale);
        return {innerPad.x + legendColorRectSize().x + labelWidth(text, maxLabelWidth) + (2.f * innerPad.x), legendItemHeight(scale)};
    }

    [[nodiscard]] static std::string sinkLabel(const UiGraphBlock& sink) {
        const std::string signalName = sink.blockSettings.value_or<std::string>("signal_name", std::string{});
        return signalName.empty() ? sink.blockSettings.value_or<std::string>("name", std::string{}) : signalName;
    }

    /// calculate where each item of the signal legend would go, at the given size parameters. does not do any intelligent fitting beyond wrapping items
    [[nodiscard]] static Layout layoutAtGivenFixedSize(std::span<const std::string> labels, bool withAddSignalButton, float fontSize, float maxLabelWidth, float maxWidth, bool drawAsColumn) {
        const float       scale = fontSize / ImGui::GetFontSize();
        imw::FontWithSize font(ImGui::GetFont(), fontSize);
        const ImVec2      spacing     = ImGui::GetStyle().ItemSpacing * scale;
        const float       itemHeight  = legendItemHeight(scale);
        Layout            layout      = {.fontSize = fontSize, .maxLabelWidth = maxLabelWidth};
        ImVec2            cursor      = {0.f, 0.f};
        const auto        addToLayout = [&cursor, &layout, itemHeight, maxWidth, spacing](float itemWidth, bool startsRow) {
            if (cursor.x > 0.f && (startsRow || cursor.x + itemWidth > maxWidth)) {
                cursor = ImVec2{0.f, cursor.y + itemHeight + spacing.y};
            }
            layout.itemRects.emplace_back(cursor, cursor + ImVec2{itemWidth, itemHeight});
            layout.size = ImMax(layout.size, layout.itemRects.back().Max);
            cursor.x += itemWidth + spacing.x;
        };
        for (const std::string& label : labels) {
            addToLayout(legendItemSize(label, maxLabelWidth, scale).x, drawAsColumn);
        }
        if (withAddSignalButton) {
            addToLayout(itemHeight, false);
        }
        return layout;
    }

    static ClickResult drawLegendItem(std::uint32_t color, std::string_view text, bool enabled = true, float maxLabelWidth = std::numeric_limits<float>::max(), float scale = 1.f) {
        ClickResult                result = ClickResult::None;
        constexpr ImGuiMouseButton kRMB   = ImGuiMouseButton_Right;

        imw::StyleVar     resetSpacing(ImGuiStyleVar_ItemSpacing, ImVec2{}); // no space between the swatch and the name
        const ImVec2      innerPad      = legendInnerPadding(scale);
        const ImVec2      colorRectSize = legendColorRectSize();
        const ImGuiStyle& style         = ImGui::GetStyle();
        const ImVec2      totalSize     = legendItemSize(text, maxLabelWidth, scale);
        const ImVec2      groupMin      = ImGui::GetCursorScreenPos();
        const ImVec2      groupMax      = groupMin + totalSize;
        const bool        groupHovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(groupMin, groupMax);
        const Palette&    palette       = LookAndFeel::instance().palette();
        const ImVec4      groupBg       = [&palette, groupHovered] {
            if (!groupHovered) {
                return palette.legendButton;
            } else {
                return ImGui::IsMouseDown(ImGuiMouseButton_Left) ? palette.legendButtonActive : palette.legendButtonHovered;
            }
        }();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(groupMin, groupMax, ImGui::GetColorU32(groupBg), style.FrameRounding);
        if (style.FrameBorderSize > 0.f) {
            drawList->AddRect(groupMin, groupMax, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding, ImDrawFlags_None, style.FrameBorderSize);
        }

        const DashboardStyle& dashboardStyle           = LookAndFeel::instance().dashboardStyle;
        const float           colorRectBorderThickness = dashboardStyle.legendColorRectBorderThickness;
        {
            imw::PushCursorPosition cursorRestore;
            ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos() + innerPad);
            if (ImGui::InvisibleButton("##ColorBox", colorRectSize)) {
                result = ClickResult::Left;
            }
        }

        const ImVec2 colorRectMin = groupMin + ImVec2{innerPad.x, .5f * (totalSize.y - colorRectSize.y)};
        const ImVec2 colorRectMax = colorRectMin + colorRectSize;
        drawList->AddRectFilled(colorRectMin, colorRectMax, rgbToImGuiABGR(color, enabled ? 0xFF : 0x88), style.FrameRounding);
        drawList->AddRect(colorRectMin, colorRectMax, rgbToImGuiABGR(color), style.FrameRounding, ImDrawFlags_None, colorRectBorderThickness);

        const bool colorRectHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        if (colorRectHovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightColor;
            }
        }

        if (ImGui::InvisibleButton(text.data(), totalSize)) {
            result = ClickResult::Left;
        }
        const ImVec2 labelMin = groupMin + ImVec2{innerPad.x + colorRectSize.x, 0.f} + innerPad;
        const ImVec2 labelMax = labelMin + ImVec2{labelWidth(text, maxLabelWidth), ImGui::GetTextLineHeight()};
        if (labelMax.x > labelMin.x) {
            imw::StyleColor textColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            ImGui::RenderTextEllipsis(drawList, labelMin, labelMax, labelMax.x, text.data(), text.data() + text.size(), nullptr);
        }
        if (!colorRectHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightName;
            }
        }

        return result;
    }

    /// Returns true if the block was right-clicked
    bool drawSink(UiGraphBlock& sink, int index, float maxLabelWidth, float scale) {
        imw::ChangeId itemId(index);

        const auto        color      = sink.blockSettings.value_or<std::uint32_t>("color", std::uint32_t{0});
        const auto        visible    = sink.blockSettings.value_or<bool>("visible", true);
        const auto&       uniqueName = sink.blockUniqueName;
        const std::string label      = sinkLabel(sink);

        // draw the legend item
        auto clickResult = drawLegendItem(color, label, visible, maxLabelWidth, scale);
        if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
            imw::Font tooltipFont(LookAndFeel::instance().fontNormal());
            ImGui::SetTooltip("%s", _dragDropEnabled ? "Drag/Toggle Signal" : "Toggle Signal");
        }

        if (clickResult == ClickResult::RightColor) {
            _editingSinkUniqueName = std::string(uniqueName);
            _openColorPopup        = true;
        }
        if (clickResult == ClickResult::Left) {
            sink.setSetting("visible", !visible);
        }

        // drag source - from global legend (empty source_chart_id = no removal needed)
        if (_dragDropEnabled) {
            if (auto dndSource = imw::DragDropSource(ImGuiDragDropFlags_None)) {
                using namespace charts;
                if (const auto& signalSink = SinkRegistry::instance().getSink(sink.blockUniqueName)) {
                    dnd::setupPayload(signalSink, {});
                    drawLegendItem(color, label, visible, maxLabelWidth, scale);
                }
            }
        }
        return clickResult == ClickResult::RightName;
    }

    /// Decide the layout of the signal legend items before drawing.
    /// @param drawAsColumn whether to insert a newline after every item
    [[nodiscard]] static Layout fitLayout(UiGraphModel& graphModel, ImVec2 maxSize, bool drawAsColumn, bool withAddSignalButton) {
        std::vector<UiGraphBlock*> sinks = graphModel.recursiveGatherPlotSinks();
        std::vector<std::string>   labels;
        for (const UiGraphBlock* sink : sinks) {
            labels.push_back(sinkLabel(*sink));
        }
        Layout layout = fitLayoutOfLabels(labels, maxSize, drawAsColumn, withAddSignalButton);
        layout.sinks  = std::move(sinks);
        return layout;
    }

    /// fitLayout() for items with the given labels, without Layout::sinks
    [[nodiscard]] static Layout fitLayoutOfLabels(std::span<const std::string> labels, ImVec2 maxSize, bool drawAsColumn, bool withAddSignalButton) {
        const auto layoutAt = [&](float fontSize, float maxLabelWidth) { return layoutAtGivenFixedSize(labels, withAddSignalButton, fontSize, maxLabelWidth, maxSize.x, drawAsColumn); };

        constexpr float kFontSizeStep        = 1.f; // we find font sizes that fit by iteration so we have to increment by some amount, a single pixel seems fine
        constexpr float kUnlimitedLabelWidth = std::numeric_limits<float>::max();
        const float     normalFontSize       = ImGui::GetFontSize();
        const float     minFontSize          = std::max(kFontSizeStep, std::floor(normalFontSize * LookAndFeel::instance().dashboardStyle.legendMinFontScale));
        for (float fontSize = normalFontSize; fontSize >= minFontSize; fontSize = std::ceil(fontSize - kFontSizeStep)) {
            if (Layout layout = layoutAt(fontSize, kUnlimitedLabelWidth); layout.isSmallerThan(maxSize)) {
                return layout;
            }
        }

        if (layoutAt(minFontSize, 0.f).isSmallerThan(maxSize)) {
            float fittingLabelWidth = 0.f;
            float tooWideLabelWidth = 0.f;
            {
                imw::FontWithSize font(ImGui::GetFont(), minFontSize);
                for (const std::string& label : labels) {
                    tooWideLabelWidth = std::max(tooWideLabelWidth, ImGui::CalcTextSize(label.c_str()).x);
                }
            }
            constexpr float kLabelWidthPrecision = 1.f;
            while (tooWideLabelWidth - fittingLabelWidth > kLabelWidthPrecision) {
                const float labelWidth = .5f * (fittingLabelWidth + tooWideLabelWidth);
                if (layoutAt(minFontSize, labelWidth).isSmallerThan(maxSize)) {
                    fittingLabelWidth = labelWidth;
                } else {
                    tooWideLabelWidth = labelWidth;
                }
            }
            return layoutAt(minFontSize, fittingLabelWidth);
        }

        for (float fontSize = minFontSize - kFontSizeStep; fontSize > kFontSizeStep; fontSize -= kFontSizeStep) {
            if (Layout layout = layoutAt(fontSize, 0.f); layout.isSmallerThan(maxSize)) {
                return layout;
            }
        }
        return layoutAt(kFontSizeStep, 0.f);
    }

    struct DrawResult {
        std::string rightClickedSinkName; // empty if no signal was right-clicked
        bool        isAddSignalButtonPressed = false;
        ImRect      addSignalButtonRect; // where the button would be drawn if it was not hidden, empty without the button
    };

    /// Draws relative to the current cursor position.
    /// @param firstRowLineHeight the legend is centered vertically in this height, if possible
    /// @param addSignalButtonHidden draws the Add Signal button after the signals, slid out of sight by this AutoHide::hidden
    DrawResult draw(UiGraphModel& graphModel, const Layout& layout, float firstRowLineHeight = 0.f, std::optional<float> addSignalButtonHidden = std::nullopt) noexcept {
        using namespace charts;
        _openColorPopup = false;

        const float  scale          = layout.fontSize / ImGui::GetFontSize(); // of the padding and spacing
        const float  verticalOffset = std::max(0.f, .5f * (firstRowLineHeight - layout.size.y));
        const ImVec2 origin         = ImGui::GetCursorScreenPos() + ImVec2{0.f, verticalOffset};
        DrawResult   result;
        {
            imw::Group        group;
            imw::FontWithSize font(ImGui::GetFont(), layout.fontSize);
            {
                imw::ChangeStrId idScope(kIdScope);
                for (std::size_t index = 0UZ; index < layout.sinks.size(); ++index) {
                    ImGui::SetCursorScreenPos(origin + layout.itemRects[index].Min);
                    if (drawSink(*layout.sinks[index], static_cast<int>(index), layout.maxLabelWidth, scale)) {
                        result.rightClickedSinkName = layout.sinks[index]->blockUniqueName;
                    }
                }
            }
            if (addSignalButtonHidden.has_value() && layout.itemRects.size() > layout.sinks.size()) {
                result.addSignalButtonRect = ImRect(origin + layout.itemRects.back().Min, origin + layout.itemRects.back().Max);
                ImGui::SetCursorScreenPos(result.addSignalButtonRect.Min);
                result.isAddSignalButtonPressed = components::drawSlidOutOfSight(*addSignalButtonHidden, [&] { return drawAddSignalButton(result.addSignalButtonRect.GetSize()); });
            }
        }
        _legendSize = layout.size + ImVec2{0.f, verticalOffset};

        if (_openColorPopup) {
            ImGui::OpenPopup("SinkColorPopup");
        }
        if (ImGui::BeginPopup("SinkColorPopup")) {
            UiGraphBlock* editingSink = graphModel.recursiveFindBlockByUniqueName(_editingSinkUniqueName).block;
            if (editingSink) {
                auto          colorIter = editingSink->blockSettings.find("color");
                std::uint32_t c         = colorIter != std::end(editingSink->blockSettings) ? colorIter->second.value_or(std::uint32_t{}) : std::uint32_t{};
                ImVec4        col       = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(c));
                float         rgb[3]{col.x, col.y, col.z};
                if (ImGui::ColorPicker3("##color", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar)) {
                    auto newColor = float3ToRGB(rgb);
                    editingSink->setSetting("color", newColor);
                }
            }
            ImGui::EndPopup();
        }

        dnd::acceptLegendDrop();

        return result;
    }

    static bool drawAddSignalButton(ImVec2 size) {
        const Palette&    palette = LookAndFeel::instance().palette();
        imw::StyleColor   buttonStyle(ImGuiCol_Button, palette.mainWindowButtonBgInactive);
        imw::StyleColor   textStyle(ImGuiCol_Text, palette.mainWindowButtonIcon.value_or(ImGui::GetStyleColorVec4(ImGuiCol_Text)));
        imw::StyleColor   buttonActiveStyle(ImGuiCol_ButtonActive, palette.mainWindowButtonBgActive);
        imw::StyleColor   buttonHoveredStyle(ImGuiCol_ButtonHovered, palette.mainWindowButtonBgHovered);
        imw::StyleVar     noPadding(ImGuiStyleVar_FramePadding, ImVec2{});
        imw::FontWithSize font(LookAndFeel::instance().fontIconsSolid, LookAndFeel::instance().dashboardStyle.legendAddButtonIconScale * size.y);
        const bool        isPressed = ImGui::Button("\u{F067}##addSignal", size);
        if (ImGui::IsItemHovered()) {
            imw::Font tooltipFont(LookAndFeel::instance().fontNormal());
            ImGui::SetTooltip("add signal");
        }
        return isPressed;
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
