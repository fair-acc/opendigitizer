#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_LEGEND_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_LEGEND_HPP_

#include "../charts/Chart.hpp" // For DndHelper, DndPayload
#include "../charts/SinkRegistry.hpp"
#include "../common/ImguiWrap.hpp"

#include <cstring>
#include <functional>
#include <string>
#include <string_view>

namespace DigitizerUi::components {

/**
 * @brief Renders a horizontal signal legend with drag-and-drop support.
 *
 * This component displays all registered signal sinks as clickable legend items
 * with color indicators. It supports:
 * - Left-click: Toggle sink draw enabled
 * - Right-click: Custom callback (e.g., open settings panel)
 * - Drag: Start drag operation for sink transfer to charts
 * - Drop: Remove sink from source chart when dropped here (uses DndHelper)
 */
class SignalLegend {
public:
    /// Result of a legend item interaction
    enum class ClickResult { None, Left, Right };

    /// Callback for right-click on a legend item
    using RightClickCallback = std::function<void(std::string_view sinkUniqueName)>;

    /**
     * @brief Draw a single legend item with color box and label.
     *
     * @param color RGB color (0xRRGGBB format)
     * @param text Label text
     * @param enabled Whether the item appears enabled (affects text color)
     * @return Click result indicating which mouse button was pressed
     */
    static ClickResult drawLegendItem(std::uint32_t color, std::string_view text, bool enabled = true) {
        ClickResult result = ClickResult::None;

        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 rectSize(ImGui::GetTextLineHeight() - 4, ImGui::GetTextLineHeight());

        // Draw color indicator
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPos + ImVec2(0, 2), cursorPos + rectSize - ImVec2(0, 2), rgbToImGuiABGR(color));

        if (ImGui::InvisibleButton("##ColorBox", rectSize)) {
            result = ClickResult::Left;
        }
        ImGui::SameLine();

        // Draw button text with transparent background
        ImVec2 buttonSize(rectSize.x + ImGui::CalcTextSize(text.data()).x - 4, ImGui::GetTextLineHeight());

        IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
        IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
        IMW::StyleColor textStyle(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        if (ImGui::Button(text.data(), buttonSize)) {
            result = ClickResult::Left;
        }

        if (ImGui::IsMouseReleased(ImGuiPopupFlags_MouseButtonRight & ImGuiPopupFlags_MouseButtonMask_) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
            result = ClickResult::Right;
        }

        return result;
    }

    /**
     * @brief Draw the global signal legend with all registered sinks.
     *
     * Uses DndHelper for drag-and-drop operations. When a sink is dropped on the
     * legend from a chart, it is automatically removed from the source chart via
     * DndHelper::handleLegendDropTarget().
     *
     * @param paneWidth Width of the containing pane (for line wrapping)
     * @param onRightClick Callback invoked when a legend item is right-clicked
     * @return The size of the rendered legend (width, height)
     */
    static ImVec2 draw(float paneWidth, RightClickCallback onRightClick = nullptr) {
        ImVec2 legendSize{0.f, 0.f};
        float  accumulatedWidth = paneWidth; // Start at full width to force new line

        {
            IMW::Group group;

            int index = 0;
            opendigitizer::charts::SinkRegistry::instance().forEach([&](opendigitizer::charts::SignalSink& sink) {
                IMW::ChangeId itemId(index++);

                const auto        color = sink.color();
                const std::string label = sink.signalName().empty() ? std::string(sink.name()) : std::string(sink.signalName());

                // Check if we need to wrap to next line
                const auto widthEstimate = ImGui::CalcTextSize(label.c_str()).x + 20.f; // icon width
                if ((accumulatedWidth + widthEstimate) < 0.9f * paneWidth) {
                    ImGui::SameLine();
                } else {
                    accumulatedWidth = 0.f; // Start new line
                }

                // Draw the legend item
                auto clickResult = drawLegendItem(color, label, sink.drawEnabled());

                if (clickResult == ClickResult::Right && onRightClick) {
                    onRightClick(sink.uniqueName());
                }
                if (clickResult == ClickResult::Left) {
                    sink.setDrawEnabled(!sink.drawEnabled());
                }

                accumulatedWidth += ImGui::GetItemRectSize().x;

                // Drag source - from global legend (empty sourceChartId = no removal needed)
                if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_None)) {
                    // Create payload directly (no shared_ptr lookup needed)
                    opendigitizer::charts::DndPayload dnd{};
                    std::strncpy(dnd.sinkName, std::string(sink.name()).c_str(), sizeof(dnd.sinkName) - 1);
                    // sourceChartId stays empty (dragging from legend = no chart to remove from)
                    ImGui::SetDragDropPayload(opendigitizer::charts::DndHelper::kPayloadType, &dnd, sizeof(dnd));

                    // Draw preview using the legend item style
                    drawLegendItem(color, label, sink.drawEnabled());
                }
            });
        }

        legendSize.x = ImGui::GetItemRectSize().x;
        legendSize.y = std::max(5.f, ImGui::GetItemRectSize().y);

        // Drop target - dropping on legend removes from source chart (via DndHelper)
        // DndHelper::handleLegendDropTarget() uses g_findChartById to locate and
        // remove the sink from the source chart automatically.
        opendigitizer::charts::DndHelper::handleLegendDropTarget();

        return legendSize;
    }
};

} // namespace DigitizerUi::components

#endif // OPENDIGITIZER_UI_COMPONENTS_SIGNAL_LEGEND_HPP_
