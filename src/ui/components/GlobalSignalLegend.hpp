#ifndef OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
#define OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP

#include "../GraphModel.hpp"
#include "../charts/Chart.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include <implot.h>

#include <string>
#include <string_view>

namespace DigitizerUi {

/// Global signal legend displaying all registered sinks from SinkRegistry.
/// Supports left-click toggle, right-click colour/settings, drag to charts, and drop from charts.
struct GlobalSignalLegend {
    enum class ClickResult { None, Left, RightColor, RightName };

    ImVec2      _legendSize{0.f, 0.f};
    std::string _editingSinkUniqueName;
    bool        _openColorPopup  = false;
    bool        _dragDropEnabled = true;

    void setDragDropEnabled(bool isNowEnabled) { _dragDropEnabled = isNowEnabled; }

    /// Returns the unique name of a signal which was right-clicked, if any
    std::string draw(UiGraphModel& graphModel, float paneWidth) noexcept {
        std::string out;
        std::tie(_legendSize, out) = drawLegend(graphModel, paneWidth);
        return out;
    }

    [[nodiscard]] ImVec2 legendSize() const noexcept { return _legendSize; }

    static ClickResult drawLegendItem(std::uint32_t color, std::string_view text, bool enabled = true) {
        ClickResult                result = ClickResult::None;
        constexpr ImGuiMouseButton kRMB   = ImGuiMouseButton_Right;

        IMW::StyleVar resetSpacing(ImGuiStyleVar_ItemSpacing, ImVec2{});     // no space between button and color rect
        const ImVec2  innerPad      = ImPlot::GetStyle().LegendInnerPadding; // mimic implot legends
        const auto    textSize      = ImGui::CalcTextSize(text.data());
        const auto    buttonSize    = textSize + innerPad * 2;
        const ImVec2  colorRectSize = {ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()};

        // colour indicator square, centered vertically next to the text
        const auto cursorOriginalY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(cursorOriginalY + (buttonSize.y - colorRectSize.y) / 2.f);
        if (ImGui::InvisibleButton("##ColorBox", colorRectSize)) {
            result = ClickResult::Left;
        }
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), rgbToImGuiABGR(color));

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightColor;
            }
        }
        ImGui::SameLine();

        // signal name button with transparent background
        IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
        IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
        IMW::StyleColor textStyle(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        if (ImGui::Button(text.data(), buttonSize)) {
            result = ClickResult::Left;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            if (ImGui::IsMouseReleased(kRMB)) {
                result = ClickResult::RightName;
            }
        }

        return result;
    }

    /// Returns true if the block was right-clicked
    bool drawSink(UiGraphBlock& sink, float& accumulatedWidth, float paneWidth, int index) {
        IMW::ChangeId itemId(index);

        auto getOrNull = [&](const gr::property_map& m, std::string_view key) -> gr::pmt::Value {
            if (auto it = m.find(std::string(key)); it != m.end()) {
                return it->second;
            }
            return {};
        };

        const auto        color       = getOrNull(sink.blockSettings, "color").value_or(std::uint32_t{0});
        const auto        visible     = getOrNull(sink.blockSettings, "visible").value_or(true);
        const auto        signal_name = getOrNull(sink.blockSettings, "signal_name").value_or(std::string{});
        const auto        name        = getOrNull(sink.blockSettings, "name").value_or(std::string{});
        const auto&       uniqueName  = sink.blockUniqueName;
        const std::string label       = signal_name.empty() ? name : signal_name;

        // check if we need to wrap to next line
        const auto widthEstimate = ImGui::CalcTextSize(label.c_str()).x + 20.f;
        if ((accumulatedWidth + widthEstimate) < 0.9f * paneWidth) {
            ImGui::SameLine();
        } else {
            accumulatedWidth = 0.f;
        }

        // draw the legend item
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

        // drag source - from global legend (empty source_chart_id = no removal needed)
        if (_dragDropEnabled) {
            if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_None)) {
                using namespace opendigitizer::charts;
                if (const auto& signalSink = SinkRegistry::instance().getSink(sink.blockUniqueName)) {
                    dnd::Payload payload{.sink_type = signalSink->signalKind()};
                    dnd::copyToBuffer(payload.sink_name, label);
                    ImGui::SetDragDropPayload(dnd::kPayloadType, &payload, sizeof(payload));
                    drawLegendItem(color, label, visible);
                }
            }
        }
        return clickResult == ClickResult::RightName;
    }

    std::pair<ImVec2, std::string> drawLegend(UiGraphModel& graphModel, float paneWidth) {
        using namespace opendigitizer::charts;
        ImVec2 legendSize{0.f, 0.f};
        float  accumulatedWidth = paneWidth; // start at full width to force new line

        _openColorPopup = false;

        std::string rightClickedBlockName;
        {
            IMW::Group group;

            int index = 0;
            for (UiGraphBlock* sink : graphModel.recursiveGatherPlotSinks()) {
                if (drawSink(*sink, accumulatedWidth, paneWidth, index)) {
                    rightClickedBlockName = sink->blockUniqueName;
                }
                ++index;
            }
        }

        legendSize.x = ImGui::GetItemRectSize().x;
        legendSize.y = std::max(5.f, ImGui::GetItemRectSize().y);

        // colour-only popup (right-click on colour square)
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
                    auto newColor = (static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f);
                    editingSink->setSetting("color", newColor);
                }
            }
            ImGui::EndPopup();
        }

        // drop target - accept drops from charts, signal removal via dnd::g_state
        dnd::handleLegendDropTarget();

        return {legendSize, rightClickedBlockName};
    }
};

} // namespace DigitizerUi

#endif // OPENDIGITIZER_GLOBAL_SIGNAL_LEGEND_HPP
