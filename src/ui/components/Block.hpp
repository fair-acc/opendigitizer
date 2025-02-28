#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_HPP_

#include "../common/ImguiWrap.hpp"

#include <chrono>
#include <functional>

namespace DigitizerUi {
struct UiGraphBlock;
class UiGraphModel;
} // namespace DigitizerUi

namespace DigitizerUi::components {

struct BlockControlsPanelContext {
    BlockControlsPanelContext();

    UiGraphBlock* block      = nullptr;
    UiGraphModel* graphModel = nullptr;
    enum class Mode { None, Insert, AddAndBranch };

    Mode mode = Mode::None;

    std::chrono::time_point<std::chrono::system_clock> closeTime;
    std::function<void(UiGraphBlock* block)>           blockClickedCallback;

    void resetTime();
};

void BlockControlsPanel(BlockControlsPanelContext& context, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout);
void BlockSettingsControls(const BlockControlsPanelContext& context, bool verticalLayout, const ImVec2& size = {0.f, 0.f});

} // namespace DigitizerUi::components

#endif
