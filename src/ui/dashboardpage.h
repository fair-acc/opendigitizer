#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include "grid_layout.h"
#include "imguiutils.h"
#include <imgui.h>
#include <stack>
#include <string>
#include <vector>

namespace DigitizerUi {

struct App;

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
    ImVec2           pane_size{ 0, 0 };     // updated by drawPlots(...)
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
    void newPlot(Dashboard *dashboard);

private:
    void                           drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard);
    void                           drawGrid(float w, float h);
    void                           drawLegend(App *app, Dashboard *dashboard, const Mode &mode) noexcept;
    static void                    drawPlot(DigitizerUi::Dashboard::Plot &plot) noexcept;

    ImGuiUtils::BlockControlsPanel m_editPane;
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
