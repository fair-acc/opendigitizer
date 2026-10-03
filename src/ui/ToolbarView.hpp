#ifndef OPENDIGITIZER_UI_TOOLBARVIEW_HPP
#define OPENDIGITIZER_UI_TOOLBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphModel.hpp"
#include "PaneBlocks.hpp"
#include "Scheduler.hpp"

namespace DigitizerUi {

/// Draws, in a row, play/pause/stop for the scheduler when asked for (a dashboard's `scheduler_ui`), then the
/// 'Dear ImGui' toolbar blocks (UICategory::Toolbar) of the scheduler's flowgraph in graph order. Needs no dashboard: a
/// host running a graph wires a Scheduler and a UiGraphModel (`graphModel.sendMessage_` to the scheduler,
/// `scheduler.handleMessages(graphModel)` each frame) and draws with those.
/// The blocks are ordinary blocks of the flowgraph; the row is rebuilt when the flowgraph changes. Toolbar blocks of
/// other toolkits are skipped with a warning.
struct ToolbarView {
    void draw(Scheduler& scheduler, const UiGraphModel& graphModel, bool schedulerControls);

    PaneBlocks _blocks{gr::UICategory::Toolbar};
};

} // namespace DigitizerUi

#endif
