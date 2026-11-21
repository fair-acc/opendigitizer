#ifndef OPENDIGITIZER_UI_COMPONENTS_UICONTROLINTERACTIONCOMPONENTS_HPP
#define OPENDIGITIZER_UI_COMPONENTS_UICONTROLINTERACTIONCOMPONENTS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../GraphModel.hpp"
#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"
#include "StyledWidgets.hpp"

namespace gr::od::components {

enum class SelectionPopupState { InProgress, Confirmed, Cancelled };

struct ScrollableListSelectionPopupResult {
    SelectionPopupState        currentState = SelectionPopupState::InProgress;
    std::optional<std::size_t> hoveredItem;
};

struct CheckboxListItem {
    std::string name;
    bool        selected = false;

    bool draw() {
        checkbox(name.c_str(), &selected);
        return selected;
    }
};

/// A popup with a scrollable list of checkboxes, confirmed with a "Done" button at the bottom
struct ScrollableListSelectionPopup {
    std::string                   _name;
    std::string                   _emptyListMessage;
    std::vector<CheckboxListItem> _items;
    bool                          _opened = false;

    ScrollableListSelectionPopup(std::string name, std::vector<CheckboxListItem> items, std::string emptyListMessage = {}) : _name(std::move(name)), _emptyListMessage(std::move(emptyListMessage)), _items(std::move(items)) {}

    [[nodiscard]] std::span<const CheckboxListItem> items() const { return _items; }

    /// `isItemEnabled(index)` before drawing each item: disabled items take no interaction
    [[nodiscard]] ScrollableListSelectionPopupResult draw(const std::function<bool(std::size_t)>& isItemEnabled = {});
};

/// the overlay of a drag-and-drop interaction, in screen space: drawn after the node editor is done,
/// else it lands either in the foreground (above other windows such as the side panel) or below some flowgraph blocks
struct BlockDragConnectOverlay {
    ImRect clipRect;
    ImVec2 lineStart;
    ImVec2 lineEnd;

    void draw() const;
};

/// a drag from a UI control block to a target
struct BlockDragConnectInteraction {
    std::string _sourceUniqueName;
    ImVec2      _startPosition; // in canvas space
    bool        _released  = false;
    bool        _cancelled = false;
    std::string _releasedOnUniqueName;

    BlockDragConnectInteraction(std::string sourceUniqueName, ImVec2 startPositionInCanvasSpace) : _sourceUniqueName(std::move(sourceUniqueName)), _startPosition(startPositionInCanvasSpace) {}

    [[nodiscard]] std::string_view sourceUniqueName() const { return _sourceUniqueName; }

    /// empty when the user released over empty space in the graph
    [[nodiscard]] std::string_view releasedOnUniqueName() const { return _releasedOnUniqueName; }

    /// true when dropped on the block it started from, like releasing off a button to cancel the press
    [[nodiscard]] bool cancelled() const { return _cancelled; }

    /// only while the node editor is active: it relies on canvas space coordinates
    [[nodiscard]] BlockDragConnectOverlay createOverlayDescription() const;

    /// only while the node editor is active: it relies on canvas space coordinates
    [[nodiscard]] static const UiGraphBlock* blockUnderMouse(std::span<UiGraphBlock* const> drawnBlocks);

    /// true when the drag has completed or been cancelled; only within the node editor's canvas scope, where the mouse position is in canvas space
    bool update(std::span<UiGraphBlock* const> drawnBlocks);
};

/// popup for selecting the properties a glob "*" selection connects to
struct UiControlMultiSelectPopup {
    std::string                  _controlUniqueName;
    ScrollableListSelectionPopup _popup;
    std::optional<std::size_t>   _hoveredItem;

    UiControlMultiSelectPopup(std::string controlUniqueName, UiGraphModel& model) //
        : _controlUniqueName(std::move(controlUniqueName)), _popup("Multi-Select", connectableProperties(_controlUniqueName, model), "No block has a property this control could set.") {}

    [[nodiscard]] std::string_view controlUniqueName() const { return _controlUniqueName; }

    /// property which was hovered during the last draw. this is used to highlight blocks which have the property and would be connected to if the user presses that
    [[nodiscard]] std::optional<std::string_view> hoveredProperty() const;

    SelectionPopupState draw(UiGraphModel& model);

    static std::vector<CheckboxListItem> connectableProperties(std::string_view controlUniqueName, UiGraphModel& model);

    void applySelection(UiGraphModel& model);
};

/// connects properties of the block a drag ended on to the UI control it started from;
/// it lists only properties compatible with the control's existing connections, and checking one disables the listed ones incompatible with it
struct UiControlPropertyLinkPopup {
    BlockDragConnectInteraction  _interaction;
    ScrollableListSelectionPopup _popup;

    UiControlPropertyLinkPopup(BlockDragConnectInteraction interaction, UiGraphModel& model) //
        : _interaction(std::move(interaction)), _popup("Select Block Properties", connectableProperties(_interaction, model), "This block has no compatible properties to control.") {}

    SelectionPopupState draw(UiGraphModel& model);

    static std::vector<CheckboxListItem> connectableProperties(const BlockDragConnectInteraction& interaction, UiGraphModel& model);

    /// `targets` with the entries of `target` replaced by the properties checked in the popup
    [[nodiscard]] TargetMap withSelectionApplied(TargetMap targets, const UiGraphBlock& target) const;

    void applySelection(UiGraphModel& model);
};

/// the target_map entries of `control` below `node`, each with an unlink action, and a drag handle for new connections
std::optional<ImVec2> drawUiControlTargetList(UiGraphModel& graphModel, const UiGraphBlock& control, const ImRect& node);

/// height of the area below the block reserved for the target list and drag handle that drawUiControlTargetList draws
[[nodiscard]] float uiControlTargetListHeight(const UiGraphBlock& block);

[[nodiscard]] float uiControlConnectionLabelsWidth(const UiGraphModel::ControlledPropertyMap& controlledProperties);
/// width of the area left of the block reserved for UI control connection labels; only while the editor is active
[[nodiscard]] inline float uiControlConnectionLabelsWidth(UiGraphModel& graphModel, const UiGraphBlock& block) { //
    return uiControlConnectionLabelsWidth(graphModel.uiControlledProperties(block));
}

/// places the UI control blocks in a wrapped row at the top of the graph view, apart from the region of the other blocks,
/// each with room on its left for the connection source labels; only while the editor is active
[[nodiscard]] std::optional<ImRect> calculateAndArrangeUiControlBlocksDesignatedRegion( //
    std::span<const UiGraphModel::ParsedUiControlTargetMap> parsedUiControlTargetMaps,  //
    std::span<UiGraphBlock* const> controlBlocks, const ImRect& graphBounds);

/// only while the editor is active, outside of a node
void drawUiControlRegionBackgroundRect(const ImRect& region);

struct PropertyConnectionIndicator {
    std::string propertyName;
    std::string blockName;

    // only for the ImGui ID
    std::string blockUniqueName;

    std::uint32_t color         = 0;
    bool          isInteractive = true;
};

/// draws block name, notch and property name, extending over the left of the block
/// "{indicator.blockName} - {indicator.propertyName}"
void drawIncomingPropertyConnectionIndicators(                                          //
    UiGraphBlock&                                                  block,               //
    ImVec2                                                         blockScreenPosition, //
    std::span<const PropertyConnectionIndicator>                   indicators,          //
    const std::function<void(const PropertyConnectionIndicator&)>& onLabelClicked,      //
    const std::function<void(const PropertyConnectionIndicator&)>& onNotchClicked       //
);
} // namespace gr::od::components

#endif
