#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <vector>

namespace DigitizerUi {

class FlowGraph;

class Dashboard {
public:
    explicit Dashboard(FlowGraph *fg);
    ~Dashboard();

    void draw();

private:
    struct Plot;
    FlowGraph        *m_flowGraph;
    std::vector<Plot> m_plots;
};

} // namespace DigitizerUi

#endif
