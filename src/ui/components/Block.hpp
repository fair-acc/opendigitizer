#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../GraphModel.hpp"
#include "../common/ImguiWrap.hpp"
#include "ExportedPropertiesList.hpp"

#include <gnuradio-4.0/Value.hpp>

#include <chrono>
#include <functional>

namespace DigitizerUi {
struct UiGraphBlock;
class UiGraphModel;
} // namespace DigitizerUi

namespace DigitizerUi::components {

// describes an incoming connection to a property, from a UI control block
struct UiControlConnection {
    std::string controlUniqueName;
    std::string controlName;
    bool        allBlocks = false;
    // checkbox state, used, for example, while the "Modify Connections" dialog is open
    bool keep = true;
};

struct ModifyUiConnectionsPopup {
    std::string                      blockUniqueName;
    std::string                      property;
    std::vector<UiControlConnection> connections;
    bool                             opened = false;

    /// returns false if the popup does not name an existing block, or it has been closed
    bool draw(UiGraphModel* graphModel);
};

/// Removes the connection to @param property if exactly one UI control block names @param block specifically.
/// Otherwise returns the popup in which the user chooses which of the connections to remove.
[[nodiscard]] std::optional<ModifyUiConnectionsPopup> unlinkUiControlledProperty(UiGraphBlock& block, const std::string& property);

struct JumpToControlPopup {
    std::string blockUniqueName;
    std::string property;
    bool        opened = false;

    /// returns false if the popup does not name an existing block, or it has been closed
    bool draw(UiGraphModel* graphModel, std::function<void(const std::string&)> focusBlockCallback);
};

struct BlockControlsPanelContext {
    BlockControlsPanelContext();
    UiGraphBlock* selectedBlock() const;
    void          setSelectedBlock(UiGraphBlock* block, UiGraphModel* model);

    UiGraphModel* graphModel = nullptr;
    std::string   targetGraph;
    enum class Mode { None, Insert, AddAndBranch };

    Mode mode = Mode::None;

    bool wantsSelectedBlockTab = true;
    bool showNeighboursPreview = false;

    ExportedPropertyList propertyList;

    std::chrono::time_point<std::chrono::system_clock> closeTime;
    std::function<void(UiGraphBlock* block)>           blockClickedCallback;

    std::optional<ModifyUiConnectionsPopup> modifyConnectionsDialog;
    std::optional<JumpToControlPopup>       jumpToControlPopup;

    std::function<void(const std::string& blockUniqueName)> focusBlockRequest;

    void resetTime();
};

struct BlockPropertyEditResult {
    enum class Type {
        AddNewWindow,
        RemoveFromExistingWindow,
    };

    Type          type{};
    UiGraphBlock* block = nullptr;
    std::string   property;

    constexpr explicit operator bool() const { return block; }
};

struct BlockControlsPanelResult {
    ExportedPropertyList::Result allExportedPropertiesPageResult;
    BlockPropertyEditResult      blockEditPaneResult;
};

/// Returns the action taken on the exported property page, if one occurred
BlockControlsPanelResult BlockControlsPanel(BlockControlsPanelContext& context, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout);
BlockPropertyEditResult  BlockSettingsControls(BlockControlsPanelContext& context, UiGraphBlock* block, const ImVec2& size = {0.f, 0.f});

/// Make imgui widgets to edit a block property value
/// Supports types in block properties: string bool integer float enum color
/// Returns a value containing monostate if no change occurred.
[[nodiscard]] gr::pmt::Value editBlockProperty(const char* label, const std::string& propertyName, const gr::pmt::Value& currentPropertyValue, const UiGraphBlock::SettingsMetaInformation& meta);

/// Min and preferred size of the edit widget that editBlockProperty would draw
/// for the given value and meta information. Used to determine how to wrap
/// these on a line.
[[nodiscard]] IMW::WidgetSize calcEditorSize(const char* label, const std::string& propertyName, const gr::pmt::Value& currentPropertyValue, const UiGraphBlock::SettingsMetaInformation& meta);

} // namespace DigitizerUi::components

#endif
