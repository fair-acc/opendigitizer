#include "GraphModel.hpp"

#include <filesystem>
#include <memory>

#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "components/ImGuiNotify.hpp"

#include "App.hpp"

using namespace std::string_literals;

namespace {
template<typename T>
std::optional<T> getOptionalProperty(const gr::property_map& data, std::string key) {
    auto it = data.find(key);
    if (it != data.end()) {
        auto* result = std::get_if<T>(&(it->second));
        if (!result) {
            return {};
        }
        return {*result};
    } else {
        return {};
    }
}

template<typename T>
T getProperty(const gr::property_map& data, std::string key) {
    return getOptionalProperty<T>(data, key).value_or(T{});
}

template<typename FieldType>
void updateFieldFrom(FieldType& field, const auto& data, const std::string& fieldName) { //
    field = getProperty<FieldType>(data, fieldName);
};
} // namespace

bool UiGraphModel::processMessage(const gr::Message& message) {
    namespace graph = gr::graph::property;
    namespace block = gr::block::property;

    if (!message.data) {
        DigitizerUi::components::Notification::error(fmt::format("Received an error: {}\n", message.data.error().message));
        return false;
    }

    const auto& data = *message.data;

    auto uniqueName = [&data](const std::string& key = "uniqueName"s) {
        auto it = data.find(key);
        if (it == data.end()) {
            return std::string();
        }

        return std::get<std::string>(it->second);
    };

    if (message.endpoint == graph::kBlockEmplaced) {
        handleBlockEmplaced(data);

    } else if (message.endpoint == graph::kBlockRemoved) {
        handleBlockRemoved(uniqueName());

    } else if (message.endpoint == graph::kBlockReplaced) {
        handleBlockRemoved(uniqueName("replacedBlockUniqueName"));
        handleBlockEmplaced(data);

    } else if (message.endpoint == graph::kBlockInspected) {
        handleBlockDataUpdated(uniqueName(), data);

    } else if (message.endpoint == block::kSetting) {
        // serviceNames is used for block's unique name in settings messages
        handleBlockSettingsChanged(message.serviceName, data);

    } else if (message.endpoint == block::kStagedSetting) {
        // serviceNames is used for block's unique name in settings messages
        handleBlockSettingsStaged(message.serviceName, data);

    } else if (message.endpoint == graph::kEdgeEmplaced) {
        handleEdgeEmplaced(data);

    } else if (message.endpoint == graph::kEdgeRemoved) {
        handleEdgeRemoved(data);

    } else if (message.endpoint == graph::kGraphInspected) {
        handleGraphRedefined(data);

    } else if (message.endpoint == graph::kRegistryBlockTypes) {
        handleAvailableGraphBlockTypes(data);

    } else if (message.endpoint == "LifecycleState") {
        // Nothing to do for lifecycle state changes

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
    } else {
        if (!message.data) {
            DigitizerUi::components::Notification::error(fmt::format("Not processed: {} data: {}\n", message.endpoint, message.data.error().message));
        }
        return false;
    }

    return true;
    //
}

void UiGraphModel::requestGraphUpdate() {
    if (_newGraphDataBeingSet) {
        return;
    }

    gr::Message message;
    message.cmd      = gr::message::Command::Set;
    message.endpoint = gr::graph::property::kGraphInspect;
    message.data     = gr::property_map{};
    sendMessage(std::move(message));
}

void UiGraphModel::requestAvailableBlocksTypesUpdate() {
    gr::Message message;
    message.cmd      = gr::message::Command::Set;
    message.endpoint = gr::graph::property::kRegistryBlockTypes;
    message.data     = gr::property_map{};
    sendMessage(std::move(message));
}

auto UiGraphModel::findBlockIteratorByUniqueName(const std::string& uniqueName) {
    auto it = std::ranges::find_if(_blocks, [&](const auto& block) { return block.blockUniqueName == uniqueName; });
    return std::make_pair(it, it != _blocks.end());
}

UiGraphBlock* UiGraphModel::findBlockByUniqueName(const std::string& uniqueName) {
    auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        return std::addressof(*it);
    } else {
        return nullptr;
    }
}

auto UiGraphModel::findPortIteratorByName(auto& ports, const std::string& portName) {
    auto it = std::ranges::find_if(ports, [&](const auto& port) { return port.portName == portName; });
    return std::make_pair(it, it != ports.end());
}

bool UiGraphModel::handleBlockRemoved(const std::string& uniqueName) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return false;
    }

    // Delete edges for the removed block
    removeEdgesForBlock(*blockIt);
    _blocks.erase(blockIt);
    return true;
}

void UiGraphModel::handleBlockEmplaced(const gr::property_map& blockData) {
    const auto uniqueName  = getProperty<std::string>(blockData, "uniqueName"s);
    const auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        setBlockData(*it, blockData);
    } else {
        auto& newBlock = _blocks.emplace_back(/*owner*/ this);
        setBlockData(newBlock, blockData);
    }
}

void UiGraphModel::handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    setBlockData(*blockIt, blockData);
}

void UiGraphModel::handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }
    for (const auto& [key, value] : data) {
        if (key != "unique_name"s) {
            blockIt->blockSettings.insert_or_assign(key, value);
            blockIt->updateBlockSettingsMetaInformation();
        }
    }
}

void UiGraphModel::handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data) { handleBlockSettingsChanged(uniqueName, data); }

void UiGraphModel::handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    const auto ctx  = std::get<std::string>(data.at("context"));
    auto       time = std::get<std::uint64_t>(data.at("time"));

    blockIt->activeContext = UiGraphBlock::ContextTime{
        .context = ctx,
        .time    = time,
    };
}

void UiGraphModel::handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
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
    blockIt->contexts = contextAndTimes;
}

void UiGraphModel::handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& /* data */) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    blockIt->getAllContexts();
    blockIt->getActiveContext();
}

void UiGraphModel::handleEdgeEmplaced(const gr::property_map& data) {
    UiGraphEdge edge(this);
    if (setEdgeData(edge, data)) {
        _edges.emplace_back(std::move(edge));
    } else {
        // Failed to read edge data
        requestGraphUpdate();
    }
}

void UiGraphModel::handleEdgeRemoved(const gr::property_map& /* data */) {}

void UiGraphModel::handleGraphRedefined(const gr::property_map& data) {
    _newGraphDataBeingSet = true;
    struct scope_guard {
        ~scope_guard() { flag = false; }
        bool& flag;
    };
    scope_guard resetDataBeingSet(_newGraphDataBeingSet);

    // Strictly speaking, UiGraphModel is not a block even if
    // gr::Graph is a gr::Block, but we can set some basic
    // properties like this
    setBlockData(*this, data);

    // Update or create blocks that GR knows
    const auto& children = getProperty<gr::property_map>(data, "children");
    for (const auto& [childUniqueName, blockData] : children) {
        const auto [blockIt, found] = findBlockIteratorByUniqueName(childUniqueName);
        if (found) {
            setBlockData(*blockIt, std::get<gr::property_map>(blockData));
        } else {
            handleBlockEmplaced(std::get<gr::property_map>(blockData));
        }
    }

    // Delete blocks that GR doesn't know about.
    // This is similar to erase-remove, but we need the list of blocks
    // we want to delete in order to disconnect them first.
    const auto toRemove = std::partition(_blocks.begin(), _blocks.end(), [&children](const auto& child) { //
        return children.contains(child.blockUniqueName);
    });
    for (auto it = toRemove; it != _blocks.end(); ++it) {
        removeEdgesForBlock(*it);
    }

    _blocks.erase(toRemove, _blocks.end());

    // Establish new edges
    _edges.clear();
    const auto& edges = getProperty<gr::property_map>(data, "edges");
    for (const auto& [index, edgeData_] : edges) {
        const auto edgeData = std::get<gr::property_map>(edgeData_);

        UiGraphEdge edge(this);
        if (setEdgeData(edge, edgeData)) {
            _edges.emplace_back(std::move(edge));
        } else {
            components::Notification::error("Invalid edge ignored");
        }
    }
}

void UiGraphModel::handleAvailableGraphBlockTypes(const gr::property_map& data) {
    const auto& knownBlockTypesList = getProperty<std::vector<std::string>>(data, "types");
    for (const auto& type : knownBlockTypesList) {
        auto splitterPosition = std::ranges::find(type, '<');

        if (splitterPosition != type.cend()) {
            knownBlockTypes[std::string(type.cbegin(), splitterPosition)].emplace(splitterPosition, type.cend());

        } else {
            knownBlockTypes[std::string(type)].emplace();
        }
    }

    fmt::print("Known block types: {}\n", knownBlockTypes);
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

void UiGraphModel::setBlockData(auto& block, const gr::property_map& blockData) {
    updateFieldFrom(block.blockUniqueName, blockData, "uniqueName"s);
    updateFieldFrom(block.blockName, blockData, "name"s);
    updateFieldFrom(block.blockTypeName, blockData, "typeName"s);

    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(block)>, UiGraphBlock>) {
        updateFieldFrom(block.blockSettings, blockData, "settings"s);
        updateFieldFrom(block.blockMetaInformation, blockData, "metaInformation"s);

        block.blockSettings.erase("unique_name"s);
        block.updateBlockSettingsMetaInformation();

        updateFieldFrom(block.blockCategory, blockData, "blockCategory"s);
        updateFieldFrom(block.blockUiCategory, blockData, "uiCategory"s);
        updateFieldFrom(block.blockIsBlocking, blockData, "isBlocking"s);

        auto processPorts = [&block, &blockData](auto& portsCollection, std::string portsField, gr::PortDirection direction) {
            portsCollection.clear();
            for (const auto& [portName, portData_] : getProperty<gr::property_map>(blockData, portsField)) {
                const auto& portData = std::get<gr::property_map>(portData_);
                auto&       port     = portsCollection.emplace_back(/*owner*/ std::addressof(block));
                port.portName        = portName;
                port.portType        = getProperty<std::string>(portData, "type"s);
                port.portDirection   = direction;
            }
        };

        processPorts(block.inputPorts, "inputPorts"s, gr::PortDirection::INPUT);
        processPorts(block.outputPorts, "outputPorts"s, gr::PortDirection::OUTPUT);

        block.getAllContexts();
        block.getActiveContext();
    }

    // TODO: When adding support for nested graphs, need to process
    // getProperty<gr::property_map>(blockData, "children"s))
    // getProperty<gr::property_map>(blockData, "edges"s))
}

bool UiGraphModel::setEdgeData(auto& edge, const gr::property_map& edgeData) {
    updateFieldFrom(edge.edgeSourceBlockName, edgeData, "sourceBlock"s);
    updateFieldFrom(edge.edgeDestinationBlockName, edgeData, "destinationBlock"s);

    auto portDefinitionFor = [&edgeData](const std::string& key) -> gr::PortDefinition {
        auto stringPortDefinition = getOptionalProperty<std::string>(edgeData, key);
        if (stringPortDefinition) {
            return gr::PortDefinition(*stringPortDefinition);
        } else {
            auto topLevel = getProperty<std::size_t>(edgeData, key + ".topLevel");
            auto subIndex = getProperty<std::size_t>(edgeData, key + ".subIndex");
            return gr::PortDefinition(topLevel, subIndex);
        }
    };

    edge.edgeSourcePortDefinition      = portDefinitionFor("sourcePort"s);
    edge.edgeDestinationPortDefinition = portDefinitionFor("destinationPort"s);

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
        return false;
    }

    updateFieldFrom(edge.edgeWeight, edgeData, "weight"s);
    updateFieldFrom(edge.edgeName, edgeData, "edgeName"s);
    updateFieldFrom(edge.edgeType, edgeData, "edgeType"s);
    updateFieldFrom(edge.edgeMinBufferSize, edgeData, "minBufferSize"s);
    updateFieldFrom(edge.edgeBufferSize, edgeData, "bufferSize"s);
    updateFieldFrom(edge.edgeState, edgeData, "edgeState"s);
    updateFieldFrom(edge.edgeNReaders, edgeData, "nReaders"s);
    updateFieldFrom(edge.edgeNWriters, edgeData, "nWriters"s);
    return true;
}

void UiGraphModel::removeEdgesForBlock(UiGraphBlock& block) {
    std::erase_if(_edges, [blockPtr = std::addressof(block)](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    });
}

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree) const { return blockInTree(block, tree, UiGraphPort::Role::Source) || blockInTree(block, tree, UiGraphPort::Role::Destination); }

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const {
    if (&block == &tree) {
        return true;
    }

    const UiGraphPort::Role role1 = direction == UiGraphPort::Role::Source ? UiGraphPort::Role::Destination : UiGraphPort::Role::Source;
    const UiGraphPort::Role role2 = direction;

    auto edges = _edges | std::views::filter([&](const auto& edge) { return edge.getBlock(role1) == &tree; });
    for (auto edge : edges) {
        auto neighbourBlock = edge.getBlock(role2);
        if (neighbourBlock && blockInTree(block, *neighbourBlock, direction)) {
            return true;
        }
    }

    return false;
}

bool UiGraphBlock::isConnected() const {
    return std::ranges::find_if(ownerGraph->edges(), [blockPtr = this](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    }) != ownerGraph->edges().cend();
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

void UiGraphBlock::updateBlockSettingsMetaInformation() {
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
