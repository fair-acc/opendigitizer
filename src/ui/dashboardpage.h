#pragma once
#pragma once

#include "grid_layout.h"
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace DigitizerUi {

class App;

class DashboardPage {
    static constexpr auto dndType = "DND_SOURCE";
    //
    ImVec2 _paneSize{ 0, 0 };     // updated by draw(...)
    ImVec2 _legendBox{ 500, 40 }; // updated by drawLegend(...)

    void   newPlot(Dashboard *dashboard);

    struct DndItem {
        Dashboard::Plot   *plotSource;
        Dashboard::Source *source;
    };

public:
    enum class Mode {
        View,
        Layout
    };
    void draw(App *app, Dashboard *Dashboard, Mode mode = Mode::View) noexcept;
    void drawPlots(App *app, DigitizerUi::DashboardPage::Mode mode, Dashboard *dashboard);
    void drawGrid(float w, float h);
    void drawLegend(App *app, Dashboard *dashboard, const Mode &mode) noexcept;

private:
    static bool plotButton(App *app, std::string_view glyph, std::string_view tooltip = {});

private:
    GridLayout plot_layout;

};

} // namespace DigitizerUi
