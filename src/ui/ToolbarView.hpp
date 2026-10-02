#ifndef OPENDIGITIZER_UI_TOOLBARVIEW_HPP
#define OPENDIGITIZER_UI_TOOLBARVIEW_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "Dashboard.hpp"

namespace DigitizerUi {

/// Draws, in a row, the 'Dear ImGui' toolbar blocks (UICategory::Toolbar) of the dashboard's flowgraph in graph order.
/// The blocks are ordinary blocks of the flowgraph; the row is rebuilt when the flowgraph changes. Toolbar blocks of
/// other toolkits are skipped with a warning.
class ToolbarView {
public:
    void draw(Dashboard& dashboard);

    [[nodiscard]] const std::vector<std::shared_ptr<gr::BlockModel>>& blocks() const noexcept { return _blocks; }

private:
    const Dashboard*                             _dashboard = nullptr;
    const void*                                  _scheduler = nullptr;
    std::optional<std::uint64_t>                 _topologyGeneration;
    std::vector<std::shared_ptr<gr::BlockModel>> _blocks;

    void refreshBlocks(Dashboard& dashboard);
};

} // namespace DigitizerUi

#endif
