#include "BlockNeighborsPreview.hpp"
#include "../FlowgraphPage.hpp"
#include "../GraphModel.hpp"
#include "../common/ImguiWrap.hpp"
#include "Block.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <ranges>
#include <string>

namespace DigitizerUi::components {

struct ScaleFont {
    explicit ScaleFont(float scale) : font(ImGui::GetFont()), originalScale(font->Scale) {
        font->Scale = scale;
        ImGui::PushFont(font);
    }
    ~ScaleFont() {
        font->Scale = originalScale;
        ImGui::PopFont();
    }

    ScaleFont(const ScaleFont&)            = delete;
    ScaleFont& operator=(const ScaleFont&) = delete;
    ScaleFont(ScaleFont&&)                 = delete;
    ScaleFont& operator=(ScaleFont&&)      = delete;

    ImFont* const font;
    const float   originalScale;
};

std::string_view shortBlockTitle(std::string_view title) {
    // minor improvement to save precious space. Drop the Source and Sink
    // suffixes as they can be deduced from the context.

    static constexpr std::string_view suffixes[] = {"Source", "Sink"};

    for (auto suffix : suffixes) {
        if (title.ends_with(suffix)) {
            return title.substr(0, title.size() - suffix.size());
        }
    }

    return title;
}

/// returns the left ports or the right ports of a block
std::set<UiGraphPort*> portsForBlock(UiGraphBlock& block, UiGraphModel& model, bool leftPorts) {

    auto edges = model.edges() | std::views::filter([&](const auto& edge) {
        if (!edge.edgeSourcePort || !edge.edgeDestinationPort) {
            return false;
        }

        if (leftPorts) {
            return edge.edgeDestinationPort->ownerBlock == &block;
        } else {
            return edge.edgeSourcePort->ownerBlock == &block;
        }
    });

    return edges | std::views::transform([&](const auto& edge) {
        return leftPorts ? edge.edgeDestinationPort : //
                   edge.edgeSourcePort;
    }) | std::ranges::to<std::set>();
}

void BlockNeighborsPreview(const BlockControlsPanelContext& context, ImVec2 availableSize) {
    // Here we draw the current block and its neighbours, for navigation purposes. Clicking a neighbour will change the panel properties.
    // Reuses styling from the main flowgraph.
    // Rendering with ax::NodeEditor for  would be overkill and introduce complications:
    // - We don't want interaction (DnD, Zoom, change edges)
    // - View should not move
    // While the above can be achieved with some hacking and private API, disabling panning was crashing inside ImGui and disabling Zoom
    // was flaky, where it still zoomed in some edge cases.

    if (!context.block || !context.graphModel) {
        // This does not happen, it would be a bug. Let it crash in debug mode so we notice.
        assert(false);
        return;
    }

    ScaleFont font(0.7f);

    auto leftEdges  = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeDestinationPort->ownerBlock == context.block; });
    auto rightEdges = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeSourcePort->ownerBlock == context.block; });

    auto leftBlocks  = leftEdges | std::views::transform([](const auto& edge) { return edge.edgeSourcePort->ownerBlock; }) | std::ranges::to<std::set>();
    auto rightBlocks = rightEdges | std::views::transform([](const auto& edge) { return edge.edgeDestinationPort->ownerBlock; }) | std::ranges::to<std::set>();

    auto maxBlockTextSize = [](auto blocks) {
        float maxWidth = 0.0f;
        for (const auto& block : blocks) {
            const ImVec2 textSize = ImGui::CalcTextSize(block->blockName.c_str());
            maxWidth              = std::max(maxWidth, textSize.x);
        }
        return maxWidth;
    };

    // Layout parameters
    const float blockInnerPadding  = 5.0f * 2;
    const float leftRectsMaxWidth  = maxBlockTextSize(leftBlocks) + blockInnerPadding;
    const float rightRectsMaxWidth = maxBlockTextSize(rightBlocks) + blockInnerPadding;
    const float blockHeight        = 35.0f;
    const float blockSpacing       = 40;
    const float verticalSpacing    = 10;
    const auto  numBlocksLeft      = leftBlocks.size();
    const auto  numBlocksRight     = rightBlocks.size();
    const auto  maxRects           = static_cast<float>(std::max(numBlocksLeft, numBlocksRight));

    // Port parameters
    const float portWidth  = 6.0f;
    const float portHeight = 6.0f;

    // Center rectangle parameters
    const float centerBlockWidth  = 50;
    const float centerBlockHeight = centerBlockWidth;

    const bool hasLeft  = numBlocksLeft;
    const bool hasRight = numBlocksRight;

    // Calculate total height needed for N rects with spacing
    const float topMargin        = 10.0f;
    const float horizontalMargin = 10.0f;
    const float totalHeight      = std::max(maxRects * blockHeight + (maxRects - 1.0f) * verticalSpacing + (topMargin * 2), centerBlockHeight + (topMargin * 2));
    const float totalWidth       = [&] {
        float result = centerBlockWidth;

        if (hasLeft) {
            result += leftRectsMaxWidth + blockSpacing;
        }

        if (hasRight) {
            result += rightRectsMaxWidth + blockSpacing;
        }

        return result + (horizontalMargin * 2);
    }();

    const float scrollbarHeight = ImGui::GetStyle().ScrollbarSize;

    ImGui::BeginChild("scroll_area", ImVec2(std::min(availableSize.x, totalWidth), totalHeight + scrollbarHeight), 0, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::BeginChild("blockNavigationPreview", ImVec2(totalWidth, totalHeight), false);
    ImGui::BeginGroup();

    // Calculate positions
    const float startX       = horizontalMargin;
    const float startY       = 10.f;
    const float middleBlockX = startX + (hasLeft ? leftRectsMaxWidth + blockSpacing : 0);
    const float rightBlocksX = middleBlockX + centerBlockWidth + blockSpacing;

    const auto& style = ax::NodeEditor::GetStyle();

    const ImU32 fillColor        = ImGui::ColorConvertFloat4ToU32(style.Colors[ax::NodeEditor::StyleColor_NodeBg]);
    const ImU32 hoverColor       = ImGui::ColorConvertFloat4ToU32(style.Colors[ax::NodeEditor::StyleColor_HovNodeBorder]);
    const ImU32 borderColor      = ImGui::ColorConvertFloat4ToU32(style.Colors[ax::NodeEditor::StyleColor_NodeBorder]);
    const ImU32 bgColor          = ImGui::ColorConvertFloat4ToU32(style.Colors[ax::NodeEditor::StyleColor_Bg]);
    const ImU32 outerBorderColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ax::NodeEditor::StyleColor_SelNodeBorder]);
    const float borderThickness  = 2.0f;
    const ImU32 lineColor        = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
    const ImU32 textColor        = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);

    // cache the port positions to simply the edge drawing code
    std::unordered_map<UiGraphPort*, ImVec2> portPositions;

    const ImVec2 centerBlockTopLeft     = ImGui::GetWindowPos() + ImVec2(middleBlockX, totalHeight / 2 - centerBlockHeight / 2);
    const ImVec2 centerBlockBottomRight = centerBlockTopLeft + ImVec2(centerBlockWidth, centerBlockHeight);

    // background
    auto drawList = ImGui::GetWindowDrawList();
    ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), bgColor);

    auto drawPort = [drawList, portWidth, portHeight](ImVec2 portPosition, UiGraphPort& port) { //
        FlowgraphPage::drawPin(drawList, portPosition, ImVec2(portWidth, portHeight), port.portName, port.portType);
    };

    // Draw left ports first for middle block
    {
        const auto ports = portsForBlock(*context.block, *context.graphModel, /*leftPorts=*/true);
        size_t     i     = 0;
        // std::views::enumerate(ports)) TODO: Use once we bump to an EMSDK that supports it
        for (const auto& port : ports) {
            const float portY = centerBlockTopLeft.y + FlowgraphPage::pinLocalPositionY(i, ports.size(), centerBlockHeight, portHeight);
            const float portX = centerBlockTopLeft.x - portWidth;
            drawPort({portX, portY}, *port);
            portPositions[port] = {portX, portY + portHeight / 2};
            i++;
        }
    }

    // Draw right ports first for middle block
    {
        const auto ports = portsForBlock(*context.block, *context.graphModel, /*leftPorts=*/false);
        size_t     i     = 0;
        for (const auto& port : ports) {
            const float portY = centerBlockTopLeft.y + FlowgraphPage::pinLocalPositionY(i, ports.size(), centerBlockHeight, portHeight);
            const float portX = centerBlockBottomRight.x;
            drawPort({portX, portY}, *port);
            portPositions[port] = {portX + portWidth, portY + portHeight / 2};
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

    auto drawNeighbourBlock = [&](UiGraphBlock& block, bool isLeft, float blockX, size_t index) {
        const float  y          = startY + (blockHeight + verticalSpacing) * static_cast<float>(index);
        const float  blockWidth = isLeft ? leftRectsMaxWidth : rightRectsMaxWidth;
        const ImVec2 rectMin    = ImGui::GetWindowPos() + ImVec2(blockX, y);
        const ImVec2 rectMax    = rectMin + ImVec2(blockWidth, blockHeight);

        // All edges connected to this neighbor
        auto edges = context.graphModel->edges() | std::views::filter([&](const auto& edge) { //
            if (!edge.edgeSourcePort || !edge.edgeDestinationPort) {
                return false;
            }

            if (isLeft) {
                return edge.edgeSourcePort->ownerBlock == &block && edge.edgeDestinationPort->ownerBlock == context.block;
            } else {
                return edge.edgeDestinationPort->ownerBlock == &block && edge.edgeSourcePort->ownerBlock == context.block;
            }
        });

        // Ports in the neighbor
        const auto ports = edges | std::views::transform([&](const auto& edge) {
            return isLeft ? edge.edgeSourcePort : //
                       edge.edgeDestinationPort;
        }) | std::ranges::to<std::set>();

        size_t portIndex = 0;
        for (auto port : ports) {
            const float portPositionX = rectMin.x + (isLeft ? blockWidth - 1.0f //
                                                            : -portWidth + 1.0f);
            const float portPositionY = rectMin.y + FlowgraphPage::pinLocalPositionY(portIndex, ports.size(), blockHeight, portHeight);

            drawPort({portPositionX, portPositionY}, *port);
            portPositions[port] = {portPositionX + (isLeft ? portWidth : 0), portPositionY + portHeight / 2};
            ++portIndex;
        }

        // Handle interaction
        ImGui::SetCursorScreenPos(rectMin);
        const ImU32 blockColor = ImGui::IsMouseHoveringRect(rectMin, rectMax) ? hoverColor : fillColor;

        // Draw block
        drawList->AddRectFilled(rectMin, rectMax, blockColor);
        drawList->AddRect(rectMin, rectMax, borderColor, 0.0f, 0, borderThickness);

        // Button and callback
        std::string buttonId = (isLeft ? "left_block_" : "right_block_") + std::to_string(index);
        if (ImGui::InvisibleButton(buttonId.c_str(), ImVec2(blockWidth, blockHeight))) {
            context.blockClickedCallback(&block);
        }

        // Block title
        const std::string_view text     = shortBlockTitle(block.blockName);
        const ImVec2           textSize = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        const ImVec2           textPos  = rectMin + ImVec2((blockWidth - textSize.x) * 0.5f, (blockHeight - textSize.y) * 0.5f);
        drawList->AddText(textPos, textColor, text.data(), text.data() + text.size());
    };

    auto drawEdge = [&](UiGraphEdge& edge) {
        const float arrowWidth  = 9.0f;
        const float arrowHeight = 9.0f;

        const ImVec2 lineStart = portPositions[edge.edgeSourcePort];
        const ImVec2 lineEnd   = portPositions[edge.edgeDestinationPort] - ImVec2(arrowHeight, 0);

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
        size_t i = 0;
        for (const auto& block : leftBlocks) {
            drawNeighbourBlock(*block, true, startX, i);
            i++;
        }
    }

    {
        size_t i = 0;
        for (const auto& block : rightBlocks) {
            drawNeighbourBlock(*block, false, rightBlocksX, i);
            i++;
        }
    }

    for (auto edge : leftEdges) {
        drawEdge(edge);
    }

    for (auto edge : rightEdges) {
        drawEdge(edge);
    }

    ImGui::EndGroup();
    ImGui::EndChild(); // blocks
    ImGui::EndChild(); // scroll_area
}
} // namespace DigitizerUi::components
