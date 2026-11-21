#ifndef OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
#define OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP

#include "../GraphModel.hpp"
#include "../charts/Chart.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include <implot.h>

#include <functional>
#include <string>
#include <string_view>

namespace gr::od {

/// every SinkRegistry sink: click toggles, right-click edits colour and settings, drag to charts and drop from charts
struct GlobalSignalLegend {
    enum class ClickResult { None, Left, RightColor, RightName };

    ImVec2      _legendSize{0.f, 0.f};
    std::string _editingSinkUniqueName;
    bool        _openColorPopup  = false;
    bool        _dragDropEnabled = true;

    void setDragDropEnabled(bool isNowEnabled) { _dragDropEnabled = isNowEnabled; }

    /// the unique name of a right-clicked signal, else empty;
    /// `firstRowLineHeight` centres the items vertically, `drawTrailingItem` draws e.g. the add-signal button
    std::string draw(UiGraphModel& graphModel, float paneWidth, float firstRowLineHeight = 0.f, const std::function<void(ImVec2)>& drawTrailingItem = {}) noexcept {
        std::string out;
        std::tie(_legendSize, out) = drawLegend(graphModel, paneWidth, firstRowLineHeight, drawTrailingItem);
        return out;
    }

    [[nodiscard]] ImVec2 legendSize() const noexcept { return _legendSize; }

    [[nodiscard]] static float  legendItemHeight() { return ImGui::GetTextLineHeight() + 2.f * ImPlot::GetStyle().LegendInnerPadding.y; }
    [[nodiscard]] static ImVec2 legendColorRectSize() { return {ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()}; }

    static ClickResult drawLegendItem(std::uint32_t color, std::string_view text, bool enabled = true) {
        ClickResult                result = ClickResult::None;
        constexpr ImGuiMouseButton kRMB   = ImGuiMouseButton_Right;

        imw::StyleVar     resetSpacing(ImGuiStyleVar_ItemSpacing, ImVec2{});     // no space between the swatch and the name
        const ImVec2      innerPad      = ImPlot::GetStyle().LegendInnerPadding; // mimic implot legends
        const auto        textSize      = ImGui::CalcTextSize(text.data());
        const ImVec2      colorRectSize = legendColorRectSize();
        const ImGuiStyle& style         = ImGui::GetStyle();
        const ImVec2      totalSize     = {innerPad.x + colorRectSize.x + textSize.x + (2.f * innerPad.x), legendItemHeight()};
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
        drawList->AddText(groupMin + ImVec2{innerPad.x + colorRectSize.x, 0.f} + innerPad, ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), text.data(), text.data() + text.size());
        if (!colorRectHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightName;
            }
        }

        return result;
    }

    /// true when right-clicked
    bool drawSink(UiGraphBlock& sink, float& accumulatedWidth, float paneWidth, int index) {
        imw::ChangeId itemId(index);

        auto getOrNull = [&](const gr::property_map& m, std::string_view key) -> gr::pmt::Value {
            if (auto it = m.find(std::string(key)); it != m.end()) {
                return it->second;
            }
            return {};
        };

        const auto        color      = getOrNull(sink.blockSettings, "color").value_or(std::uint32_t{0});
        const auto        visible    = getOrNull(sink.blockSettings, "visible").value_or(true);
        const auto        signalName = getOrNull(sink.blockSettings, "signal_name").value_or(std::string{});
        const auto        name       = getOrNull(sink.blockSettings, "name").value_or(std::string{});
        const auto&       uniqueName = sink.blockUniqueName;
        const std::string label      = signalName.empty() ? name : signalName;

        const auto widthEstimate = ImGui::CalcTextSize(label.c_str()).x + 20.f;
        if ((accumulatedWidth + widthEstimate) < 0.9f * paneWidth) {
            ImGui::SameLine();
        } else {
            accumulatedWidth = 0.f;
        }

        auto clickResult = drawLegendItem(color, label, visible);
        if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
            ImGui::SetTooltip(_dragDropEnabled ? "Drag/Toggle Signal" : "Toggle Signal");
        }

        if (clickResult == ClickResult::RightColor) {
            _editingSinkUniqueName = std::string(uniqueName);
            _openColorPopup        = true;
        }
        if (clickResult == ClickResult::Left) {
            sink.setSetting("visible", !visible);
        }

        accumulatedWidth += ImGui::GetItemRectSize().x;

        // an empty sourceChartId: nothing to remove at the source
        if (_dragDropEnabled) {
            if (auto dndSource = imw::DragDropSource(ImGuiDragDropFlags_None)) {
                using namespace charts;
                if (const auto& signalSink = SinkRegistry::instance().getSink(sink.blockUniqueName)) {
                    dnd::setupPayload(signalSink, {});
                    drawLegendItem(color, label, visible);
                }
            }
        }
        return clickResult == ClickResult::RightName;
    }

    std::pair<ImVec2, std::string> drawLegend(UiGraphModel& graphModel, float paneWidth, float firstRowLineHeight, const std::function<void(ImVec2)>& drawTrailingItem) {
        using namespace charts;
        ImVec2 legendSize{0.f, 0.f};
        float  accumulatedWidth = paneWidth; // start at full width to force new line

        _openColorPopup = false;

        std::string rightClickedBlockName;
        {
            imw::Group group;
            {
                imw::StyleVar noSpacing(ImGuiStyleVar_ItemSpacing, ImVec2{});
                // use Dummy instead of setting cursor position because legend items use SameLine which looks at the last item
                ImGui::Dummy(ImVec2{0.f, std::max(0.f, (firstRowLineHeight - legendItemHeight()) / 2.f)});
            }

            int index = 0;
            for (UiGraphBlock* sink : graphModel.recursiveGatherPlotSinks()) {
                if (drawSink(*sink, accumulatedWidth, paneWidth, index)) {
                    rightClickedBlockName = sink->blockUniqueName;
                }
                ++index;
            }

            if (drawTrailingItem) {
                if (index > 0) {
                    ImGui::SameLine();
                }
                drawTrailingItem(ImVec2{legendItemHeight(), legendItemHeight()});
            }
        }

        legendSize.x = ImGui::GetItemRectSize().x;
        legendSize.y = std::max(5.f, ImGui::GetItemRectSize().y);

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

        return {legendSize, rightClickedBlockName};
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
