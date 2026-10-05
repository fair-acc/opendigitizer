#ifndef OPENDIGITIZER_UI_PANEBLOCKS_HPP
#define OPENDIGITIZER_UI_PANEBLOCKS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphSession.hpp"

namespace DigitizerUi {

class PaneBlocks {
public:
    explicit PaneBlocks(gr::UICategory category) noexcept : _category(category) {}

    const std::vector<std::shared_ptr<gr::BlockModel>>& of(const GraphSession& session);

private:
    gr::UICategory                               _category;
    const GraphSession*                          _session = nullptr;
    std::optional<std::uint64_t>                 _topologyGeneration;
    std::vector<std::shared_ptr<gr::BlockModel>> _blocks;
};

} // namespace DigitizerUi

#endif
