#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <stack>
#include <string>
#include <vector>

#include "common/ImguiWrap.hpp"

#include "components/Block.hpp"
#include "components/SignalSelector.hpp"

#include "GridLayout.hpp"

struct TestApp;

namespace DigitizerUi {

class DashboardPage {
public:
    enum class Action { None, Move, ResizeLeft, ResizeRight, ResizeTop, ResizeBottom, ResizeTopLeft, ResizeTopRight, ResizeBottomLeft, ResizeBottomRight };

private:
    ImVec2     pane_size{0, 0};     // updated by drawPlots(...)
    ImVec2     legend_box{500, 40}; // updated by drawLegend(...)
    GridLayout plot_layout;

    Dashboard::Plot* clicked_plot   = nullptr;
    Action           clicked_action = Action::None;

private:
    static constexpr inline auto* dnd_type = "DND_SOURCE";

public:
    enum class Mode { View, Layout };
    struct DndItem {
        Dashboard::Plot*   plotSource;
        Dashboard::Source* source;
    };

public:
    void draw(Dashboard& dashboard, Mode mode = Mode::View) noexcept;
    void newPlot(Dashboard& dashboard);

private:
    friend struct ::TestApp;
    void        drawPlots(Dashboard& dashboard, DigitizerUi::DashboardPage::Mode mode);
    void        drawGrid(float w, float h);
    void        drawLegend(Dashboard& dashboard, const Mode& mode) noexcept;
    static void drawPlot(Dashboard& dashboard, DigitizerUi::Dashboard::Plot& plot) noexcept;

    void addSignalCallback(Dashboard& dashboard, Block* block);

    //
    components::BlockControlsPanelContext m_editPane;
    SignalSelector                        m_signalSelector;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
