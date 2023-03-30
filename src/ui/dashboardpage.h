#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <string>
#include <string_view>
#include <vector>

namespace DigitizerUi {

class Dashboard;
class FlowGraph;

class DashboardPage {
public:
    explicit DashboardPage(FlowGraph *fg);
    ~DashboardPage();

    enum class Mode {
        View,
        Layout
    };
    void draw(Dashboard *Dashboard, Mode mode = Mode::View);

private:
    void       newPlot(Dashboard *dashboard);

    FlowGraph *m_flowGraph;
};

} // namespace DigitizerUi

#endif
