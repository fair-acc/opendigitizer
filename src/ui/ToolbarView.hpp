#ifndef OPENDIGITIZER_UI_TOOLBARVIEW_HPP
#define OPENDIGITIZER_UI_TOOLBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphSession.hpp"
#include "PaneBlocks.hpp"

namespace DigitizerUi {

/// Draws, in a row, play/pause/stop for the scheduler when asked for (a dashboard's `scheduler_ui`), then the
/// 'Dear ImGui' toolbar blocks (UICategory::Toolbar) of the scheduler's flowgraph in graph order. Needs no dashboard: a
/// host running a graph draws with its GraphSession (`session.handleMessages()` each frame).
/// The blocks are ordinary blocks of the flowgraph; the row is rebuilt when the flowgraph changes. Toolbar blocks of
/// other toolkits are skipped with a warning.
enum class SchedulerRequest { none, play, pause, stop }; // pressed in the scheduler controls this frame

struct ToolbarView {
    [[nodiscard]] SchedulerRequest draw(GraphSession& session, bool schedulerControls);

    PaneBlocks _blocks{gr::UICategory::Toolbar};
};

} // namespace DigitizerUi

#endif
