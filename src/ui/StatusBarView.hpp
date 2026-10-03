#ifndef OPENDIGITIZER_UI_STATUSBARVIEW_HPP
#define OPENDIGITIZER_UI_STATUSBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphModel.hpp"
#include "PaneBlocks.hpp"
#include "Scheduler.hpp"

namespace DigitizerUi {

/// Draws a one-line status bar: the latest GR4 warning or error and notification (see logHistory()) with the counts per
/// level, a popup with the retained records, then the 'Dear ImGui' status-bar blocks (UICategory::StatusBar) of the
/// scheduler's flowgraph in graph order (a dashboard's, or one a host runs with a Scheduler and a UiGraphModel).
/// @code
/// DigitizerUi::StatusBarView statusBar;
/// statusBar.draw(&scheduler, &graphModel); // nullptr: log only
/// @endcode
class StatusBarView {
public:
    void draw(Scheduler* scheduler, const UiGraphModel* graphModel); // nullptr: log only

    [[nodiscard]] static float height() noexcept;

private:
    PaneBlocks _blocks{gr::UICategory::StatusBar};

    void drawLogLine();
    void drawLogPopup();
};

} // namespace DigitizerUi

#endif
