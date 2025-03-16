#include "BlockNeighborsPreview.hpp"
#include "../GraphModel.hpp"
#include "../common/ImguiWrap.hpp"
#include "Block.hpp"

#include <ranges>
#include <string>

namespace DigitizerUi::components {

bool drawCollapsible(ImVec2 availableSize) {
    // Collapsibles are being removed. But this one might be useful, as the preview is quite tall.
    // To be discussed

    static bool enabled   = true;
    const auto& style     = ImGui::GetStyle();
    const auto  curpos    = ImGui::GetCursorPos();
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    if (ImGui::Button("##blockNavigationCollapsible", {availableSize.x, 0.f})) {
        enabled = !enabled;
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Displays the current block and its adjacent blocks.\nClicking an adjacent block will display its properties.");
    }

    const auto newPos = ImGui::GetCursorPos();

    ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
    ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.IndentSpacing);
    ImGui::TextUnformatted("Block Navigation");

    ImGui::SetCursorPos(newPos);

    return enabled;
}

void BlockNeighborsPreview(const BlockControlsPanelContext& context, ImVec2 availableSize) {
    // Here we draw the current block and its neighbours, for navigation purposes. Clicking a neighbour will change the panel properties.
    // Reusing ax::NodeEditor seems overkill for this use case and would introduce complexity:
    // - We need a different block styling, as we have very little space (blocks should be smaller)
    // - We don't need interaction (DnD, Zoom, change edges)
    // - Needs unique ids for the edges and blocks (otherwise clashes with main fg)
    // - We want ports to be explicitly rendered and colored
    // The gui is separate from the business logic, can be changed once the main frame graph is restyled
    // Open discussion points:
    // - There's not a lot of vertical space in the properties panel. Maybe make the blocks smaller and use text wrapping ?
    // - which colors to use for the different port types?

    if (!context.block || !context.graphModel) {
        // This does not happen, it would be a bug. Let it crash in debug mode so we notice.
        assert(false);
        return;
    }

    if (!drawCollapsible(availableSize)) {
        return;
    }

    auto leftEdges  = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeDestinationPort->ownerBlock == context.block; });
    auto rightEdges = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeSourcePort->ownerBlock == context.block; });

    auto leftBlocks  = leftEdges | std::views::transform([](const auto& edge) { return edge.edgeSourcePort->ownerBlock; });
    auto rightBlocks = rightEdges | std::views::transform([](const auto& edge) { return edge.edgeDestinationPort->ownerBlock; });

    auto maxBlockTextSize = [](auto blocks) {
        float maxWidth = 0.0f;
        for (const auto& block : blocks) {
            const ImVec2 textSize = ImGui::CalcTextSize(block->blockName.c_str());
            maxWidth              = std::max(maxWidth, textSize.x);
        }
        return maxWidth;
    };

    // Layout parameters
    const float blockInnerPadding  = 10.0f * 2;
    const float leftRectsMaxWidth  = maxBlockTextSize(leftBlocks) + blockInnerPadding;
    const float rightRectsMaxWidth = maxBlockTextSize(rightBlocks) + blockInnerPadding;
    const float blockHeight        = 50.0f;
    const float blockSpacing       = 40;
    const float verticalSpacing    = 10;
    const auto  numBlocksLeft      = std::ranges::distance(leftEdges);
    const auto  numBlocksRight     = std::ranges::distance(rightEdges);
    const auto  maxRects           = static_cast<float>(std::max(numBlocksLeft, numBlocksRight));

    // Port parameters
    const float portWidth  = 6.0f;
    const float portHeight = 6.0f;
    const ImU32 portColor  = IM_COL32(255, 100, 100, 255); // Lighter red

    // Center rectangle parameters
    const float centerBlockWidth  = 50;
    const float centerBlockHeight = centerBlockWidth;

    const bool hasLeft  = numBlocksLeft;
    const bool hasRight = numBlocksRight;

    // Calculate total height needed for N rects with spacing
    const float topMargin   = 10.0f;
    const float totalHeight = std::max(maxRects * blockHeight + (maxRects - 1.0f) * verticalSpacing + (topMargin * 2), centerBlockHeight + (topMargin * 2));
    const float totalWidth  = [&] {
        float result = centerBlockWidth;

        if (hasLeft) {
            result += leftRectsMaxWidth + blockSpacing;
        }

        if (hasRight) {
            result += rightRectsMaxWidth + blockSpacing;
        }

        return result;
    }();

    const float scrollbarHeight = ImGui::GetStyle().ScrollbarSize;

    ImGui::BeginChild("scroll_area", ImVec2(availableSize.x, totalHeight + scrollbarHeight), 0, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::BeginChild("blockNavigationPreview", ImVec2(totalWidth, totalHeight), false);
    ImGui::BeginGroup();

    // Calculate positions
    const float startX       = 0;
    const float startY       = 10.0f;
    const float middleBlockX = startX + (hasLeft ? leftRectsMaxWidth + blockSpacing : 0);
    const float rightBlocksX = middleBlockX + centerBlockWidth + blockSpacing;

    const ImU32 fillColor        = IM_COL32(240, 235, 255, 255);
    const ImU32 hoverColor       = IM_COL32(250, 245, 255, 255);
    const ImU32 activeColor      = IM_COL32(255, 250, 255, 255);
    const ImU32 borderColor      = IM_COL32(61, 61, 61, 255);
    const ImU32 outerBorderColor = IM_COL32(215, 156, 62, 255);
    const ImU32 lineColor        = IM_COL32(0, 0, 0, 255);
    const float borderThickness  = 2.0f;

    // Calculate center connection points
    float centerTopOffset    = 10.0f; // Offset from top of center box
    float centerBottomOffset = 10.0f; // Offset from bottom of center box

    const ImVec2 centerBlockTopLeft     = ImGui::GetWindowPos() + ImVec2(middleBlockX, totalHeight / 2 - centerBlockHeight / 2);
    const ImVec2 centerBlockBottomRight = centerBlockTopLeft + ImVec2(centerBlockWidth, centerBlockHeight);

    auto drawList = ImGui::GetWindowDrawList();

    auto drawPort = [drawList, portWidth, portHeight](ImVec2 portPosition, const std::string& tooltip) {
        const ImVec2 portTopLeft     = portPosition;
        const ImVec2 portBottomRight = portTopLeft + ImVec2(portWidth, portHeight);
        drawList->AddRectFilled(portTopLeft, portBottomRight, portColor);

        if (ImGui::IsMouseHoveringRect(portTopLeft, portBottomRight)) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }

        return std::make_pair(portTopLeft, portBottomRight);
    };

    // Draw left ports first for middle block
    {
        size_t i = 0;
        // for (const auto& [i, edge] : std::views::enumerate(leftEdges)) TODO: Uncomment once we bump to an EMSDK that supports it
        for (const auto& edge : leftEdges) {
            ImVec2 leftMiddlePortTopLeft = centerBlockTopLeft + ImVec2(-portWidth, centerTopOffset + static_cast<float>(i) * ((centerBlockHeight - centerTopOffset - centerBottomOffset) / 2) - portHeight / 2);
            drawPort(leftMiddlePortTopLeft, edge.edgeSourcePort->portName);
            i++;
        }
    }

    // Draw right ports first for middle block
    {
        size_t i = 0;
        for (const auto& edge : rightEdges) {
            ImVec2 rightMiddlePortTopLeft = centerBlockBottomRight + ImVec2(0, centerTopOffset + static_cast<float>(i) * ((centerBlockHeight - centerTopOffset - centerBottomOffset) / 2) - centerBlockHeight - portHeight / 2);
            drawPort(rightMiddlePortTopLeft, edge.edgeDestinationPort->portName);
            i++;
        }
    }

    // Draw middle block
    drawList->AddRectFilled(centerBlockTopLeft, centerBlockBottomRight, fillColor);
    drawList->AddRect(centerBlockTopLeft - ImVec2(1, 1), centerBlockBottomRight + ImVec2(1, 1), outerBorderColor, 0.0f, 0, borderThickness);
    drawList->AddRect(centerBlockTopLeft, centerBlockBottomRight, borderColor, 0.0f, 0, borderThickness);
    if (ImGui::IsMouseHoveringRect(centerBlockTopLeft, centerBlockBottomRight)) {
        ImGui::SetTooltip("%s", context.block->blockName.c_str());
    }

    auto drawNeighbourBlock = [&](UiGraphPort& port, bool isLeft, float blockX, long index) {
        const float  y          = startY + (blockHeight + verticalSpacing) * static_cast<float>(index);
        const float  blockWidth = isLeft ? leftRectsMaxWidth : rightRectsMaxWidth;
        const ImVec2 rectMin    = ImGui::GetWindowPos() + ImVec2(blockX, y);
        const ImVec2 rectMax    = rectMin + ImVec2(blockWidth, blockHeight);

        // Draw port
        ImVec2 portPosition;
        if (isLeft) {
            portPosition = rectMin + ImVec2(blockWidth - 1.0f, blockHeight / 2 - portHeight / 2);
        } else {
            portPosition = rectMin + ImVec2(-portWidth + 1, blockHeight / 2 - portHeight / 2);
        }
        auto [portTopLeft, portBottomRight] = drawPort(portPosition, port.portName);

        // Handle interaction
        ImGui::SetCursorScreenPos(rectMin);
        const bool hovered = ImGui::IsMouseHoveringRect(rectMin, rectMax);
        const bool active  = ImGui::IsMouseDown(0) && hovered;

        const ImU32 blockColor = active ? activeColor : (hovered ? hoverColor : fillColor);

        // Draw block
        drawList->AddRectFilled(rectMin, rectMax, blockColor);
        drawList->AddRect(rectMin, rectMax, borderColor, 0.0f, 0, borderThickness);

        // Button and callback
        std::string buttonId = (isLeft ? "left_block_" : "right_block_") + std::to_string(index);
        if (ImGui::InvisibleButton(buttonId.c_str(), ImVec2(blockWidth, blockHeight))) {
            context.blockClickedCallback(port.ownerBlock);
        }

        // Block title
        const std::string text     = port.ownerBlock->blockName;
        const ImVec2      textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2      textPos  = rectMin + ImVec2((blockWidth - textSize.x) * 0.5f, (blockHeight - textSize.y) * 0.5f);
        drawList->AddText(textPos, IM_COL32(0, 0, 0, 255), text.c_str());

        // Draw connecting lines
        const float arrowWidth  = 9.0f;
        const float arrowHeight = 9.0f;
        ImVec2      lineStart, lineEnd;
        if (isLeft) {
            const ImVec2 leftMiddlePortTopLeft = centerBlockTopLeft + ImVec2(-portWidth, centerTopOffset + static_cast<float>(index) * ((centerBlockHeight - centerTopOffset - centerBottomOffset) / 2) - portHeight / 2);
            lineStart                          = portTopLeft + ImVec2(portWidth, portHeight / 2);
            lineEnd                            = leftMiddlePortTopLeft + ImVec2(0, portHeight / 2) + ImVec2(-arrowHeight, 0);
        } else {
            ImVec2 rightMiddlePortTopLeft = centerBlockBottomRight + ImVec2(0, centerTopOffset + static_cast<float>(index) * ((centerBlockHeight - centerTopOffset - centerBottomOffset) / 2) - centerBlockHeight - portHeight / 2);
            lineStart                     = rightMiddlePortTopLeft + ImVec2(portWidth, portHeight / 2);
            lineEnd                       = portTopLeft + ImVec2(0, portHeight / 2) + ImVec2(-arrowHeight, 0);
        }

        const ImVec2 cp1 = lineStart + ImVec2(blockSpacing / 2, 0);
        const ImVec2 cp2 = lineEnd + ImVec2(-blockSpacing / 2, 0);
        drawList->AddBezierCubic(lineStart, cp1, cp2, lineEnd, lineColor, 1.0f);

        // Draw arrow at line end:
        ImVec2 arrowPoints[3];
        arrowPoints[0] = lineEnd + ImVec2(0, -arrowWidth / 2);
        arrowPoints[1] = lineEnd + ImVec2(arrowHeight, 0);
        arrowPoints[2] = lineEnd + ImVec2(0, arrowWidth / 2);
        drawList->AddTriangleFilled(arrowPoints[0], arrowPoints[1], arrowPoints[2], lineColor);
    };

    {
        int i = 0;
        for (const auto& edge : leftEdges) {
            drawNeighbourBlock(*edge.edgeSourcePort, true, startX, i);
            i++;
        }
    }

    {
        int i = 0;
        for (const auto& edge : rightEdges) {
            drawNeighbourBlock(*edge.edgeDestinationPort, false, rightBlocksX, i);
            i++;
        }
    }

    ImGui::EndGroup();
    ImGui::EndChild(); // blocks
    ImGui::EndChild(); // scroll_area
}
} // namespace DigitizerUi::components
