#include "PaneBlocks.hpp"

#include <string>

#include <gnuradio-4.0/Logger.hpp>

namespace DigitizerUi {

namespace {
constexpr inline std::string_view kToolkit = "Dear ImGui";

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

const std::vector<std::shared_ptr<gr::BlockModel>>& PaneBlocks::of(Scheduler& scheduler, const UiGraphModel& graphModel) {
    const void*         schedulerImpl      = scheduler ? static_cast<const void*>(scheduler.operator->()) : nullptr;
    const std::uint64_t topologyGeneration = graphModel.topologyGeneration;
    if (_graphModel == std::addressof(graphModel) && _scheduler == schedulerImpl && _topologyGeneration == topologyGeneration) {
        return _blocks;
    }
    _graphModel         = std::addressof(graphModel);
    _scheduler          = schedulerImpl;
    _topologyGeneration = topologyGeneration;
    _blocks.clear();
    if (!schedulerImpl) {
        return _blocks;
    }

    for (const auto& block : scheduler->graph().blocks()) {
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

} // namespace DigitizerUi
