#include "PaneBlocks.hpp"

#include <algorithm>
#include <string>

#include <gnuradio-4.0/Logger.hpp>

namespace DigitizerUi {

namespace {
constexpr inline std::string_view kToolkit = "ImGui";

std::string toolkitOf(const gr::BlockModel& block) {
    const gr::property_map& metaInformation = block.metaInformation();
    const auto              drawable        = metaInformation.find_value(std::string("Drawable"), std::pmr::get_default_resource());
    if (!drawable) {
        return {};
    }
    const auto drawableInfo = drawable->get_if<gr::property_map>();
    if (!drawableInfo) {
        return {};
    }
    return drawableInfo->find_value(std::string("Toolkit"), std::pmr::get_default_resource()).value_or(gr::pmt::Value{}).value_or(std::string());
}
} // namespace

const std::vector<std::shared_ptr<gr::BlockModel>>& PaneBlocks::of(const GraphSession& session) {
    const std::uint64_t topologyGeneration = session.graphModel.topologyGeneration;
    if ((_session == std::addressof(session) && _topologyGeneration == topologyGeneration) || session.isExchangingGraph()) {
        return _blocks; // the graph may be swapped while a .grc is set
    }
    _session            = std::addressof(session);
    _topologyGeneration = topologyGeneration;
    _blocks.clear();
    if (!session) {
        return _blocks;
    }

    for (const auto& block : session.graph().blocks()) {
        if (block->uiCategory() != _category) {
            continue;
        }
        if (const std::string toolkit = toolkitOf(*block); toolkit != kToolkit) {
            gr::log::warning("{} block '{}' is drawn with toolkit '{}', not '{}': not shown", gr::meta::enumName(_category).value_or(""), block->uniqueName(), toolkit, kToolkit);
            continue;
        }
        _blocks.push_back(block);
    }
    return _blocks;
}

std::shared_ptr<gr::BlockModel> findDrawableBlock(const GraphSession& session, gr::UICategory category, std::string_view uniqueName) {
    if (!session || session.isExchangingGraph()) {
        return nullptr;
    }
    const auto blocks = session.graph().blocks();
    const auto found  = std::ranges::find_if(blocks, [&](const auto& block) { return block->uniqueName() == uniqueName && block->uiCategory() == category && toolkitOf(*block) == kToolkit; });
    return found == blocks.end() ? nullptr : *found;
}

} // namespace DigitizerUi
