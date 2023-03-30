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

    void draw(Dashboard *Dashboard);

private:
    FlowGraph *m_flowGraph;
};

} // namespace DigitizerUi

#endif
