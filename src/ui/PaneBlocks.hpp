#ifndef OPENDIGITIZER_UI_PANEBLOCKS_HPP
#define OPENDIGITIZER_UI_PANEBLOCKS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "Dashboard.hpp"

namespace DigitizerUi {

/// The 'Dear ImGui' blocks of one UICategory at the top level of a scheduler's flowgraph, in graph order, for the pane
/// that draws them (toolbar, status bar). The list is rebuilt when the graph model reports a topology change; blocks of
/// other toolkits are skipped with a warning.
class PaneBlocks {
public:
    explicit PaneBlocks(gr::UICategory category) noexcept : _category(category) {}

    const std::vector<std::shared_ptr<gr::BlockModel>>& of(Scheduler& scheduler, const UiGraphModel& graphModel);
    const std::vector<std::shared_ptr<gr::BlockModel>>& of(Dashboard& dashboard) { return of(dashboard.scheduler, dashboard.graphModel); }

    [[nodiscard]] const std::vector<std::shared_ptr<gr::BlockModel>>& blocks() const noexcept { return _blocks; }

private:
    gr::UICategory                               _category;
    const UiGraphModel*                          _graphModel = nullptr;
    const void*                                  _scheduler  = nullptr;
    std::optional<std::uint64_t>                 _topologyGeneration;
    std::vector<std::shared_ptr<gr::BlockModel>> _blocks;
};

} // namespace DigitizerUi

#endif
