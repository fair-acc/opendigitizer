#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_NEIGHBORS_PREVIEW_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_NEIGHBORS_PREVIEW_HPP_

#include <imgui.h>

struct UiGraphBlock;

namespace DigitizerUi::components {

struct BlockControlsPanelContext;

void BlockNeighboursPreview(const BlockControlsPanelContext&, ImVec2 availableSize);

} // namespace DigitizerUi::components

#endif
