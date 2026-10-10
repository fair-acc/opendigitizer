#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCKDRAGCONNECTINTERACTION_HPP
#define OPENDIGITIZER_UI_COMPONENTS_BLOCKDRAGCONNECTINTERACTION_HPP

#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "../GraphModel.hpp"
#include "../common/ImguiNodeEditorWrap.hpp"
#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi::components {

/// The drag from a UI control's handle onto the block it should control: it draws the pending
/// connection and reports which node the mouse was released over.
class BlockDragConnectInteraction {
    std::string _sourceUniqueName;
    ImVec2      _startPosition; // canvas space, so the line keeps its anchor while the view is panned or zoomed
    bool        _released = false;
    bool        _cancelled = false;
    std::string _releasedOnUniqueName;

public:
    BlockDragConnectInteraction(std::string sourceUniqueName, ImVec2 startPositionInCanvasSpace) : _sourceUniqueName(std::move(sourceUniqueName)), _startPosition(startPositionInCanvasSpace) {}

    [[nodiscard]] std::string_view sourceUniqueName() const { return _sourceUniqueName; }

    /// the block the drag ended on, empty when it ended over empty canvas
    [[nodiscard]] std::string_view releasedOnUniqueName() const { return _releasedOnUniqueName; }

    /// whether the drag ended with nothing to connect, which is how releasing over the source control reads
    [[nodiscard]] bool cancelled() const { return _cancelled; }

    /// Draws the pending connection and returns whether the drag has ended, staying true from then on.
    /// Call this inside the node editor scope, where the mouse and the node positions share the canvas coordinate system.
    bool draw(std::span<UiGraphBlock* const> drawnBlocks) {
        if (_released) {
            return true;
        }

        const ImVec2  mousePosition = ImGui::GetMousePos();
        UiGraphBlock* dropTarget    = nullptr;
        for (UiGraphBlock* candidate : drawnBlocks) {
            const auto   nodeId   = ax::NodeEditor::NodeId(candidate);
            const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
            if (ImRect(position, position + ax::NodeEditor::GetNodeSize(nodeId)).Contains(mousePosition)) {
                dropTarget = candidate; // the last hit wins, it is drawn on top
            }
        }

        const auto  lineColor = LookAndFeel::getColorU32ImGui(&Palette::flowgraphUiControlPendingConnection);
        const float thickness = 2.F * LookAndFeel::dpiScale();
        auto*       drawList  = ImGui::GetWindowDrawList();
        drawList->AddLine(_startPosition, mousePosition, lineColor, thickness);
        if (dropTarget && dropTarget->blockUniqueName != _sourceUniqueName) {
            const auto   nodeId   = ax::NodeEditor::NodeId(dropTarget);
            const ImVec2 position = ax::NodeEditor::GetNodePosition(nodeId);
            drawList->AddRect(position, position + ax::NodeEditor::GetNodeSize(nodeId), lineColor, 0, ImDrawFlags_None, thickness);
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            return false;
        }
        _released = true;
        if (dropTarget) {
            _releasedOnUniqueName = dropTarget->blockUniqueName;
        }
        _cancelled = _releasedOnUniqueName == _sourceUniqueName;
        return true;
    }
};

} // namespace DigitizerUi::components

#endif
