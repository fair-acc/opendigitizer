#ifndef OPENDIGITIZER_UI_COMPONENTS_BLOCK_NEIGHBORS_PREVIEW_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_BLOCK_NEIGHBORS_PREVIEW_HPP_

#include <imgui.h>

namespace gr::od::components {

struct BlockControlsPanelContext;

void blockNeighboursPreview(BlockControlsPanelContext&, ImVec2 availableSize);

} // namespace gr::od::components

#endif
