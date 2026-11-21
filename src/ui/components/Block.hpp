#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../GraphModel.hpp"
#include "../common/Imw.hpp"

#include <gnuradio-4.0/Value.hpp>

#include <chrono>
#include <functional>

namespace gr::od {
struct UiGraphBlock;
struct UiGraphModel;
} // namespace gr::od

namespace gr::od::components {

// an incoming connection to a property, from a UI control block
struct UiControlConnection {
    std::string controlUniqueName;
    std::string controlName;
    bool        allBlocks = false;
    // checkbox state, e.g. in the "Modify Connections" dialog
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

/// removes the connection to `property` when exactly one UI control block names `block` specifically;
/// otherwise returns the popup in which the user picks the connections to remove
[[nodiscard]] std::optional<ModifyUiConnectionsPopup> unlinkUiControlledProperty(UiGraphBlock& block, const std::string& property);

struct JumpToControlPopup {
    std::string blockUniqueName;
    std::string property;
    bool        opened = false;

    /// returns false if the popup does not name an existing block, or it has been closed
    bool draw(UiGraphModel* graphModel, std::function<void(const std::string&)> focusBlockCallback);
};

struct BlockControlsPanelContext {
    enum class Mode { None, Insert, AddAndBranch };

    UiGraphModel* graphModel = nullptr;
    std::string   targetGraph;

    Mode mode = Mode::None;

    bool wantsSelectedBlockTab = true;
    bool showNeighboursPreview = false;

    std::chrono::time_point<std::chrono::system_clock> closeTime;

    std::optional<ModifyUiConnectionsPopup> modifyConnectionsDialog;
    std::optional<JumpToControlPopup>       jumpToControlPopup;

    std::function<void(const std::string& blockUniqueName)> focusBlockRequest;

    UiGraphBlock* selectedBlock() const;
    void          setSelectedBlock(UiGraphBlock* block, UiGraphModel* model);
    void          blockClicked(UiGraphBlock* block);
    void          close();
    void          resetTime();
};

void blockControlsPanel(BlockControlsPanelContext& context, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout);
void blockSettingsControls(BlockControlsPanelContext& context, UiGraphBlock* block, const ImVec2& size = {0.f, 0.f});

/// widgets editing a property of type string, bool, integer, float, enum or colour; monostate when unchanged
[[nodiscard]] gr::pmt::Value editBlockProperty(const char* label, const std::string& propertyName, const gr::pmt::Value& currentPropertyValue, const UiGraphBlock::SettingsMetaInformation& meta);

} // namespace gr::od::components

#endif
