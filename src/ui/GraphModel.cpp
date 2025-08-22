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
void pretty_print_map(const gr::property_map& map, std::size_t level = 0) {
    for (const auto& [k, v] : map) {
        std::print("{}{} -> ", std::string(4 * level, ' '), k);
        std::visit(gr::meta::overloaded(
                       [level](const gr::property_map& sub) {
                           std::println();
                           pretty_print_map(sub, level + 1);
                       },
                       [](const std::string& val) {
                           auto newline = std::ranges::find(val, '\n');
                           std::print("'{}'", std::string(val.begin(), newline));
                           if (newline == val.cend()) {
                               std::println("...");
                           } else {
                               std::println(".");
                           }
                       },
                       []<typename T>(const T& val) {
                           if constexpr (std::is_integral_v<T>) {
                               std::println("{}", val);
                           } else if constexpr (std::is_same_v<T, float>) {
                               std::println("{}", val);
                           } else if constexpr (std::is_same_v<T, double>) {
                               std::println("{}", val);
                           } else {
                               std::println("[unsup.]");
                           }
                       }),
            v);
    }
}

template<typename T, bool allow_conversion = false>
inline std::expected<T, gr::Error> getOptionalProperty(const gr::property_map& map, std::string_view propertyName) {
    auto it = map.find(propertyName);
    if (it == map.cend()) {
        return std::unexpected(gr::Error(std::format("Missing field {} in YAML object", propertyName)));
    }

    if constexpr (!allow_conversion) {
        auto* value = std::get_if<T>(&it->second);
        if (value == nullptr) {
            std::println("WARNING: Wrong type of {} in:", propertyName);
            pretty_print_map(map, 1);
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
void updateFieldFrom(FieldType& field, const auto& data, const Keys&... keys) { //
    field = getProperty<FieldType>(data, keys...);
};
} // namespace

bool UiGraphModel::processMessage(const gr::Message& message) {
    namespace graph     = gr::graph::property;
    namespace scheduler = gr::scheduler::property;
    namespace block     = gr::block::property;

    if (!message.data) {
        DigitizerUi::components::Notification::error(std::format("Received an error: {}\n", message.data.error().message));
        return false;
    }

    const auto& data = *message.data;

    auto uniqueName = [&data](const std::string& key = "unique_name"s) {
        auto it = data.find(key);
        if (it == data.end()) {
            std::println("GREPME error {} has no {}", data, key);
            return std::string();
        }

        return std::get<std::string>(it->second);
    };

    if (message.endpoint == scheduler::kBlockEmplaced) {
        handleBlockEmplaced(data);

    } else if (message.endpoint == scheduler::kBlockRemoved) {
        handleBlockRemoved(uniqueName());

    } else if (message.endpoint == scheduler::kBlockReplaced) {
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

    } else if (message.endpoint == scheduler::kEdgeEmplaced) {
        handleEdgeEmplaced(data);

    } else if (message.endpoint == scheduler::kEdgeRemoved) {
        handleEdgeRemoved(data);

    } else if (message.endpoint == graph::kGraphInspected) {
        handleGraphRedefined(data);

    } else if (message.endpoint == graph::kRegistryBlockTypes) {
        handleAvailableGraphBlockTypes(data);

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
    auto it = std::ranges::find_if(_blocks, [&](const auto& block) { return block->blockUniqueName == uniqueName; });
    return std::make_pair(it, it != _blocks.end());
}

UiGraphBlock* UiGraphModel::findBlockByUniqueName(const std::string& uniqueName) {
    auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        return it->get();
    } else {
        return nullptr;
    }
}

bool UiGraphModel::rearrangeBlocks() const { return _rearrangeBlocks; }

void UiGraphModel::setRearrangeBlocks(bool rearrange) { _rearrangeBlocks = rearrange; }

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

    removeEdgesForBlock(*blockIt->get());

    if (blockIt->get() == selectedBlock) {
        selectedBlock = nullptr;
    }

    _blocks.erase(blockIt);
    _rearrangeBlocks = true;

    return true;
}

void UiGraphModel::handleBlockEmplaced(const gr::property_map& blockData) {
    const auto uniqueName  = getProperty<std::string>(blockData, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    const auto [it, found] = findBlockIteratorByUniqueName(uniqueName);
    if (found) {
        UiGraphBlock& block = *it->get();
        setBlockData(block, blockData);
    } else {
        auto newBlock = std::make_unique<UiGraphBlock>(/*owner*/ this);
        setBlockData(*newBlock, blockData);
        _blocks.push_back(std::move(newBlock));
    }
    _rearrangeBlocks = true;
}

void UiGraphModel::handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    UiGraphBlock& block = *blockIt->get();
    setBlockData(block, blockData);
}

void UiGraphModel::handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    for (const auto& [key, value] : data) {
        if (key == "ui_constraints"s) {
            const auto map = std::get<gr::property_map>(value);
            if (!map.empty()) {
                const auto x = std::visit(gr::meta::overloaded([](std::convertible_to<float> auto x) { return static_cast<float>(x); }, [](auto) { return 0.0f; }), map.at("x"));
                const auto y = std::visit(gr::meta::overloaded([](std::convertible_to<float> auto y) { return static_cast<float>(y); }, [](auto) { return 0.0f; }), map.at("y"));

                if (!(*blockIt)->storedXY.has_value() || ((*blockIt)->storedXY.value().x != x || (*blockIt)->storedXY.value().y != y)) {
                    (*blockIt)->storedXY = UiGraphBlock::StoredXY{
                        .x = x,
                        .y = y,
                    };
                    (*blockIt)->updatePosition = true;

                    _rearrangeBlocks = true;
                }
            }
        } else if (key != "unique_name"s) {
            (*blockIt)->blockSettings.insert_or_assign(key, value);
            (*blockIt)->updateBlockSettingsMetaInformation();
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

    (*blockIt)->activeContext = UiGraphBlock::ContextTime{
        .context = ctx,
        .time    = time,
    };

    _rearrangeBlocks = true;
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
    (*blockIt)->contexts = contextAndTimes;

    _rearrangeBlocks = true;
}

void UiGraphModel::handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& /* data */) {
    auto [blockIt, found] = findBlockIteratorByUniqueName(uniqueName);
    if (!found) {
        requestGraphUpdate();
        return;
    }

    (*blockIt)->getAllContexts();
    (*blockIt)->getActiveContext();

    _rearrangeBlocks = true;
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
    _newGraphDataBeingSet                          = true;
    Digitizer::utils::scope_exit resetDataBeingSet = [&] { _newGraphDataBeingSet = false; };

    // Strictly speaking, UiGraphModel is not a block even if
    // gr::Graph is a gr::Block, but we can set some basic
    // properties like this
    setBlockData(*this, data);

    // Update or create blocks that GR knows
    const auto& children = getProperty<gr::property_map>(data, "children"s);
    for (const auto& [childUniqueName, blockData] : children) {
        const auto [blockIt, found] = findBlockIteratorByUniqueName(childUniqueName);
        if (found) {
            setBlockData(**blockIt, std::get<gr::property_map>(blockData));
        } else {
            handleBlockEmplaced(std::get<gr::property_map>(blockData));
        }
    }

    // Delete blocks that GR doesn't know about.
    // This is similar to erase-remove, but we need the list of blocks
    // we want to delete in order to disconnect them first.
    const auto toRemove = std::partition(_blocks.begin(), _blocks.end(), [&children](const auto& child) { //
        return children.contains(child->blockUniqueName);
    });
    for (auto it = toRemove; it != _blocks.end(); ++it) {
        removeEdgesForBlock(**it);
    }

    _blocks.erase(toRemove, _blocks.end());

    // Establish new edges
    _edges.clear();
    const auto& edges = getProperty<gr::property_map>(data, "edges"s);
    for (const auto& [index, edgeData_] : edges) {
        const auto edgeData = std::get<gr::property_map>(edgeData_);

        UiGraphEdge edge(this);
        if (setEdgeData(edge, edgeData)) {
            _edges.emplace_back(std::move(edge));
        } else {
            components::Notification::error("Invalid edge ignored");
        }
    }
    _rearrangeBlocks = true;
}

void UiGraphModel::handleAvailableGraphBlockTypes(const gr::property_map& data) {
    const auto& knownBlockTypesList = getProperty<std::vector<std::string>>(data, "types"s);
    for (const auto& type : knownBlockTypesList) {
        auto splitterPosition = std::ranges::find(type, '<');

        if (splitterPosition != type.cend()) {
            knownBlockTypes[std::string(type.cbegin(), splitterPosition)].emplace(splitterPosition, type.cend());

        } else {
            knownBlockTypes[std::string(type)].emplace();
        }
    }
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
    updateFieldFrom(block.blockUniqueName, blockData, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    updateFieldFrom(block.blockName, blockData, "parameters"s, "name"s);
    updateFieldFrom(block.blockTypeName, blockData, "type_name"s);

    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(block)>, UiGraphBlock>) {
        updateFieldFrom(block.blockSettings, blockData, gr::serialization_fields::BLOCK_PARAMETERS);
        updateFieldFrom(block.blockMetaInformation, blockData, gr::serialization_fields::BLOCK_META_INFORMATION);

        block.blockSettings.erase(std::string(gr::serialization_fields::BLOCK_UNIQUE_NAME));
        block.updateBlockSettingsMetaInformation();

        updateFieldFrom(block.blockCategory, blockData, "block_category"s);
        updateFieldFrom(block.blockUiCategory, blockData, "ui_category"s);
        updateFieldFrom(block.blockIsBlocking, blockData, "is_blocking"s);

        auto processPorts = [&block, &blockData](auto& portsCollection, std::string_view portsField, gr::PortDirection direction) {
            portsCollection.clear();
            for (const auto& [portName, portData_] : getProperty<gr::property_map>(blockData, portsField)) {
                const auto& portData = std::get<gr::property_map>(portData_);
                auto&       port     = portsCollection.emplace_back(/*owner*/ std::addressof(block));
                port.portName        = portName;
                port.portType        = getProperty<std::string>(portData, "type"s);
                port.portDirection   = direction;
            }
        };

        processPorts(block.inputPorts, gr::serialization_fields::BLOCK_INPUT_PORTS, gr::PortDirection::INPUT);
        processPorts(block.outputPorts, gr::serialization_fields::BLOCK_OUTPUT_PORTS, gr::PortDirection::OUTPUT);

        block.getAllContexts();
        block.getActiveContext();

        if (auto parametersIt = blockData.find("parameters"); parametersIt != blockData.end()) {
            const auto* parameters = std::get_if<gr::property_map>(&(parametersIt->second));
            if (parameters) {
                if (auto uiConstraintsIt = parameters->find("ui_constraints"); uiConstraintsIt != parameters->end()) {
                    const auto* uiConstraints = std::get_if<gr::property_map>(&(uiConstraintsIt->second));
                    if (uiConstraints) {
                        auto x = getOptionalProperty<float, true>(*uiConstraints, "x");
                        auto y = getOptionalProperty<float, true>(*uiConstraints, "y");

                        if (x && y && (!block.storedXY.has_value() || (block.storedXY->x != *x || block.storedXY->y != *y))) {
                            block.storedXY = UiGraphBlock::StoredXY{
                                .x = *x,
                                .y = *y,
                            };
                            block.updatePosition = true;
                        }
                    }
                }
            }
        }

        _rearrangeBlocks = true;
    }

    // TODO: When adding support for nested graphs, need to process
    // getProperty<gr::property_map>(blockData, "children"s))
    // getProperty<gr::property_map>(blockData, "edges"s))
}

bool UiGraphModel::setEdgeData(auto& edge, const gr::property_map& edgeData) {
    updateFieldFrom(edge.edgeSourceBlockName, edgeData, gr::serialization_fields::EDGE_SOURCE_BLOCK);
    updateFieldFrom(edge.edgeDestinationBlockName, edgeData, gr::serialization_fields::EDGE_DESTINATION_BLOCK);

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
        return false;
    }

    updateFieldFrom(edge.edgeWeight, edgeData, gr::serialization_fields::EDGE_WEIGHT);
    updateFieldFrom(edge.edgeName, edgeData, gr::serialization_fields::EDGE_NAME);
    updateFieldFrom(edge.edgeType, edgeData, gr::serialization_fields::EDGE_TYPE);
    updateFieldFrom(edge.edgeMinBufferSize, edgeData, gr::serialization_fields::EDGE_MIN_BUFFER_SIZE);
    updateFieldFrom(edge.edgeBufferSize, edgeData, gr::serialization_fields::EDGE_BUFFER_SIZE);
    updateFieldFrom(edge.edgeState, edgeData, gr::serialization_fields::EDGE_EDGE_STATE);
    updateFieldFrom(edge.edgeNReaders, edgeData, gr::serialization_fields::EDGE_N_READERS);
    updateFieldFrom(edge.edgeNWriters, edgeData, gr::serialization_fields::EDGE_N_WRITERS);
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
