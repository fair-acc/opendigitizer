#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../GraphModel.hpp"
#include "../common/ImguiWrap.hpp"
#include "ExportedPropertiesList.hpp"

#include <gnuradio-4.0/Value.hpp>

#include <imgui.h> // ImVec2

#include <chrono>
#include <functional>

namespace DigitizerUi {
struct UiGraphBlock;
class UiGraphModel;
} // namespace DigitizerUi

namespace DigitizerUi::components {

struct BlockControlsPanelContext {
    BlockControlsPanelContext();
    UiGraphBlock* selectedBlock() const;
    void          setSelectedBlock(UiGraphBlock* block, UiGraphModel* model);

    UiGraphModel* graphModel = nullptr;
    std::string   targetGraph;
    enum class Mode { None, Insert, AddAndBranch };

    Mode mode = Mode::None;

    bool wantsSelectedBlockTab = true;

    ExportedPropertyList propertyList;

    std::chrono::time_point<std::chrono::system_clock> closeTime;
    std::function<void(UiGraphBlock* block)>           blockClickedCallback;

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
BlockPropertyEditResult  BlockSettingsControls(UiGraphBlock* block, const ImVec2& size = {0.f, 0.f});

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
