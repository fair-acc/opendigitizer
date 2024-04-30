#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../common/ImguiWrap.hpp"

#include "../Flowgraph.hpp"

namespace DigitizerUi {
class Dashboard;
class DashboardPage;
} // namespace DigitizerUi

namespace DigitizerUi::components {

struct BlockControlsPanelContext {
    DigitizerUi::Block *block = {};
    enum class Mode {
        None,
        Insert,
        AddAndBranch
    };
    Mode                                               mode            = Mode::None;
    DigitizerUi::Block::Port                          *insertBefore    = nullptr;
    DigitizerUi::Block::Port                          *insertFrom      = nullptr;
    DigitizerUi::Connection                           *breakConnection = nullptr;
    std::chrono::time_point<std::chrono::system_clock> closeTime;
};

void BlockControlsPanel(Dashboard &dashboard, DashboardPage &dashboardPage, BlockControlsPanelContext &context, const ImVec2 &pos, const ImVec2 &frameSize, bool verticalLayout);
void BlockParametersControls(DigitizerUi::Block *b, bool verticalLayout, const ImVec2 &size = { 0.f, 0.f });

} // namespace DigitizerUi::components

#endif
