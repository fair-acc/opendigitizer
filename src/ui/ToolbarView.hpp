#ifndef OPENDIGITIZER_UI_TOOLBARVIEW_HPP
#define OPENDIGITIZER_UI_TOOLBARVIEW_HPP

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphSession.hpp"
#include "PaneBlocks.hpp"
#include "blocks/ToolbarBlock.hpp"

namespace DigitizerUi {

struct ToolbarSlot {
    std::size_t row       = 0UZ;
    float       width     = 0.f;
    bool        showLabel = true;
};

[[nodiscard]] std::vector<ToolbarSlot> planToolbar(std::span<const toolbar::ItemWidths> items, float rowWidth, float spacing, std::size_t maxRows = 2UZ);

struct ToolbarView {
    void draw(GraphSession& session, bool schedulerControls);

    PaneBlocks                             _blocks{gr::UICategory::Toolbar};
    std::unordered_map<std::string, float> _fixedWidths;
    std::size_t                            _rows = 1UZ;
};

} // namespace DigitizerUi

#endif
