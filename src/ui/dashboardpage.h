#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include "grid_layout.h"
#include <imgui.h>
#include <stack>
#include <string>
#include <vector>

namespace DigitizerUi {

class App;

class DashboardPage {
public:
    enum class Action {
        None,
        Move,
        ResizeLeft,
        ResizeRight,
        ResizeTop,
        ResizeBottom,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomLeft,
        ResizeBottomRight
    };

private:
    ImVec2           pane_size{ 0, 0 };     // updated by draw(...)
    ImVec2           legend_box{ 500, 40 }; // updated by drawLegend(...)
    GridLayout       plot_layout;

    Dashboard::Plot *clicked_plot   = nullptr;
    Action           clicked_action = Action::None;

private:
    static constexpr inline auto *dnd_type = "DND_SOURCE";

public:
    enum class Mode {
        View,
        Layout
    };
    struct DndItem {
        Dashboard::Plot   *plotSource;
        Dashboard::Source *source;
    };

public:
    void draw(App *app, Dashboard *Dashboard, Mode mode = Mode::View) noexcept;

private:
    void        drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard);
    void        drawGrid(float w, float h);
    void        drawLegend(App *app, Dashboard *dashboard, const Mode &mode) noexcept;
    void        newPlot(Dashboard *dashboard);
    static void drawPlot(DigitizerUi::Dashboard::Plot &plot) noexcept;
    void        drawControlsPanel(Dashboard *dashboard, const ImVec2 &pos, const ImVec2 &size, bool verticalLayout);

    struct EditPane {
        Dashboard::Plot         *plot  = {};
        Block                   *block = {};
        std::stack<Connection *> history;
        enum class Mode {
            None,
            Insert,
            AddAndBranch
        };
        Mode                                               mode = Mode::None;
        std::chrono::time_point<std::chrono::system_clock> closeTime;
    } m_editPane;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
