#ifndef OPENDIGITIZER_UI_TOOLBARVIEW_HPP
#define OPENDIGITIZER_UI_TOOLBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "Dashboard.hpp"
#include "PaneBlocks.hpp"

namespace DigitizerUi {

/// Draws, in a row, play/pause/stop for the scheduler when the dashboard asks for them (`scheduler_ui`), then the
/// 'Dear ImGui' toolbar blocks (UICategory::Toolbar) of the dashboard's flowgraph in graph order.
/// The blocks are ordinary blocks of the flowgraph; the row is rebuilt when the flowgraph changes. Toolbar blocks of
/// other toolkits are skipped with a warning.
class ToolbarView {
public:
    void draw(Dashboard& dashboard);

    [[nodiscard]] const std::vector<std::shared_ptr<gr::BlockModel>>& blocks() const noexcept { return _blocks.blocks(); }

private:
    PaneBlocks _blocks{gr::UICategory::Toolbar};
};

} // namespace DigitizerUi

#endif
