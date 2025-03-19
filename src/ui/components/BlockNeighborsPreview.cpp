#include "BlockNeighborsPreview.hpp"
#include "../FlowgraphItem.hpp"
#include "../GraphModel.hpp"
#include "../common/ImguiWrap.hpp"
#include "Block.hpp"

#include <fmt/core.h>
#include <imgui_node_editor.h>
#include <imgui_node_editor_internal.h>

#include <ranges>
#include <string>

namespace DigitizerUi::components {

void BlockNeighborsPreview(const BlockControlsPanelContext& context, ImVec2 availableSize) {
    // Here we draw the current block and its neighbours, for navigation purposes.
    // Clicking a neighbour will change the panel properties.

    if (!context.block || !context.graphModel) {
        // This does not happen, it would be a bug. Let it crash in debug mode so we notice.
        assert(false);
        return;
    }

    auto maxBlockTextSize = [](auto blocks) {
        float maxWidth = 0.0f;
        for (const auto& block : blocks) {
            const ImVec2 textSize = ImGui::CalcTextSize(block->blockName.c_str());
            maxWidth              = std::max(maxWidth, textSize.x);
        }
        return maxWidth;
    };

    auto leftEdges      = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeDestinationPort->ownerBlock == context.block; });
    auto rightEdges     = context.graphModel->edges() | std::views::filter([&context](const auto& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeSourcePort->ownerBlock == context.block; });
    auto leftBlocks     = leftEdges | std::views::transform([](const auto& edge) { return edge.edgeSourcePort->ownerBlock; });
    auto previousEditor = ax::NodeEditor::GetCurrentEditor();

    // Layout parameters
    const float blockInnerPadding = 10.0f * 2;
    const float leftRectsMaxWidth = maxBlockTextSize(leftBlocks) + blockInnerPadding;

    const float blockHeight     = 50.0f;
    const float blockSpacing    = 40;
    const float verticalSpacing = 10;
    const int   pinHeight       = 10;
    const int   pinYOffset      = 5;
    const int   previewHeight   = 150;
    const auto  numBlocksLeft   = std::ranges::distance(leftEdges);

    static ax::NodeEditor::EditorContext* editorContext = nullptr;
    if (!editorContext) {
        ax::NodeEditor::Config config;
        config.SettingsFile = nullptr;
        // config.NavigateButtonIndex = -1; crashes in ImGui if we try to disable panning
        config.EnableSmoothZoom = false;
        config.CustomZoomLevels.clear();
        config.CustomZoomLevels.push_back(1.0f); // force 1x zoom
        editorContext = ax::NodeEditor::CreateEditor(&config);
    }

    {
        IMW::Child child("preview_editor", ImVec2(availableSize.x, previewHeight), 0, 0);

        auto mainStyle = ax::NodeEditor::GetStyle();
        ax::NodeEditor::SetCurrentEditor(editorContext);
        ax::NodeEditor::GetStyle() = mainStyle; // share style with main editor

        {
            IMW::NodeEditor::Editor editor("editor", availableSize);

            // Center node:
            float  centerNodeX     = 0;
            float  centerNodeY     = 0;
            ImVec2 centralNodeSize = {};
            {
                const auto            centralBlockId = ax::NodeEditor::NodeId(context.block);
                IMW::NodeEditor::Node node(centralBlockId);
                ax::NodeEditor::ClearSelection();
                ax::NodeEditor::SelectNode(centralBlockId, true);

                // Makes the node fixed size. Center node doesn't have text, so make it small.
                ImGui::Dummy(ImVec2(blockHeight, blockHeight));
                auto nodeSize = ax::NodeEditor::GetNodeSize(centralBlockId);

                centerNodeX = -availableSize.x / 2 + nodeSize.x / 2 + (numBlocksLeft ? (leftRectsMaxWidth + blockSpacing) : 0);
                centerNodeY = nodeSize.y / 2;
                ax::NodeEditor::SetNodePosition(centralBlockId, {centerNodeX, centerNodeY});

                centralNodeSize = ax::NodeEditor::GetNodeSize(centralBlockId);

                // Register pins
                for (const auto& inPort : context.block->inputPorts) {
                    // reuse code from FlowGraphItem to draw an arrow
                    FlowGraphItem::addPin(ax::NodeEditor::PinId(&inPort), ax::NodeEditor::PinKind::Input, {centerNodeX, centerNodeY + pinYOffset}, {0, pinHeight});
                }

                for (const auto& outPort : context.block->outputPorts) {
                    // output ports don't have arrows
                    ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(&outPort), ax::NodeEditor::PinKind::Output);
                    ax::NodeEditor::EndPin();
                }
            }

            auto blockClickHandler = [&] {
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                    auto n     = ax::NodeEditor::GetHoveredNode();
                    auto block = n.AsPointer<UiGraphBlock>();
                    if (block) {
                        context.blockClickedCallback(block);
                    }
                }
            };

            // Left nodes
            float       nextNodeY = centerNodeY;
            const float leftNodeX = centerNodeX - leftRectsMaxWidth - blockSpacing;

            for (const auto& edge : leftEdges) {
                auto       block       = edge.edgeSourcePort->ownerBlock;
                const auto leftBlockId = ax::NodeEditor::NodeId(block);
                {
                    IMW::NodeEditor::Node node(leftBlockId);
                    ImGui::Text("%s", block->blockName.c_str());
                    ax::NodeEditor::SetNodePosition(leftBlockId, ImVec2(leftNodeX, nextNodeY));

                    for (const auto& outPort : edge.edgeSourcePort->ownerBlock->outputPorts) {
                        ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(&outPort), ax::NodeEditor::PinKind::Output);
                        ax::NodeEditor::EndPin();
                    }

                    blockClickHandler();
                }

                const auto previousNodeSize = ax::NodeEditor::GetNodeSize(ax::NodeEditor::NodeId(block));
                nextNodeY += previousNodeSize.y + verticalSpacing;
            }

            // Right nodes
            const float rightNodeX = centerNodeX + blockSpacing + centralNodeSize.x;
            nextNodeY              = centerNodeY;
            for (const auto& edge : rightEdges) {
                auto       block        = edge.edgeDestinationPort->ownerBlock;
                const auto rightBlockId = ax::NodeEditor::NodeId(block);
                ax::NodeEditor::SetNodePosition(rightBlockId, ImVec2(rightNodeX, nextNodeY));
                {
                    IMW::NodeEditor::Node node(rightBlockId);
                    ImGui::Text("%s", block->blockName.c_str());

                    for (const auto& inPort : edge.edgeDestinationPort->ownerBlock->inputPorts) {
                        FlowGraphItem::addPin(ax::NodeEditor::PinId(&inPort), ax::NodeEditor::PinKind::Input, {rightNodeX, nextNodeY}, {0, pinHeight});
                    }

                    blockClickHandler();
                }

                const auto previousNodeSize = ax::NodeEditor::GetNodeSize(rightBlockId);
                nextNodeY += previousNodeSize.y + verticalSpacing;
            }

            const auto linkColor = ImGui::GetStyle().Colors[ImGuiCol_Text];

            for (const auto& edge : leftEdges) {
                for (const auto& outPort : edge.edgeSourcePort->ownerBlock->outputPorts) {
                    if (outPort.portName == edge.edgeSourcePort->portName) {
                        ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), ax::NodeEditor::PinId(&outPort), ax::NodeEditor::PinId(edge.edgeDestinationPort), linkColor);
                    }
                }
            }

            for (const auto& edge : rightEdges) {
                for (const auto& inPort : edge.edgeDestinationPort->ownerBlock->inputPorts) {
                    if (inPort.portName == edge.edgeDestinationPort->portName) {
                        ax::NodeEditor::Link(ax::NodeEditor::LinkId(&edge), ax::NodeEditor::PinId(edge.edgeSourcePort), ax::NodeEditor::PinId(&inPort), linkColor);
                    }
                }
            }
        } // end editor
    } // end child

    ax::NodeEditor::SetCurrentEditor(previousEditor);
}

} // namespace DigitizerUi::components
