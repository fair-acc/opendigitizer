#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../common/ImguiWrap.hpp"

#include <chrono>

namespace DigitizerUi {
struct UiGraphBlock;
} // namespace DigitizerUi

namespace DigitizerUi::components {

struct BlockControlsPanelContext {
    UiGraphBlock* block = nullptr;
    enum class Mode { None, Insert, AddAndBranch };

    Mode mode = Mode::None;

    std::chrono::time_point<std::chrono::system_clock> closeTime;
};

void BlockControlsPanel(BlockControlsPanelContext& context, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout);
void BlockSettingsControls(UiGraphBlock* block, bool verticalLayout, const ImVec2& size = {0.f, 0.f});

} // namespace DigitizerUi::components

#endif
