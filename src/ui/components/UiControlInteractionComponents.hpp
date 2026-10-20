#ifndef OPENDIGITIZER_UI_COMPONENTS_UICONTROLINTERACTIONCOMPONENTS_HPP
#define OPENDIGITIZER_UI_COMPONENTS_UICONTROLINTERACTIONCOMPONENTS_HPP

#include <algorithm>
#include <concepts>
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
#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi::components {

enum class SelectionPopupState { InProgress, Confirmed, Cancelled };

struct ScrollableListSelectionPopupResult {
    // this is a view into the ScrollableListSelectionPopup, it becomes invalid
    // when that is destroyed or draw() is called on it again
    std::span<const std::size_t> enabledItems;
    SelectionPopupState          currentState = SelectionPopupState::InProgress;
    std::optional<std::size_t>   hoveredItem;
};

// example type that can be given as a parameter to
// ScrollableListSelectionPopup. this one just has a line of text and a
// checkbox to enable or disable each line
struct CheckboxListItem {
    std::string name;
    bool        selected = false;

    bool draw() {
        ImGui::Checkbox(name.c_str(), &selected);
        return selected;
    }
};

/// This class stores the state needed for a drawing a popup containing a
/// scrollable list of items, each of which is selectable, and then handling
/// the user pressing a "Done" button at the bottom of the popup to confirm
/// their selection
template<typename T>
class ScrollableListSelectionPopup {
    std::string              _name;
    std::string              _emptyListMessage;
    std::vector<T>           _items;
    std::vector<std::size_t> _enabledItems;
    bool                     _opened = false;

public:
    ScrollableListSelectionPopup(std::string name, std::vector<T> items, std::string emptyListMessage = {}) : _name(std::move(name)), _emptyListMessage(std::move(emptyListMessage)), _items(std::move(items)) {}

    [[nodiscard]] std::span<const T> items() const { return _items; }

    [[nodiscard]] ScrollableListSelectionPopupResult draw() {
        return draw([](std::size_t) { return true; });
    }

    /// @param isItemEnabled is called with the index of each item before drawing it, disabled items cannot be interacted with
    [[nodiscard]] ScrollableListSelectionPopupResult draw(std::predicate<std::size_t> auto isItemEnabled) {
        if (!std::exchange(_opened, true)) {
            ImGui::OpenPopup(_name.c_str());
        }
        _enabledItems.clear();
        std::optional<std::size_t> hoveredItem;
        bool                       confirmed = false;

        if (auto popup = IMW::Popup(_name.c_str(), 0)) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::TextUnformatted(_name.c_str());
            ImGui::Separator();
            {
                IMW::Child list("##items", ImVec2(280.F, 180.F), true, 0);
                for (std::size_t index = 0UZ; index < _items.size(); ++index) {
                    IMW::Disabled incompatible(!isItemEnabled(index));
                    if (_items[index].draw()) {
                        _enabledItems.push_back(index);
                    }
                    if (ImGui::IsItemHovered()) {
                        hoveredItem = index;
                    }
                }
                if (_items.empty() && !_emptyListMessage.empty()) {
                    IMW::StyleColor textColor(ImGuiCol_Text, LookAndFeel::instance().palette().errorColor);
                    ImGui::TextWrapped("%s", _emptyListMessage.c_str());
                }
            }

            const float doneWidth = 100.F;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (ImGui::GetContentRegionAvail().x - doneWidth) / 2.F));
            if (ImGui::Button("Done", ImVec2(doneWidth, 0.F))) {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }
        }

        const SelectionPopupState unconfirmedState = ImGui::IsPopupOpen(_name.c_str()) ? SelectionPopupState::InProgress : SelectionPopupState::Cancelled;
        const SelectionPopupState state            = confirmed ? SelectionPopupState::Confirmed : unconfirmedState;
        return {.enabledItems = _enabledItems, .currentState = state, .hoveredItem = hoveredItem};
    }
};

/// Description of the overlay graphics that should get drawn when a drag and drop interaction is happening
/// This needs to exist because we have to defer drawing these until after the node editor is done,
/// otherwise the Z ordering either goes into foreground (above other windows like the side panel) or
/// potentially below some flowgraph blocks
///
/// all coordinates here are screen space, because they are intended to be drawn after the canvas is done
struct BlockDragConnectOverlay {
    ImRect                clipRect;
    ImVec2                lineStart;
    ImVec2                lineEnd;
    std::optional<ImRect> dropTargetOutline;

    void draw() const;
};

/// Represents the state of a user dragging their mouse from a UI control block to a target
class BlockDragConnectInteraction {
    std::string _sourceUniqueName;
    ImVec2      _startPosition; // in canvas space
    bool        _released  = false;
    bool        _cancelled = false;
    std::string _releasedOnUniqueName;

public:
    BlockDragConnectInteraction(std::string sourceUniqueName, ImVec2 startPositionInCanvasSpace) : _sourceUniqueName(std::move(sourceUniqueName)), _startPosition(startPositionInCanvasSpace) {}

    [[nodiscard]] std::string_view sourceUniqueName() const { return _sourceUniqueName; }

    /// This is empty when the user released over empty space in the graph
    [[nodiscard]] std::string_view releasedOnUniqueName() const { return _releasedOnUniqueName; }

    /// Returns true if the user dropped the connection on top of the block which it was from.
    /// Consider this to be the equivalent of a user dragging their mouse off of a button before releasing to cancel the press
    [[nodiscard]] bool cancelled() const { return _cancelled; }

    /// This must be called while the node editor is active because it relies on canvas space coordinates
    [[nodiscard]] BlockDragConnectOverlay createOverlayDescription(std::span<UiGraphBlock* const> drawnBlocks, const std::function<bool(const UiGraphBlock&)>& isCompatibleDropTarget) const;

    /// Returns true if the drag has completed or been cancelled.
    /// This must be called within the node editor's canvas scope, so the mouse position is in canvas space
    bool update(std::span<UiGraphBlock* const> drawnBlocks);
};

/// Popup for selecting what properties to connect to with a glob "*" selection
class UiControlMultiSelectPopup {
    std::string                                    _controlUniqueName;
    ScrollableListSelectionPopup<CheckboxListItem> _popup;
    std::optional<std::size_t>                     _hoveredItem;

public:
    UiControlMultiSelectPopup(std::string controlUniqueName, UiGraphModel& model) //
        : _controlUniqueName(std::move(controlUniqueName)), _popup("Multi-Select", connectableProperties(_controlUniqueName, model), "No block has a property this control could set.") {}

    [[nodiscard]] std::string_view controlUniqueName() const { return _controlUniqueName; }

    /// property which was hovered during the last draw. this is used to highlight blocks which have the property and would be connected to if the user presses that
    [[nodiscard]] std::optional<std::string_view> hoveredProperty() const;

    SelectionPopupState draw(UiGraphModel& model);

private:
    static std::vector<CheckboxListItem> connectableProperties(std::string_view controlUniqueName, UiGraphModel& model);

    void applySelection(UiGraphModel& model);
};

/// Connects properties of the block a drag ended on to the UI control it started from.
/// Only properties compatible with the control's existing connections are listed, and checking one disables
/// the listed properties which are incompatible with it.
class UiControlPropertyLinkPopup {
    BlockDragConnectInteraction                    _interaction;
    ScrollableListSelectionPopup<CheckboxListItem> _popup;

public:
    UiControlPropertyLinkPopup(BlockDragConnectInteraction interaction, UiGraphModel& model) //
        : _interaction(std::move(interaction)), _popup("Select Block Properties", connectableProperties(_interaction, model), "This block has no compatible properties to control.") {}

    SelectionPopupState draw(UiGraphModel& model);

private:
    static std::vector<CheckboxListItem> connectableProperties(const BlockDragConnectInteraction& interaction, UiGraphModel& model);

    /// @param targets with the entries for @param target replaced by the properties currently checked in the popup
    [[nodiscard]] TargetMap withSelectionApplied(TargetMap targets, const UiGraphBlock& target) const;

    void applySelection(UiGraphModel& model);
};

/// Uses @param blockStartCursorPosition to deduce the horizontal and vertical padding used when drawing the block.
/// Returns true while a drag is happening and false otherwise
bool drawUiControlDragDropHandle(const char* label, float availableWidth, ImVec2 blockStartCursorPosition);

struct UiControlConnectionNotchResult {
    float labelHeight  = 0.F;
    bool  labelClicked = false;
    bool  notchClicked = false;
};

/// Draws the target_map entries of @param control as a bunch of rows with the labels block:property, with remove buttons
void drawUiControlTargetList(UiGraphModel& graphModel, const UiGraphBlock& control);

/// Width of the area to the left of the block which is reserved for UI control connection labels
/// Must be called while the editor is active
[[nodiscard]] float uiControlConnectionLabelsWidth(UiGraphModel& graphModel, const UiGraphBlock& block);

/// Spacing between the UI control block region and the rest of the graph, and between the blocks within it
constexpr float kUiControlRegionGap = 16.0f;

/// Sets UI control blocks' positions based on the number of UI blocks and the region occupied by non-UI blocks.
/// This creates a wrapped row at the top of the graph view which is separate from the regular block nodes
/// Each block is given extra room on its left for UI control connection source labels
/// Must be called while the editor is active
[[nodiscard]] std::optional<ImRect> calculateAndArrangeUiControlBlocksDesignatedRegion(UiGraphModel& graphModel, std::span<UiGraphBlock* const> controlBlocks, const ImRect& graphBounds);

/// Must be called while the editor is active, outside of a node
void drawUiControlRegionBackgroundRect(const ImRect& region);

struct PropertyConnectionIndicator {
    std::string propertyName;
    std::string blockName;

    // TODO: remove this when block names are guaranteed unique, just used for ImGui ID
    std::string blockUniqueName;

    std::uint32_t color         = 0;
    bool          isInteractive = true;
};

/// Draw block name and notch and property name, extending over the LHS of the block.
/// "{indicator.blockName} - {indicator.propertyName}"
void drawIncomingPropertyConnectionIndicators(                                          //
    UiGraphBlock&                                                  block,               //
    ImVec2                                                         blockScreenPosition, //
    std::span<const PropertyConnectionIndicator>                   indicators,          //
    const std::function<void(const PropertyConnectionIndicator&)>& onLabelClicked,      //
    const std::function<void(const PropertyConnectionIndicator&)>& onNotchClicked       //
);
} // namespace DigitizerUi::components

#endif
