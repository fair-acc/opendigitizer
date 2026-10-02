#ifndef OPENDIGITIZER_UI_PANEBLOCKS_HPP
#define OPENDIGITIZER_UI_PANEBLOCKS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "Dashboard.hpp"

namespace DigitizerUi {

/// The 'Dear ImGui' blocks of one UICategory at the top level of a dashboard's flowgraph, in graph order, for the pane
/// that draws them (toolbar, status bar). The list is rebuilt when the flowgraph changes; blocks of other toolkits are
/// skipped with a warning.
class PaneBlocks {
public:
    explicit PaneBlocks(gr::UICategory category) noexcept : _category(category) {}

    const std::vector<std::shared_ptr<gr::BlockModel>>& of(Dashboard& dashboard);

    [[nodiscard]] const std::vector<std::shared_ptr<gr::BlockModel>>& blocks() const noexcept { return _blocks; }

private:
    gr::UICategory                               _category;
    const Dashboard*                             _dashboard = nullptr;
    const void*                                  _scheduler = nullptr;
    std::optional<std::uint64_t>                 _topologyGeneration;
    std::vector<std::shared_ptr<gr::BlockModel>> _blocks;
};

} // namespace DigitizerUi

#endif
