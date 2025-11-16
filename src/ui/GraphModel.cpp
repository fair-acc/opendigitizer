#include "GraphModel.hpp"

#include <filesystem>
#include <memory>

#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "components/ImGuiNotify.hpp"

#include <scope_exit.hpp>

#include "App.hpp"

using namespace std::string_literals;

namespace {
template<typename T, bool allow_conversion = false>
inline std::expected<T, gr::Error> getOptionalProperty(const gr::property_map& map, std::string_view propertyName) {
    auto it = map.find(propertyName);
    if (it == map.cend()) {
        return std::unexpected(gr::Error(std::format("Missing field {} in YAML object", propertyName)));
    }

    if constexpr (!allow_conversion) {
        auto* value = std::get_if<T>(&it->second);
        if (value == nullptr) {
            return std::unexpected(gr::Error(std::format("Field {} in YAML object has an incorrect type index={} instead of {}", propertyName, it->second.index(), gr::meta::type_name<T>())));
        }

        return {*value};
    } else {
        return std::visit(gr::meta::overloaded(                                                                         //
                              [](std::convertible_to<T> auto value) -> std::expected<T, gr::Error> { return {value}; }, //
                              [&](auto) -> std::expected<T, gr::Error> { return std::unexpected(gr::Error(std::format("Field {} in YAML object has an incorrect type index={} instead of {}", propertyName, it->second.index(), gr::meta::type_name<T>()))); }),
            it->second);
    }
}

template<typename T, bool allow_conversion = false>
inline std::expected<T, gr::Error> getOptionalProperty(const gr::property_map& map, std::string_view propertyName, auto... propertySubNames)
requires(sizeof...(propertySubNames) > 0)
{
    static_assert((std::is_convertible_v<decltype(propertySubNames), std::string_view> && ...));
    auto it = map.find(propertyName);
    if (it == map.cend()) {
        return std::unexpected(gr::Error(std::format("Missing field {} in YAML object", propertyName)));
    }

    auto* value = std::get_if<gr::property_map>(&it->second);
    if (value == nullptr) {
        return std::unexpected(gr::Error(std::format("Field {} in YAML object has an incorrect type index={} instead of gr::property_map", propertyName, it->second.index())));
    }

    return getOptionalProperty<T, allow_conversion>(*value, propertySubNames...);
}

template<typename T, typename... Keys>
T getProperty(const gr::property_map& data, const Keys&... keys) {
    static_assert((std::is_convertible_v<Keys, std::string_view> && ...));
    return getOptionalProperty<T>(data, keys...).value_or(T{});
}

template<typename FieldType, typename... Keys>
void updateFieldFrom(FieldType& field, const auto& data, const FieldType& defaultValue, const Keys&... keys) { //
    field = getOptionalProperty<FieldType>(data, keys...).value_or(defaultValue);
};
} // namespace

auto UiGraphBlock::findBlockIteratorByUniqueName(const std::string& uniqueName) {
    auto it = std::ranges::find_if(childBlocks, [&](const auto& block) { return block->blockUniqueName == uniqueName || block->schedulerUniqueName == uniqueName; });
    return std::make_pair(it, it != childBlocks.end());
}

UiGraphBlock* UiGraphBlock::findBlockByUniqueName(const std::string& uniqueName) {
    auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        return it->get();
    } else {
        return nullptr;
    }
}

auto UiGraphBlock::findPortIteratorByName(auto& ports, const std::string& portName) {
    auto it = std::ranges::find_if(ports, [&](const auto& port) { return port.portName == portName; });
    return std::make_pair(it, it != ports.end());
}

void UiGraphBlock::handleChildBlockEmplaced(const gr::property_map& blockData) {
    const auto uniqueName  = getProperty<std::string>(blockData, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    const auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        UiGraphBlock& block = *it->get();
        block.setBlockData(blockData);
    } else {
        auto newBlock = std::make_unique<UiGraphBlock>(/*owner*/ ownerGraph);
        newBlock->setBlockData(blockData);
        childBlocks.push_back(std::move(newBlock));
    }
    shouldRearrangeBlocks = true;
}

void UiGraphBlock::setGraphData(const gr::property_map& data) {
    newGraphDataBeingSet           = true;
    Digitizer::utils::scope_exit _ = [&] { newGraphDataBeingSet = false; };

    setBlockData(data);

    // Update or create blocks that GR knows
    const auto& children = getProperty<gr::property_map>(data, "children"s);
    for (const auto& [childUniqueName, blockData_] : children) {
        const auto [blockIt, found] = findBlockIteratorByUniqueName(childUniqueName);
        const auto& blockData       = std::get<gr::property_map>(blockData_);
        if (found) {
            (*blockIt)->setGraphData(blockData);
        } else {
            handleChildBlockEmplaced(blockData);
        }
    }

    // Delete blocks that GR doesn't know about.
    // This is similar to erase-remove, but we need the list of blocks
    // we want to delete in order to disconnect them first.
    const auto toRemove = std::partition(childBlocks.begin(), childBlocks.end(), [&children](const auto& child) { //
        return children.contains(child->blockUniqueName);
    });
    for (auto it = toRemove; it != childBlocks.end(); ++it) {
        removeEdgesForBlock(**it);
    }

    childBlocks.erase(toRemove, childBlocks.end());

    // Establish new edges
    childEdges.clear();
    const auto& edges = getProperty<gr::property_map>(data, "edges"s);
    for (const auto& [index, edgeData_] : edges) {
        const auto edgeData = std::get<gr::property_map>(edgeData_);

        UiGraphEdge edge;
        if (setEdgeData(edge, edgeData)) {
            childEdges.emplace_back(std::move(edge));
        } else {
            components::Notification::error("Invalid edge ignored");
        }
    }

    shouldRearrangeBlocks = true;
}

void UiGraphBlock::setBlockData(const gr::property_map& blockData) {
    if (blockUniqueName.empty()) {
        updateFieldFrom(blockUniqueName, blockData, {}, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    }

    updateFieldFrom(blockName, blockData, blockName, "name"s);
    if (blockName.empty()) {
        updateFieldFrom(blockName, blockData, {}, "parameters"s, "name"s);
    }

    updateFieldFrom(blockTypeName, blockData, blockTypeName, "type_name"s);
    if (blockTypeName.empty()) {
        updateFieldFrom(blockTypeName, blockData, {}, "id"s);
    }

    updateFieldFrom(blockSettings, blockData, {}, gr::serialization_fields::BLOCK_PARAMETERS);
    updateFieldFrom(blockMetaInformation, blockData, {}, gr::serialization_fields::BLOCK_META_INFORMATION);

    blockSettings.erase(std::string(gr::serialization_fields::BLOCK_UNIQUE_NAME));
    updateBlockSettingsMetaInformation();

    updateExportedPorts();

    updateFieldFrom(blockCategory, blockData, {}, "block_category"s);
    updateFieldFrom(blockUiCategory, blockData, {}, "ui_category"s);
    updateFieldFrom(blockIsBlocking, blockData, {}, "is_blocking"s);

    auto processPorts = [&blockData, this](auto& portsCollection, std::string_view portsField, gr::PortDirection direction) {
        portsCollection.clear();
        for (const auto& [portName, portData_] : getProperty<gr::property_map>(blockData, portsField)) {
            const auto& portData = std::get<gr::property_map>(portData_);
            auto&       port     = portsCollection.emplace_back(/*owner*/ this);
            port.portName        = portName;
            port.portType        = getProperty<std::string>(portData, "type"s);
            port.portDirection   = direction;
        }
    };

    processPorts(inputPorts, gr::serialization_fields::BLOCK_INPUT_PORTS, gr::PortDirection::INPUT);
    processPorts(outputPorts, gr::serialization_fields::BLOCK_OUTPUT_PORTS, gr::PortDirection::OUTPUT);


    if (auto parametersIt = blockData.find("parameters"); parametersIt != blockData.end()) {
        const auto* parameters = std::get_if<gr::property_map>(&(parametersIt->second));
        if (parameters) {
            if (auto uiConstraintsIt = parameters->find("ui_constraints"); uiConstraintsIt != parameters->end()) {
                const auto* uiConstraints = std::get_if<gr::property_map>(&(uiConstraintsIt->second));
                if (uiConstraints) {
                    auto x = getOptionalProperty<float, true>(*uiConstraints, "x");
                    auto y = getOptionalProperty<float, true>(*uiConstraints, "y");

                    if (x && y && (!storedXY.has_value() || (storedXY->x != *x || storedXY->y != *y))) {
                        storedXY = UiGraphBlock::StoredXY{
                            .x = *x,
                            .y = *y,
                        };
                        updatePosition = true;
                    }
                }
            }
        }
    }

    shouldRearrangeBlocks = true;
}

bool UiGraphBlock::setEdgeData(auto& edge, const gr::property_map& edgeData) {
    updateFieldFrom(edge.edgeSourceBlockName, edgeData, {}, gr::serialization_fields::EDGE_SOURCE_BLOCK);
    updateFieldFrom(edge.edgeDestinationBlockName, edgeData, {}, gr::serialization_fields::EDGE_DESTINATION_BLOCK);

    auto portDefinitionFor = [&edgeData](std::string key) -> gr::PortDefinition {
        auto stringPortDefinition = getOptionalProperty<std::string>(edgeData, key);
        if (stringPortDefinition) {
            return gr::PortDefinition(*stringPortDefinition);
        } else {
            auto topLevel = getProperty<std::size_t>(edgeData, key + ".top_level");
            auto subIndex = getProperty<std::size_t>(edgeData, key + ".sub_index");
            return gr::PortDefinition(topLevel, subIndex);
        }
    };

    edge.edgeSourcePortDefinition      = portDefinitionFor(std::string(gr::serialization_fields::EDGE_SOURCE_PORT));
    edge.edgeDestinationPortDefinition = portDefinitionFor(std::string(gr::serialization_fields::EDGE_DESTINATION_PORT));

    auto findPortFor = [this](std::string& currentBlockName, auto member, const gr::PortDefinition& portDefinition_) -> UiGraphPort* {
        auto [it, found] = findBlockIteratorByUniqueName(currentBlockName);
        if (!found) {
            return nullptr;
        }

        auto& block = *it;
        auto& ports = std::invoke(member, block);

        return std::visit(gr::meta::overloaded{//
                              [&ports](const gr::PortDefinition::IndexBased& indexBasedDefinition) -> UiGraphPort* {
                                  // TODO: sub-index for ports -- when we add UI support for
                                  // port arrays
                                  if (indexBasedDefinition.topLevel >= ports.size()) {
                                      return nullptr;
                                  }

                                  return std::addressof(ports[indexBasedDefinition.topLevel]);
                              },
                              [&ports](const gr::PortDefinition::StringBased& stringBasedDefinition) -> UiGraphPort* { //
                                  auto portIt = std::ranges::find_if(ports, [&](const auto& port) {                    //
                                      return port.portName == stringBasedDefinition.name;
                                  });
                                  if (portIt == ports.end()) {
                                      return nullptr;
                                  }
                                  return std::addressof(*portIt);
                              }},
            portDefinition_.definition);
    };

    edge.edgeSourcePort      = findPortFor(edge.edgeSourceBlockName, &UiGraphBlock::outputPorts, edge.edgeSourcePortDefinition);
    edge.edgeDestinationPort = findPortFor(edge.edgeDestinationBlockName, &UiGraphBlock::inputPorts, edge.edgeDestinationPortDefinition);

    if (!edge.edgeSourcePort || !edge.edgeDestinationPort) {
        std::println("Warning: Edge definition invalid source {} destination {}", !!edge.edgeSourcePort, !!edge.edgeDestinationPort);
        return false;
    }

    updateFieldFrom(edge.edgeWeight, edgeData, {}, gr::serialization_fields::EDGE_WEIGHT);
    updateFieldFrom(edge.edgeName, edgeData, {}, gr::serialization_fields::EDGE_NAME);
    updateFieldFrom(edge.edgeType, edgeData, {}, gr::serialization_fields::EDGE_TYPE);
    updateFieldFrom(edge.edgeMinBufferSize, edgeData, {}, gr::serialization_fields::EDGE_MIN_BUFFER_SIZE);
    updateFieldFrom(edge.edgeBufferSize, edgeData, {}, gr::serialization_fields::EDGE_BUFFER_SIZE);
    updateFieldFrom(edge.edgeState, edgeData, {}, gr::serialization_fields::EDGE_EDGE_STATE);
    updateFieldFrom(edge.edgeNReaders, edgeData, {}, gr::serialization_fields::EDGE_N_READERS);
    updateFieldFrom(edge.edgeNWriters, edgeData, {}, gr::serialization_fields::EDGE_N_WRITERS);
    return true;
}

void UiGraphBlock::removeEdgesForBlock(UiGraphBlock& block) {
    std::erase_if(childEdges, [blockPtr = std::addressof(block)](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    });
}

bool UiGraphBlock::isConnected() const {
    return std::ranges::find_if(ownerGraph->rootBlock.childEdges, [blockPtr = this](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    }) != ownerGraph->rootBlock.childEdges.cend();
}

void UiGraphBlock::getAllContexts() { ownerGraph->sendMessage(gr::Message{.cmd = gr::Message::Get, .serviceName = blockUniqueName, .clientRequestID = "all", .endpoint = gr::block::property::kSettingsContexts, .data = {}}); }

void UiGraphBlock::setActiveContext(const ContextTime& contextTime) {
    const auto& [context, time] = contextTime;
    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Set,
        .serviceName     = blockUniqueName,
        .clientRequestID = "activate",
        .endpoint        = gr::block::property::kActiveContext,
        .data            = gr::property_map{{"context", context}, {"time", time}},
    });
}

void UiGraphBlock::getActiveContext() { ownerGraph->sendMessage(gr::Message{.cmd = gr::Message::Get, .serviceName = blockUniqueName, .clientRequestID = "active", .endpoint = gr::block::property::kActiveContext, .data = {}}); }

void UiGraphBlock::addContext(const ContextTime& contextTime) {
    const auto& [context, time] = contextTime;
    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Set,
        .serviceName     = blockUniqueName,
        .clientRequestID = "add",
        .endpoint        = gr::block::property::kSettingsCtx,
        .data            = gr::property_map{{"context", context}, {"time", time}},
    });
}

void UiGraphBlock::removeContext(const ContextTime& contextTime) {
    const auto& [context, time] = contextTime;
    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Disconnect,
        .serviceName     = blockUniqueName,
        .clientRequestID = "rm",
        .endpoint        = gr::block::property::kSettingsCtx,
        .data            = gr::property_map{{"context", context}, {"time", time}},
    });
}

void UiGraphBlock::storeXY() {
    storedXY = UiGraphBlock::StoredXY{
        .x = view->x,
        .y = view->y,
    };

    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Set,
        .serviceName     = blockUniqueName,
        .clientRequestID = "ui_constraints",
        .endpoint        = gr::block::property::kSetting,
        .data =
            gr::property_map{
                {
                    "ui_constraints",
                    gr::property_map{
                        {"x", storedXY->x},
                        {"y", storedXY->y},
                    },
                },
            },
    });
}

void UiGraphBlock::updateExportedPorts() {
    auto readExportedPorts = [this](const std::string& key) {
        std::map<std::string, std::set<std::string>> result;

        auto it = blockMetaInformation.find(key);
        if (it == blockMetaInformation.end()) {
            return result;
        }

        auto map = std::get<gr::property_map>(it->second);
        for (const auto& [subBlockName, portNames] : map) {
            for (const auto& portName : std::get<std::vector<std::string>>(portNames)) {
                result[subBlockName].insert(portName);
            }
        }

        return result;
    };
    exportedInputPorts  = readExportedPorts("exportedInputPorts"s);
    exportedOutputPorts = readExportedPorts("exportedOutputPorts"s);
}

void UiGraphBlock::updateBlockSettingsMetaInformation() {
    if (!blockMetaInformation.empty()) {
        // meta information is not changing, don't re-read it
        return;
    }

    for (const auto& [settingKey, _] : blockSettings) {
        auto findMetaInformation = [this, &settingKey]<typename TDesired>(const std::string& key, const std::string& attr, TDesired defaultResult) -> TDesired {
            const auto it = this->blockMetaInformation.find(key + "::" + attr);
            if (const auto unitPtr = std::get_if<TDesired>(&it->second); unitPtr) {
                return *unitPtr;
            }
            return defaultResult;
        };
        const auto description = findMetaInformation(settingKey, "description", settingKey);
        const auto unit        = findMetaInformation(settingKey, "unit", std::string());
        const auto isVisible   = findMetaInformation(settingKey, "visible", false);

        blockSettingsMetaInformation[settingKey] = SettingsMetaInformation{.unit = unit, .description = description, .isVisible = isVisible};
    }
}

auto UiGraphModel::recursiveFindBlockByUniqueName(const std::string& uniqueName) {
    struct Result {
        UiGraphBlock* parentGraph = nullptr;
        UiGraphBlock* block       = nullptr;
    };

    if (rootBlock.blockUniqueName == uniqueName || rootBlock.schedulerUniqueName == uniqueName) {
        return Result{nullptr, std::addressof(rootBlock)};
    }

    std::deque<UiGraphBlock*> toProcess{std::addressof(rootBlock)};

    while (!toProcess.empty()) {
        auto* currentGraph = toProcess.front();
        toProcess.pop_front();

        for (auto& block : currentGraph->childBlocks) {
            if (block->blockUniqueName == uniqueName || block->schedulerUniqueName == uniqueName) {
                return Result{currentGraph, block.get()};
            }

            if (!block->childBlocks.empty()) {
                toProcess.push_back(block.get());
            }
        }
    }

    return Result{nullptr, nullptr};
}

void updateKnownTypeMap(auto& map, const auto& data) {
    map.clear();
    const auto& knownList = getProperty<std::vector<std::string>>(data, "types"s);
    for (const auto& type : knownList) {
        auto splitterPosition = std::ranges::find(type, '<');

        if (splitterPosition != type.cend()) {
            map[std::string(type.cbegin(), splitterPosition)].emplace(splitterPosition, type.cend());

        } else {
            map[std::string(type)].emplace();
        }
    }
}

void UiGraphModel::handleAvailableGraphBlockTypes(const gr::property_map& data) { updateKnownTypeMap(knownBlockTypes, data); }

void UiGraphModel::handleAvailableGraphSchedulerTypes(const gr::property_map& data) {
    updateKnownTypeMap(knownSchedulerTypes, data);
    knownSchedulerTypes["gr::Graph"] = {"<>"};
}

UiGraphModel::AvailableParametrizationsResult UiGraphModel::availableParametrizationsFor(const std::string& fullBlockType) const {

    auto blockTypeSplitter = std::ranges::find(fullBlockType, '<');

    if (blockTypeSplitter != fullBlockType.cend()) {
        const auto currentBlockBaseType            = std::string(fullBlockType.cbegin(), blockTypeSplitter);
        const auto currentBlockParametrizationType = std::string(blockTypeSplitter, fullBlockType.cend());

        if (auto typeIt = knownBlockTypes.find(currentBlockBaseType); typeIt != knownBlockTypes.cend()) {
            return UiGraphModel::AvailableParametrizationsResult{currentBlockBaseType, currentBlockParametrizationType, std::addressof(typeIt->second)};
        }
    }

    return UiGraphModel::AvailableParametrizationsResult{std::string(), std::string(), nullptr};
}

bool UiGraphModel::processMessage(const gr::Message& message) {
    namespace graph     = gr::graph::property;
    namespace scheduler = gr::scheduler::property;
    namespace block     = gr::block::property;

    if (!message.data) {
        DigitizerUi::components::Notification::error(std::format("Received an error: {}\n", message.data.error().message));
        return false;
    }

    const auto& data = *message.data;

    auto uniqueName = [&data](const std::string_view& key = gr::serialization_fields::BLOCK_UNIQUE_NAME) {
        auto it = data.find(key);
        if (it == data.end()) {
            return std::string();
        }

        return std::get<std::string>(it->second);
    };

    // We can not really process messages until we get the initial graph contents
    if (rootBlock.blockUniqueName.empty()) {
        if (message.endpoint == scheduler::kSchedulerInspected) {
            // The SchedulerInspected message is special. If we do not know about
            // anything WRT the GR flowgraph, we need to deduce that the root graph
            // is the top level graph in the GraphInspected message
            rootBlock.schedulerUniqueName = message.serviceName;
            const auto& children          = getProperty<gr::property_map>(data, "children"s);
            // There is only one child in the scheduler -- the contained graph
            for (const auto& [graphName, _] : children) {
                rootBlock.blockUniqueName = graphName;
            }
        } else if (message.endpoint == graph::kGraphInspected) {
            // The GraphInspected message is special. If we do not know about
            // anything WRT the GR flowgraph, we need to deduce that the root graph
            // is the top level graph in the GraphInspected message
            rootBlock.blockUniqueName = message.serviceName;
        } else {
            // We can not process any messages until we get the Graph contents
            requestGraphUpdate();
            return false;
        }
    }

    auto targetGraphIt   = data.find("_targetGraph"s);
    auto targetBlockName = targetGraphIt == data.end() ? message.serviceName : std::get<std::string>(targetGraphIt->second);

    auto [_, targetBlock] = recursiveFindBlockByUniqueName(targetBlockName);

    if (!targetBlock) {
        components::Notification::error(std::format("Got a message for an unknown block {} {}", message.serviceName, message.endpoint));
        requestGraphUpdate();
        return false;
    }

    if (message.endpoint == scheduler::kBlockEmplaced) {
        targetBlock->handleChildBlockEmplaced(data);

    } else if (message.endpoint == scheduler::kBlockRemoved) {
        handleBlockRemoved(uniqueName());

    } else if (message.endpoint == scheduler::kBlockReplaced) {
        handleBlockRemoved(uniqueName("replacedBlockUniqueName"));
        targetBlock->handleChildBlockEmplaced(data);

    } else if (message.endpoint == graph::kBlockInspected) {
        handleBlockDataUpdated(uniqueName(), data);

    } else if (message.endpoint == block::kSetting) {
        // serviceName is used for block's unique name in settings messages
        handleBlockSettingsChanged(message.serviceName, data);

    } else if (message.endpoint == block::kStagedSetting) {
        // serviceName is used for block's unique name in settings messages
        handleBlockSettingsStaged(message.serviceName, data);

    } else if (message.endpoint == scheduler::kEdgeEmplaced) {
        handleEdgeEmplaced(data);

    } else if (message.endpoint == scheduler::kEdgeRemoved) {
        handleEdgeRemoved(data);

    } else if (message.endpoint == scheduler::kSchedulerInspected) {
        const auto& children = getProperty<gr::property_map>(data, "children"s);
        // There is only one child in the scheduler -- the contained graph
        for (const auto& [graphName, graphData] : children) {
            targetBlock->setGraphData(std::get<gr::property_map>(graphData));
        }

    } else if (message.endpoint == graph::kGraphInspected) {
        targetBlock->setGraphData(data);

    } else if (message.endpoint == graph::kRegistryBlockTypes) {
        handleAvailableGraphBlockTypes(data);

    } else if (message.endpoint == graph::kRegistrySchedulerTypes) {
        handleAvailableGraphSchedulerTypes(data);

    } else if (message.endpoint == "LifecycleState") {
        // Nothing to do for lifecycle state changes
        auto valueIt = data.find("state");
        if (valueIt != data.end() && std::get<std::string>(valueIt->second) == "RUNNING") {
            std::println("Lifecycle state changed to: RUNNING, requesting update");
            requestGraphUpdate();
            requestAvailableBlocksTypesUpdate();
        }

    } else if (message.endpoint == block::kActiveContext) {
        handleBlockActiveContext(message.serviceName, data);

    } else if (message.endpoint == block::kSettingsContexts) {
        if (message.clientRequestID == "all") {
            handleBlockAllContexts(message.serviceName, data);
        }
    } else if (message.endpoint == block::kSettingsCtx) {
        if (message.clientRequestID == "add" || message.clientRequestID == "rm") {
            handleBlockAddOrRemoveContext(message.serviceName, data);
        }

    } else if (message.endpoint == scheduler::kGraphGRC) {
        if (auto valueIt = data.find("value"); valueIt != data.end()) {
            std::println("Retrieved Graph GRC YAML");
            m_localFlowgraphGrc = std::get<std::string>(valueIt->second);
        } else {
            assert(false);
        }
    } else {
        if (!message.data) {
            DigitizerUi::components::Notification::error(std::format("Not processed: {} data: {}\n", message.endpoint, message.data.error().message));
        }
        return false;
    }

    return true;
}

void UiGraphModel::requestGraphUpdate() {
    if (rootBlock.newGraphDataBeingSet) {
        return;
    }

    gr::Message message;
    message.cmd      = gr::message::Command::Get;
    message.endpoint = gr::scheduler::property::kSchedulerInspect;
    message.data     = gr::property_map{};
    sendMessage(std::move(message));
}

void UiGraphModel::requestAvailableBlocksTypesUpdate() {
    // Get known block types
    {
        gr::Message message;
        message.cmd      = gr::message::Command::Get;
        message.endpoint = gr::graph::property::kRegistryBlockTypes;
        message.data     = gr::property_map{};
        sendMessage(std::move(message));
    }

    // Get known scheduler types
    {
        gr::Message message;
        message.cmd      = gr::message::Command::Get;
        message.endpoint = gr::graph::property::kRegistrySchedulerTypes;
        message.data     = gr::property_map{};
        sendMessage(std::move(message));
    }
}

bool UiGraphModel::handleBlockRemoved(const std::string& uniqueName) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return false;
    }

    rootBlock.removeEdgesForBlock(*blockIt->get());

    if (blockIt->get() == selectedBlock) {
        selectedBlock = nullptr;
    }

    rootBlock.childBlocks.erase(blockIt);
    rootBlock.shouldRearrangeBlocks = true;

    return true;
}

void UiGraphModel::handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    UiGraphBlock& block = *blockIt->get();
    block.setBlockData(blockData);
}

void UiGraphModel::handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    for (const auto& [key, value] : data) {
        if (key == "ui_constraints"s) {
            const auto map = std::get<gr::property_map>(value);
            if (!map.empty()) {
                const auto x = std::visit(gr::meta::overloaded([](std::convertible_to<float> auto _x) { return static_cast<float>(_x); }, [](auto) { return 0.0f; }), map.at("x"));
                const auto y = std::visit(gr::meta::overloaded([](std::convertible_to<float> auto _y) { return static_cast<float>(_y); }, [](auto) { return 0.0f; }), map.at("y"));

                if (!(*blockIt)->storedXY.has_value() || ((*blockIt)->storedXY.value().x != x || (*blockIt)->storedXY.value().y != y)) {
                    (*blockIt)->storedXY = UiGraphBlock::StoredXY{
                        .x = x,
                        .y = y,
                    };
                    (*blockIt)->updatePosition = true;

                    rootBlock.shouldRearrangeBlocks = true;
                }
            }
        } else if (key != gr::serialization_fields::BLOCK_UNIQUE_NAME) {
            (*blockIt)->blockSettings.insert_or_assign(key, value);
            (*blockIt)->updateBlockSettingsMetaInformation();
        }
    }
}

void UiGraphModel::handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data) { handleBlockSettingsChanged(uniqueName, data); }

void UiGraphModel::handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    const auto ctx  = std::get<std::string>(data.at("context"));
    auto       time = std::get<std::uint64_t>(data.at("time"));

    (*blockIt)->activeContext = UiGraphBlock::ContextTime{
        .context = ctx,
        .time    = time,
    };

    rootBlock.shouldRearrangeBlocks = true;
}

void UiGraphModel::handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    auto contexts = std::get<std::vector<std::string>>(data.at("contexts"));
    auto times    = std::get<std::vector<std::uint64_t>>(data.at("times"));

    std::vector<UiGraphBlock::ContextTime> contextAndTimes;
    for (std::size_t i = 0UZ; i < contexts.size(); ++i) {
        contextAndTimes.emplace_back(UiGraphBlock::ContextTime{
            .context = contexts[i],
            .time    = times[i],
        });
    }
    (*blockIt)->contexts = contextAndTimes;

    rootBlock.shouldRearrangeBlocks = true;
}

void UiGraphModel::handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& /* data */) {
    auto [blockIt, found] = rootBlock.findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    (*blockIt)->getAllContexts();
    (*blockIt)->getActiveContext();

    rootBlock.shouldRearrangeBlocks = true;
}

void UiGraphModel::handleEdgeEmplaced(const gr::property_map& data) {
    UiGraphEdge edge;
    if (rootBlock.setEdgeData(edge, data)) {
        rootBlock.childEdges.emplace_back(std::move(edge));
    } else {
        // Failed to read edge data
        requestGraphUpdate();
    }
}

void UiGraphModel::handleEdgeRemoved(const gr::property_map& /* data */) {}

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree) const { return blockInTree(block, tree, UiGraphPort::Role::Source) || blockInTree(block, tree, UiGraphPort::Role::Destination); }

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const {
    if (&block == &tree) {
        return true;
    }

    const UiGraphPort::Role role1 = direction == UiGraphPort::Role::Source ? UiGraphPort::Role::Destination : UiGraphPort::Role::Source;
    const UiGraphPort::Role role2 = direction;

    auto edges = rootBlock.childEdges | std::views::filter([&](const auto& edge) { return edge.getBlock(role1) == &tree; });
    for (auto edge : edges) {
        auto neighbourBlock = edge.getBlock(role2);
        if (neighbourBlock && blockInTree(block, *neighbourBlock, direction)) {
            return true;
        }
    }

    return false;
}
