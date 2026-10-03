#ifndef OPENDIGITIZER_UI_STATUSBARVIEW_HPP
#define OPENDIGITIZER_UI_STATUSBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "Dashboard.hpp"
#include "LogHistory.hpp"
#include "PaneBlocks.hpp"

namespace DigitizerUi {

/// Draws a one-line status bar: the latest GR4 warning or error and notification (see logHistory()) with the counts per
/// level, a popup with the retained records, then the 'Dear ImGui' status-bar blocks (UICategory::StatusBar) of the
/// scheduler's flowgraph in graph order (a dashboard's, or one a host runs with a Scheduler and a UiGraphModel).
/// @code
/// DigitizerUi::StatusBarView statusBar{DigitizerUi::logHistory()};
/// statusBar.draw(dashboard.get()); // nullptr: log only
/// @endcode
class StatusBarView {
public:
    explicit StatusBarView(LogHistory& history) noexcept : _history(history) {}

    void draw(Scheduler* scheduler, const UiGraphModel* graphModel); // nullptr: log only
    void draw(Dashboard* dashboard) { draw(dashboard ? &dashboard->scheduler : nullptr, dashboard ? &dashboard->graphModel : nullptr); }

    [[nodiscard]] static float height() noexcept;

    [[nodiscard]] const std::vector<std::shared_ptr<gr::BlockModel>>& blocks() const noexcept { return _blocks.blocks(); }

private:
    LogHistory& _history;
    PaneBlocks  _blocks{gr::UICategory::StatusBar};

    void drawLogLine();
    void drawLogPopup();
};

} // namespace DigitizerUi

#endif
