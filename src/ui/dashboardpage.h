#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include "grid_layout.h"
#include <imgui.h>
#include <string>
#include <vector>

namespace DigitizerUi {

class App;

class DashboardPage {
    ImVec2     pane_size{ 0, 0 };     // updated by draw(...)
    ImVec2     legend_box{ 500, 40 }; // updated by drawLegend(...)
    GridLayout plot_layout;

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
    void drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard);
    void drawGrid(float w, float h);
    void drawLegend(App *app, Dashboard *dashboard, const Mode &mode) noexcept;
    void newPlot(Dashboard *dashboard);
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H