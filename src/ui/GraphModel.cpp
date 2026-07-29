#include "GraphModel.hpp"

#include "components/ImGuiNotify.hpp"

#include "scope_exit.hpp"

#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <cmath>
#include <memory>
#include <set>

#include "MapUtils.hpp"

using namespace std::string_literals;

UiGraphBlock::~UiGraphBlock() {
    if (ownerGraph) {
        ownerGraph->blockDestructionCount++;
        if (ownerGraph->selectedBlock == this) {
            ownerGraph->selectedBlock = nullptr;
        }
    }
}

auto UiGraphBlock::findBlockIteratorBy(std::initializer_list<SearchProperty> searchProperties, std::string_view value) {
    assert(std::get_if<GraphBlockInfo>(&blockCategoryInfo) && "This makes sense only for graphs");
    auto it = std::ranges::find_if(childBlocks, [&](const auto& block) {
        for (auto searchProperty : searchProperties) {
            if (searchProperty == SearchProperty::UniqueName) {
                if (block->blockUniqueName == value) {
                    return true;
                }
            } else if (searchProperty == SearchProperty::Name) {
                if (block->blockName == value) {
                    return true;
                }
            }
        }
        return false;
    });
    return std::make_pair(it, it != childBlocks.end());
}

std::optional<std::string> UiGraphPort::getExportedName(const UiGraphBlock* exportedTo) const {
    assert(!exportedTo || exportedTo->isGraph() || exportedTo->isScheduler());
    const auto& exportedPorts = [this, exportedTo]() -> std::set<UiGraphBlock::PortNameMapper> {
        if (!ownerBlock || !exportedTo) {
            return {};
        }
        const auto& map      = (portDirection == gr::PortDirection::INPUT) ? exportedTo->exportedInputPorts : exportedTo->exportedOutputPorts;
        auto        iterator = map.find(ownerBlock->blockUniqueName);
        if (iterator != std::end(map)) {
            return iterator->second;
        }
        return {};
    }();

    auto iterator = std::ranges::find_if(exportedPorts, [this](const auto& portmapper) { return portmapper.internalName == portName; });
    if (iterator != std::end(exportedPorts)) {
        return iterator->exportedName;
    }
    return {};
}

UiGraphBlock* UiGraphBlock::findBlockByUniqueName(const std::string& uniqueName) {
    auto [it, found] = findBlockIteratorBy({SearchProperty::UniqueName}, uniqueName);
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
    // Only graphs can have children emplaced. Schedulers have only one child
    // (the graph) which is never emplaced, it is there from the start
    auto* thisGraphInfo = std::get_if<GraphBlockInfo>(&blockCategoryInfo);
    assert(thisGraphInfo);

    const auto newBlockUniqueName = getProperty<std::string>(blockData, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    const auto [it, found]        = findBlockIteratorBy({SearchProperty::UniqueName}, newBlockUniqueName);
    if (found) {
        UiGraphBlock& block = *it->get();
        block.setBlockData(blockData);
    } else {
        const auto [newBlockOwnerSchedulerUniqueName, newBlockOwnerGraphUniqueName] = [&] -> std::pair<std::string, std::string> {
            return {// If `this` is a graph, then the owner scheduler for the
                    // new block is our owner scheduler [...]
                thisGraphInfo->ownerSchedulerUniqueName,
                // [...] and were the owning graph
                blockUniqueName};
        }();

        // Before we properly process the block, we need to set its category
        // and initialize its owner graph and scheduler
        childBlocks.push_back(ownerGraph->makeGraphBlock(this, blockData, newBlockOwnerSchedulerUniqueName, newBlockOwnerGraphUniqueName));
    }
}

bool UiGraphBlock::handleChildBlockRemoved(const std::string& uniqueName) {
    auto [blockIt, found] = findBlockIteratorBy({SearchProperty::UniqueName}, uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block to be removed {}", uniqueName);
        ownerGraph->requestFullUpdate();
        return false;
    }

    removeEdgesForBlock(*blockIt->get());

    childBlocks.erase(blockIt);

    return true;
}

void UiGraphBlock::handleChildEdgeEmplaced(const gr::property_map& data) {
    auto edge = parseEdgeData(data);
    if (edge) {
        childEdges.emplace_back(std::move(*edge));
    } else {
        // Failed to read edge data
        std::println("!requestFullUpdate reason: failed to read edge data {}", data);
        ownerGraph->requestFullUpdate();
    }
}

void UiGraphBlock::handleChildEdgeRemoved(const gr::property_map& /* data */) { ownerGraph->requestFullUpdate(); }

void UiGraphBlock::setSchedulerGraph(const gr::property_map& data) {
    assert(blockCategory == "ScheduledBlockGroup");
    const auto& children = getProperty<gr::property_map>(data, "children"s);
    assert(children.size() == 1 && "Schedulers contain only a single child -- the graph");

    for (const auto& [graphUniqueName, graphData_] : children) {
        const bool found     = !childBlocks.empty() && childBlocks[0]->blockUniqueName == std::string_view(graphUniqueName);
        const auto graphData = graphData_.get_if<gr::property_map>();
        if (!graphData) {
            continue;
        }
        if (found) {
            // Graph was not replaced
            childBlocks[0]->setBlockData(*graphData);
        } else {
            // We have a new graph
            childBlocks.clear();
            childBlocks.push_back(ownerGraph->makeGraphBlock(this, *graphData, blockUniqueName, {}));
        }
    }
}

void UiGraphBlock::setGraphChildren(const gr::property_map& data) {
    if (blockCategory == "NormalBlock") {
        return;
    }

    std::set<std::string> childrenUniqueNames;

    // Update or create blocks that GR knows
    if (data.contains("children"s)) {
        const auto& children = getProperty<gr::property_map>(data, "children"s);
        for (const auto& [childUniqueName, blockData_] : children) {
            childrenUniqueNames.insert(std::string(childUniqueName));
            const auto [blockIt, found] = findBlockIteratorBy({SearchProperty::UniqueName}, childUniqueName);
            const auto blockData        = blockData_.get_if<gr::property_map>();
            if (!blockData) {
                continue;
            }
            if (found) {
                (*blockIt)->setBlockData(*blockData);
            } else {
                handleChildBlockEmplaced(*blockData);
            }
        }
    } else {
        const auto& children = getProperty<gr::Tensor<gr::pmt::Value>>(data, "graph"s, "blocks"s);
        for (const auto& blockData : children) {
            const auto mapOpt = blockData.get_if<gr::property_map>();
            if (!mapOpt) {
                continue;
            }
            const auto& map        = *mapOpt;
            const auto& uniqueName = getProperty<std::string>(map, "unique_name"s);
            childrenUniqueNames.insert(uniqueName);
            const auto [blockIt, found] = findBlockIteratorBy({SearchProperty::UniqueName}, uniqueName);
            if (found) {
                (*blockIt)->setBlockData(map);
            } else {
                handleChildBlockEmplaced(map);
            }
        }
    }

    // Delete blocks that GR doesn't know about.
    // This is similar to erase-remove, but we need the list of blocks
    // we want to delete in order to disconnect them first.
    const auto toRemove = std::partition(childBlocks.begin(), childBlocks.end(), [&childrenUniqueNames](const auto& child) { //
        return childrenUniqueNames.contains(child->blockUniqueName);
    });
    for (auto it = toRemove; it != childBlocks.end(); ++it) {
        removeEdgesForBlock(**it);
    }

    childBlocks.erase(toRemove, childBlocks.end());

    // Establish new edges
    childEdges.clear();
    if (data.contains("edges"s)) {
        const auto& edges = getProperty<gr::property_map>(data, "edges"s);
        for (const auto& [index, edgeData_] : edges) {
            const auto edgeData = edgeData_.get_if<gr::property_map>();
            if (!edgeData) {
                continue;
            }

            auto edge = parseEdgeData(*edgeData);
            if (edge) {
                childEdges.emplace_back(std::move(*edge));
            } else {
                components::Notification::error("Invalid edge ignored");
            }
        }
    } else {
        const auto& edges = getProperty<gr::Tensor<gr::pmt::Value>>(data, "graph"s, "connections"s);
        for (const auto& edgeData : edges) {
            const auto edgeProperties = edgeData.value_or(gr::Tensor<gr::pmt::Value>{});
            if (edgeProperties.size() < 4) {
                components::Notification::error("Invalid connection ignored");
                continue;
            }

            // edge format: [sourceBlockName, sourcePort, destinationBlockName, destinationPort, ?minBufferSize]
            gr::property_map map{
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), edgeProperties[0]},      //
                {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), edgeProperties[1]},       //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), edgeProperties[2]}, //
                {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), edgeProperties[3]}   //
            };
            if (edgeProperties.size() > 4) {
                map[std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE)] = edgeProperties[4];
            }

            auto edge = parseEdgeData(map);
            if (edge) {
                childEdges.emplace_back(std::move(*edge));
            } else {
                components::Notification::error("Invalid edge ignored");
            }
        }
    }
}

void UiGraphBlock::setSetting(std::string_view keyToUpdate, gr::pmt::Value&& updatedValue) {
    if (!ownerGraph) {
        return;
    }

    const auto setSettingImpl = [](UiGraphBlock& block, std::string_view keyToUpdateImpl, gr::pmt::Value&& updatedValueImpl) {
        gr::Message message;
        message.serviceName = block.blockUniqueName;
        message.endpoint    = gr::block::property::kSetting;
        message.cmd         = gr::message::Command::Set;
        message.data        = gr::property_map{{std::pmr::string(keyToUpdateImpl), std::move(updatedValueImpl)}};
        block.ownerGraph->sendMessage(std::move(message));
    };

    // set matching exported properties
    auto exportedPropertyIter = exportedProperties.find(keyToUpdate);
    if (exportedPropertyIter != std::end(exportedProperties) && exportedPropertyIter->second.windowId.has_value()) {
        for (const auto& result : ownerGraph->recursiveGatherMatchingExportedProperties(*exportedPropertyIter->second.windowId, this)) {
            if (result.block == this) {
                break;
            }
            setSettingImpl(*result.block, result.propertyName, gr::pmt::Value{updatedValue});
        }
    }

    setSettingImpl(*this, keyToUpdate, std::move(updatedValue));
}

void UiGraphBlock::setBlockData(const gr::property_map& data) {
    newGraphDataBeingSet           = true;
    Digitizer::utils::scope_exit _ = [&] { newGraphDataBeingSet = false; };

    setBasicBlockData(data);

    if (isGraph()) {
        setGraphChildren(data);
    } else if (isScheduler()) {
        // When we get new data for a scheduler, we can only set the
        // basic data and send the inspection message for the scheduler
        // to send us its data.
        gr::Message message;
        message.cmd         = gr::message::Command::Get;
        message.endpoint    = gr::scheduler::property::kSchedulerInspect;
        message.serviceName = blockUniqueName;
        message.data        = gr::property_map{};
        ownerGraph->sendMessage(std::move(message));
    }

    // graphs and schedulers derive their ports from their children, so this has to
    // run once the children are known
    if (updatePorts(data) && parentBlock && !parentBlock->newGraphDataBeingSet) {
        // this update did not come through the parent, so the parent's edges may
        // point into the port collections we have just rebuilt
        parentBlock->graphResolveEdgePortPointersAndRemoveIfInvalid();
    }
}

void UiGraphBlock::setBasicBlockData(const gr::property_map& blockData) {
    if (blockUniqueName.empty()) {
        updateFieldFrom(blockUniqueName, blockData, blockUniqueName, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    }

    updateFieldFrom(blockName, blockData, blockName, "name"s);
    if (blockName.empty()) {
        updateFieldFrom(blockName, blockData, {}, "parameters"s, "name"s);
    }

    std::optional<gr::property_map> parameters;
    const auto                      tryGetParameters = [&parameters](const gr::property_map& map) {
        auto parametersIterator = map.find(gr::serialization_fields::BLOCK_PARAMETERS);
        if (parametersIterator != std::end(map)) {
            parameters = parametersIterator->second.get_if<gr::property_map>();
        }
    };

    // handle the case where parameters are in the "scheduler" key and not at the root of the block description
    const auto scheduler = getOptionalProperty<gr::property_map>(blockData, "scheduler");
    if (scheduler) {
        tryGetParameters(*scheduler);
    }
    if (!parameters) {
        tryGetParameters(blockData);
    }
    if (parameters) {
        blockSettings = *parameters;
    }

    updateFieldFrom(blockTypeName, blockData, blockTypeName, "type_name"s);
    if (blockTypeName.empty() && scheduler) {
        updateFieldFrom(blockTypeName, *scheduler, {}, gr::serialization_fields::BLOCK_ID);
    }
    if (blockTypeName.empty()) {
        updateFieldFrom(blockTypeName, blockData, {}, gr::serialization_fields::BLOCK_ID);
    }

    // Meta information needs special handling as it contains the
    // information about the exported ports
    if (auto metaInformation = getOptionalProperty<gr::property_map>(blockData, gr::serialization_fields::BLOCK_META_INFORMATION); metaInformation.has_value()) {

        blockMetaInformation   = *metaInformation;
        auto readExportedPorts = [this](auto& destination, const std::string& key) {
            auto it = blockMetaInformation.find(key);
            if (it == blockMetaInformation.end()) {
                return;
            }

            destination.clear();
            auto map = it->second.get_if<gr::property_map>();
            if (!map) {
                return;
            }

            for (const auto& [subBlockName, portMappings_] : *map) {
                const auto portMappings = portMappings_.template get_if<gr::property_map>();
                if (!portMappings) {
                    continue;
                }
                for (const auto& [portName, portMapping_] : *portMappings) {
                    const auto portMapping = portMapping_.template get_if<gr::property_map>();
                    if (!portMapping) {
                        continue;
                    }
                    if (auto portIt = portMapping->find("exportedName"); portIt != portMapping->end()) {
                        destination[std::string(subBlockName)].insert(PortNameMapper{std::string(portName), portIt->second.value_or(std::string())});
                    }
                }
            }
        };

        readExportedPorts(exportedInputPorts, "exportedInputPorts"s);
        readExportedPorts(exportedOutputPorts, "exportedOutputPorts"s);
    }

    blockSettings.erase(std::pmr::string(gr::serialization_fields::BLOCK_UNIQUE_NAME));
    updateBlockSettingsMetaInformation();

    updateFieldFrom(blockCategory, blockData, blockCategory, "block_category"s);
    updateFieldFrom(blockUiCategory, blockData, blockUiCategory, "ui_category"s);
    updateFieldFrom(blockIsBlocking, blockData, blockIsBlocking, "is_blocking"s);

    if (!storedXY) {
        const auto x = getOptionalProperty<float, true>(blockSettings, "ui_constraints", "x");
        const auto y = getOptionalProperty<float, true>(blockSettings, "ui_constraints", "y");
        if (x && y && std::isfinite(*x) && std::isfinite(*y)) {
            storedXY = StoredXY{*x, *y};
        }
    }
}

bool UiGraphBlock::updatePorts(const gr::property_map& blockData) {
    auto processPorts = [&blockData, this](auto& portsCollection, std::string_view portsField, gr::PortDirection direction) {
        portsCollection.clear();

        const auto appendDeclaredPorts = [&] {
            const auto declaredPorts = getProperty<gr::property_map>(blockData, portsField);
            for (const auto& [portName, portData_] : declaredPorts) {
                const auto portData = portData_.get_if<gr::property_map>();
                if (!portData) {
                    continue;
                }
                auto& port         = portsCollection.emplace_back(/*owner*/ this);
                port.portName      = portName;
                port.portType      = getProperty<std::string>(*portData, "type"s);
                port.portDirection = direction;
            }
        };

        const auto appendExportedPorts = [&](UiGraphBlock& graphBlock) {
            auto& exportedPorts = direction == gr::PortDirection::INPUT ? exportedInputPorts : exportedOutputPorts;
            for (const auto& [childBlockName, portDefinitions] : exportedPorts) {
                auto* child = graphBlock.findBlockByUniqueName(childBlockName);
                if (child == nullptr) {
                    continue;
                }
                for (const auto& portDefinition : portDefinitions) {
                    auto portFound = child->findPortIteratorByName(direction == gr::PortDirection::INPUT ? child->inputPorts() : child->outputPorts(), portDefinition.internalName);
                    if (!portFound.second) {
                        continue;
                    }

                    auto& port         = portsCollection.emplace_back(/*owner*/ this);
                    port.portName      = portDefinition.exportedName;
                    port.portType      = portFound.first->portType;
                    port.portDirection = direction;
                }
            }
        };

        if (isScheduler()) {
            assert(childBlocks.size() <= 1);
            if (!childBlocks.empty()) {
                appendExportedPorts(*childBlocks.front());
            }
        } else if (isGraph()) {
            appendExportedPorts(*this);
        } else {
            appendDeclaredPorts();
        }
    };

    processPorts(_inputPorts, gr::serialization_fields::BLOCK_INPUT_PORTS, gr::PortDirection::INPUT);
    processPorts(_outputPorts, gr::serialization_fields::BLOCK_OUTPUT_PORTS, gr::PortDirection::OUTPUT);

    return isGraph() || isScheduler();
}

std::optional<UiGraphEdge> UiGraphBlock::parseEdgeData(const gr::property_map& edgeData) {
    UiGraphEdge edge;
    // this may be unique name (edge message, gr::serializeEdge) or regular name (from saveGraphToMap)
    updateFieldFrom(edge.edgeSourceBlockUniqueName, edgeData, {}, gr::serialization_fields::EDGE_SOURCE_BLOCK);
    updateFieldFrom(edge.edgeDestinationBlockUniqueName, edgeData, {}, gr::serialization_fields::EDGE_DESTINATION_BLOCK);

    auto portDefinitionFor = [&edgeData](std::string key) -> gr::PortDefinition {
        const auto portField = edgeData.find_value(key).value_or(gr::pmt::Value{});

        if (portField.is_string()) {
            return gr::PortDefinition(portField.value_or(std::string()));
        }
        if (const auto indices = portField.value_or(gr::Tensor<gr::pmt::Value>{}); indices.size() == 2) {
            // this is a collection in format [topLevel, subIndex]. it is serialized in this form by saveGraphToMap
            return gr::PortDefinition(static_cast<std::size_t>(indices[0].value_or(std::int64_t{})), static_cast<std::size_t>(indices[1].value_or(std::int64_t{})));
        }
        if (const auto index = portField.get_if<std::int64_t>()) { // regular port definition, by index
            return gr::PortDefinition(static_cast<std::size_t>(*index));
        }

        // gr::serializeEdge write port collections in this format
        return gr::PortDefinition(getProperty<std::size_t>(edgeData, key + ".top_level"), getProperty<std::size_t>(edgeData, key + ".sub_index"));
    };

    edge.edgeSourcePortDefinition      = portDefinitionFor(std::string(gr::serialization_fields::EDGE_SOURCE_PORT));
    edge.edgeDestinationPortDefinition = portDefinitionFor(std::string(gr::serialization_fields::EDGE_DESTINATION_PORT));

    edge.edgeSourcePort      = resolveChildPort(edge.edgeSourceBlockUniqueName, gr::PortDirection::OUTPUT, edge.edgeSourcePortDefinition);
    edge.edgeDestinationPort = resolveChildPort(edge.edgeDestinationBlockUniqueName, gr::PortDirection::INPUT, edge.edgeDestinationPortDefinition);

    if (!edge.edgeSourcePort || !edge.edgeDestinationPort) {
        std::println("Warning: Edge definition invalid! source {} ({} {}) destination {} ({} {})", //
            !!edge.edgeSourcePort, edge.edgeSourceBlockUniqueName, edge.edgeSourcePortDefinition,  //
            !!edge.edgeDestinationPort, edge.edgeDestinationBlockUniqueName, edge.edgeDestinationPortDefinition);
        return {};
    }

    // if this is a graph description saved with saveGraphToMap, then it used regular names, because that function is for persisting the graph
    // if this is an edge message that only exists at runtime, then it used unique name. To make things easier elsewhere we always use unique
    // name in the UI and not what was written in the message.
    edge.edgeSourceBlockUniqueName      = edge.edgeSourcePort->ownerBlock->blockUniqueName;
    edge.edgeDestinationBlockUniqueName = edge.edgeDestinationPort->ownerBlock->blockUniqueName;

    updateFieldFrom(edge.edgeWeight, edgeData, {}, gr::serialization_fields::EDGE_WEIGHT);
    updateFieldFrom(edge.edgeName, edgeData, {}, gr::serialization_fields::EDGE_NAME);
    updateFieldFrom(edge.edgeType, edgeData, {}, gr::serialization_fields::EDGE_TYPE);
    updateFieldFrom(edge.edgeMinBufferSize, edgeData, {}, gr::serialization_fields::EDGE_MIN_BUFFER_SIZE);
    updateFieldFrom(edge.edgeBufferSize, edgeData, {}, gr::serialization_fields::EDGE_BUFFER_SIZE);
    updateFieldFrom(edge.edgeState, edgeData, {}, gr::serialization_fields::EDGE_EDGE_STATE);
    updateFieldFrom(edge.edgeNReaders, edgeData, {}, gr::serialization_fields::EDGE_N_READERS);
    updateFieldFrom(edge.edgeNWriters, edgeData, {}, gr::serialization_fields::EDGE_N_WRITERS);
    return edge;
}

UiGraphPort* UiGraphBlock::resolveChildPort(const std::string& childNameOrUniqueName, gr::PortDirection direction, const gr::PortDefinition& portDefinition) {
    auto [it, found] = findBlockIteratorBy({SearchProperty::UniqueName, SearchProperty::Name}, childNameOrUniqueName);
    if (!found) {
        return nullptr;
    }

    auto& ports = direction == gr::PortDirection::INPUT ? (*it)->_inputPorts : (*it)->_outputPorts;

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
        portDefinition.definition);
}

void UiGraphBlock::graphResolveEdgePortPointersAndRemoveIfInvalid() {
    std::erase_if(childEdges, [this](UiGraphEdge& edge) {
        edge.edgeSourcePort      = resolveChildPort(edge.edgeSourceBlockUniqueName, gr::PortDirection::OUTPUT, edge.edgeSourcePortDefinition);
        edge.edgeDestinationPort = resolveChildPort(edge.edgeDestinationBlockUniqueName, gr::PortDirection::INPUT, edge.edgeDestinationPortDefinition);
        return edge.edgeSourcePort == nullptr || edge.edgeDestinationPort == nullptr;
    });
}

void UiGraphBlock::removeEdgesForBlock(UiGraphBlock& block) {
    std::erase_if(childEdges, [blockPtr = std::addressof(block)](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    });

    UiGraphBlock* exportOwner      = (parentBlock && parentBlock->isScheduler()) ? parentBlock : this;
    const bool    hadInputExports  = exportOwner->exportedInputPorts.erase(block.blockUniqueName) > 0;
    const bool    hadOutputExports = exportOwner->exportedOutputPorts.erase(block.blockUniqueName) > 0;
    if (hadInputExports || hadOutputExports) {
        exportOwner->requestBlockUpdate();
    }
}

bool UiGraphBlock::isConnected() const {
    assert(parentBlock);
    return std::ranges::find_if(parentBlock->childEdges, [blockPtr = this](const auto& edge) {
        return edge.edgeSourcePort->ownerBlock == blockPtr || //
               edge.edgeDestinationPort->ownerBlock == blockPtr;
    }) != parentBlock->childEdges.cend();
}

void UiGraphBlock::getAllContexts() { ownerGraph->sendMessage(gr::Message{.cmd = gr::Message::Get, .serviceName = blockUniqueName, .clientRequestID = "all", .endpoint = gr::block::property::kSettingsContexts, .data = {}}); }

void UiGraphBlock::setActiveContext(const ContextTime& contextTime) {
    const auto& [context, time] = contextTime;
    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Set,
        .serviceName     = blockUniqueName,
        .clientRequestID = "activate",
        .endpoint        = gr::block::property::kActiveContext,
        .data            = gr::property_map{{"gr:context", context}, {"gr:ctx_time", time}},
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
        .data            = gr::property_map{{"gr:context", context}, {"gr:ctx_time", time}},
    });
}

void UiGraphBlock::removeContext(const ContextTime& contextTime) {
    const auto& [context, time] = contextTime;
    ownerGraph->sendMessage(gr::Message{
        .cmd             = gr::Message::Disconnect,
        .serviceName     = blockUniqueName,
        .clientRequestID = "rm",
        .endpoint        = gr::block::property::kSettingsCtx,
        .data            = gr::property_map{{"gr:context", context}, {"gr:ctx_time", time}},
    });
}

void UiGraphBlock::requestBlockUpdate() {
    const auto& targetGraph = ownerSchedulerUniqueName();

    // If we are updating a nested scheduler, we need to send the
    // introspection message to its parent
    if (blockUniqueName == ownerSchedulerUniqueName()) {
        if (parentBlock) {
            // This is a nested scheduler, so it is a scheduler *and* a block
            {
                gr::Message message;
                message.cmd         = gr::message::Command::Get;
                message.endpoint    = gr::scheduler::property::kSchedulerInspect;
                message.serviceName = parentBlock->blockUniqueName;
                ownerGraph->sendMessage(std::move(message));
            }
            {
                gr::Message message;
                message.cmd         = gr::message::Command::Get;
                message.endpoint    = gr::graph::property::kInspectBlock;
                message.serviceName = parentBlock->blockUniqueName;
                message.data        = gr::property_map{{"uniqueName", blockUniqueName}};
                ownerGraph->sendMessage(std::move(message));
            }
        }
    } else {
        gr::Message message;
        message.cmd         = gr::message::Command::Get;
        message.endpoint    = gr::graph::property::kInspectBlock;
        message.serviceName = targetGraph;
        message.data        = gr::property_map{{"uniqueName", blockUniqueName}};
        ownerGraph->sendMessage(std::move(message));
    }
}

void UiGraphBlock::updateBlockSettingsMetaInformation() {
    const auto findMeta = [this](std::string_view key, std::string_view attr) -> std::optional<gr::pmt::Value> {
        const auto it = blockMetaInformation.find(std::format("{}::{}", key, attr));
        if (it == blockMetaInformation.end()) {
            return std::nullopt;
        }
        return gr::pmt::Value(it->second);
    };

    blockSettingsMetaInformation.clear();

    for (const auto& [settingKey, _] : blockSettings) {
        std::string description{settingKey};
        std::string unit;
        bool        isVisible = false;

        if (const auto v = findMeta(settingKey, "description"); v && v->is_string()) {
            description = v->value_or(std::string{});
        }
        if (const auto v = findMeta(settingKey, "unit"); v && v->is_string()) {
            unit = v->value_or(std::string{});
        }
        if (const auto v = findMeta(settingKey, "visible")) {
            if (const auto* b = v->get_if<bool>()) {
                isVisible = *b;
            }
        }

        const auto extractDouble = [&](std::string_view attr) -> std::optional<double> {
            if (const auto v = findMeta(settingKey, attr)) {
                if (const auto* d = v->get_if<double>()) {
                    return *d;
                }
                if (const auto* f = v->get_if<float>()) {
                    return static_cast<double>(*f);
                }
                if (const auto* i = v->get_if<int32_t>()) {
                    return static_cast<double>(*i);
                }
            }
            return std::nullopt;
        };
        std::optional<double> minVal = extractDouble("min_value");
        std::optional<double> maxVal = extractDouble("max_value");

        std::vector<std::string> enumValues;
        if (const auto v = findMeta(settingKey, "enum_values")) {
            if (const auto tensor = v->get_if<gr::TensorView<gr::pmt::Value>>()) {
                for (const auto& elem : *tensor) {
                    if (auto sv = elem.value_or(std::string_view{}); sv.data() != nullptr) {
                        enumValues.emplace_back(sv);
                    }
                }
            }
        }

        blockSettingsMetaInformation.insert_or_assign(std::string(settingKey), SettingsMetaInformation{.unit = std::move(unit), .description = std::move(description), .isVisible = isVisible, .minValue = minVal, .maxValue = maxVal, .enumValues = std::move(enumValues)});
    }

    std::erase_if(exportedProperties, [this](const auto& item) { return !blockSettings.contains(item.first); });
}

UiGraphModel::FindBlockResult UiGraphModel::recursiveFindBlockByUniqueName(std::string_view uniqueName) {
    FindBlockResult out{};
    recursiveForEachBlock([&out, uniqueName](const FindBlockResult& element) {
        assert(element.block);
        if (element.block->blockUniqueName == uniqueName) {
            out = element;
            return VisitorResult::Break;
        }
        return VisitorResult::Recurse;
    });
    return out;
}

UiGraphModel::FindBlockResult UiGraphModel::recursiveFindBlockByName(std::string_view name) {
    FindBlockResult out{};
    recursiveForEachBlock([&out, name](const FindBlockResult& element) {
        assert(element.block);
        if (element.block->blockName == name) {
            out = element;
            return VisitorResult::Break;
        }
        return VisitorResult::Recurse;
    });
    return out;
}

UiGraphModel::ExportedPropertiesView UiGraphModel::recursiveGatherExportedProperties() {
    ExportedPropertiesView output;
    recursiveForEachBlock([&output](const FindBlockResult& element) {
        if (!element.block->exportedProperties.empty()) {
            output.try_emplace(element.block->blockName, std::addressof(element.block->exportedProperties));
        }
        return VisitorResult::Recurse;
    });
    return output;
}

std::vector<UiGraphModel::ExportedPropertyMatchResult> UiGraphModel::recursiveGatherMatchingExportedProperties(std::size_t id, UiGraphBlock* exclude) {
    std::vector<ExportedPropertyMatchResult> output;
    recursiveForEachBlock([&output, id, exclude](const FindBlockResult& element) {
        if (element.block != exclude) {
            for (const auto& [propertyName, exportedInfo] : element.block->exportedProperties) {
                if (exportedInfo.windowId == id) {
                    output.emplace_back(element.block, propertyName);
                    break;
                }
            }
        }
        return VisitorResult::Recurse;
    });
    return output;
}

std::vector<UiGraphBlock*> UiGraphModel::recursiveGatherPlotSinks() {
    std::vector<UiGraphBlock*> output;
    recursiveForEachBlock([&output](const FindBlockResult& element) {
        if (element.block->isPlotSink()) {
            output.push_back(element.block);
        }
        return VisitorResult::Recurse;
    });
    return output;
}

void UiGraphModel::recursiveForEachBlock(const std::function<VisitorResult(const FindBlockResult&)>& callback) {
    if (callback({.block = std::addressof(rootBlock)}) != VisitorResult::Recurse) {
        return;
    }

    std::deque<UiGraphBlock*> toProcess{std::addressof(rootBlock)};
    while (!toProcess.empty()) {
        auto* currentGraph = toProcess.front();
        toProcess.pop_front();

        auto& childBlocks = currentGraph->childBlocks;
        for (auto blockIt = childBlocks.begin(); blockIt != childBlocks.end(); ++blockIt) {
            auto&               block  = *blockIt;
            const VisitorResult result = callback(FindBlockResult{
                .parentGraph        = currentGraph,
                .block              = block.get(),
                .owningCollection   = std::addressof(childBlocks),
                .owningCollectionIt = blockIt,
            });
            using enum VisitorResult;
            switch (result) {
            case Break: return;
            case Continue: continue;
            case Recurse: break;
            }
            if (!block->childBlocks.empty()) {
                toProcess.push_back(block.get());
            }
        }
    }
}

void updateKnownTypeMap(auto& map, const auto& data) {
    map.clear();
    const auto& knownList = getProperty<gr::Tensor<gr::pmt::Value>>(data, "types"s);
    for (const auto& typeValue : knownList) {
        if (!typeValue.is_string()) {
            continue;
        }
        const auto type             = typeValue.value_or(std::string());
        auto       splitterPosition = std::ranges::find(type, '<');

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

void UiGraphModel::saveBlockPositions(gr::property_map& graphData) {
    const auto blocksView = graphData.get_if<gr::TensorView<gr::pmt::Value>>("blocks");
    if (!blocksView) {
        return;
    }
    auto blocks = blocksView->owned();
    for (auto& blockValue : blocks) {
        if (!blockValue.is_map()) {
            continue;
        }
        auto       block    = blockValue.value_or(gr::property_map{});
        const auto found    = recursiveFindBlockByUniqueName(block.value_or<std::string>("unique_name", {}));
        const auto position = found ? found.block->storedXY : std::nullopt;
        if (position && std::isfinite(position->x) && std::isfinite(position->y)) {
            const auto scheduler         = block.get_if<gr::ValueMapView>("scheduler");
            auto       settings          = scheduler ? scheduler->owned() : block;
            auto       parameters        = settings.value_or<gr::property_map>("parameters", {});
            auto       constraints       = parameters.value_or<gr::property_map>("ui_constraints", {});
            constraints["x"]             = position->x;
            constraints["y"]             = position->y;
            parameters["ui_constraints"] = std::move(constraints);
            settings["parameters"]       = std::move(parameters);
            if (scheduler) {
                block["scheduler"] = std::move(settings);
            } else {
                block = std::move(settings);
            }
        }
        if (const auto nestedGraphView = block.get_if<gr::ValueMapView>("graph")) {
            auto nestedGraph = nestedGraphView->owned();
            saveBlockPositions(nestedGraph);
            block["graph"] = std::move(nestedGraph);
        }
        blockValue = std::move(block);
    }
    graphData["blocks"] = std::move(blocks);
}

bool UiGraphModel::processMessage(const gr::Message& message) {
    namespace graph     = gr::graph::property;
    namespace scheduler = gr::scheduler::property;
    namespace block     = gr::block::property;

    for (const auto& [_, subscription] : _testResponseSubscriptions) {
        subscription(message);
    }

    if (!message.data) {
        std::println("Received an error: {}", message.data.error().message);
        DigitizerUi::components::Notification::error(std::format("Received an error: {}\n", message.data.error().message));
        return false;
    }

    {
        static const auto debugReceivedMessages = [] -> std::optional<std::set<std::string, std::less<>>> {
            const char* env = ::getenv("OPENDIGITIZER_DEBUG_RECEIVED_MESSAGES");
            if (!env) {
                return std::nullopt;
            }

            std::set<std::string, std::less<>> endpoints;
            for (auto&& part : std::string_view{env} | std::views::split(',')) {
                std::string token(part.begin(), part.end());
                std::erase_if(token, [](char ch) { return ch == ' ' || ch == '\t'; });
                if (!token.empty()) {
                    endpoints.emplace(std::move(token));
                }
            }

            return endpoints;
        }();
        if (debugReceivedMessages && (debugReceivedMessages->empty() || debugReceivedMessages->contains(message.endpoint))) {
            std::println("UiGraphModel::processMessage called with message = {} {}", message.serviceName, message.endpoint);
            pretty_print_map(*message.data, -1UZ, 1);
        }
    }

    const auto& data = *message.data;

    auto uniqueName = [&data](const std::string_view& key = gr::serialization_fields::BLOCK_UNIQUE_NAME) {
        auto it = data.find(key);
        if (it == data.end()) {
            return std::string();
        }

        return it->second.value_or(std::string());
    };

    // We can not really process messages until we get the initial graph contents
    if (rootBlock.blockUniqueName.empty()) {
        if (message.endpoint == scheduler::kSchedulerInspected) {
            assert(getProperty<std::string>(data, "block_category") == "ScheduledBlockGroup");

            // The SchedulerInspected message is special. If we do not know about
            // anything WRT the GR flowgraph, we need to deduce that the root scheduler
            // in the kSchedulerInspected message is the top level scheduler
            rootBlock.blockCategoryInfo = UiGraphBlock::SchedulerBlockInfo{};
            rootBlock.blockUniqueName   = message.serviceName;
            const auto& children        = getProperty<gr::property_map>(data, "children");
            assert(children.size() == 1);

        } else {
            // We can not process any messages until we get the Graph contents
            requestFullUpdate();
            return false;
        }
    }

    auto targetGraphIt         = data.find("_targetGraph"s);
    auto targetBlockUniqueName = targetGraphIt == data.end() ? message.serviceName : targetGraphIt->second.value_or(std::string());

    auto targetBlock = recursiveFindBlockByUniqueName(targetBlockUniqueName);

    if (!targetBlock) {
        components::Notification::error(std::format("Got a message for an unknown block {} {}", message.serviceName, message.endpoint));
        std::println("!requestFullUpdate reason: Got a message for an unknown block {} {}", message.serviceName, message.endpoint);
        requestFullUpdate();
        return false;
    }

    if (message.endpoint == scheduler::kBlockEmplaced) {
        targetBlock.block->handleChildBlockEmplaced(data);

    } else if (message.endpoint == scheduler::kBlockRemoved) {
        targetBlock.block->handleChildBlockRemoved(uniqueName("uniqueName"));

    } else if (message.endpoint == scheduler::kBlockReplaced) {
        const auto  replacedName  = uniqueName("replacedBlockUniqueName");
        const auto* replacedBlock = targetBlock.block->findBlockByUniqueName(replacedName);
        const auto  position      = replacedBlock ? replacedBlock->storedXY : std::nullopt;
        targetBlock.block->handleChildBlockRemoved(replacedName);
        targetBlock.block->handleChildBlockEmplaced(data);
        if (position) {
            if (auto* replacement = targetBlock.block->findBlockByUniqueName(uniqueName())) {
                replacement->storedXY = position;
            }
        }

    } else if (message.endpoint == graph::kBlockInspected) {
        handleBlockDataUpdated(uniqueName(), data);

    } else if (message.endpoint == block::kSetting) {
        // serviceName is used for block's unique name in settings messages
        handleBlockSettingsChanged(message.serviceName, data);

    } else if (message.endpoint == block::kStagedSetting) {
        // serviceName is used for block's unique name in settings messages
        handleBlockSettingsStaged(message.serviceName, data);

    } else if (message.endpoint == scheduler::kEdgeEmplaced) {
        targetBlock.block->handleChildEdgeEmplaced(data);

    } else if (message.endpoint == scheduler::kEdgeRemoved) {
        targetBlock.block->handleChildEdgeRemoved(data);

    } else if (message.endpoint == scheduler::kSchedulerInspected) {
        // setBlockData, but force setting children data
        targetBlock.block->setBasicBlockData(data);
        targetBlock.block->setSchedulerGraph(data);
        if (targetBlock.block->updatePorts(data) && targetBlock.parentGraph) {
            // the scheduler's ports were just rebuilt, so the parent's edges may
            // point into the port collections we have replaced
            targetBlock.parentGraph->graphResolveEdgePortPointersAndRemoveIfInvalid();
        }
        targetBlock.block->blockCategoryInfo = UiGraphBlock::SchedulerBlockInfo{.childrenLoaded = true};
        requestedFullUpdate                  = false;

    } else if (message.endpoint == graph::kGraphInspected) {
        targetBlock.block->setBlockData(data);

    } else if (message.endpoint == graph::kRegistryBlockTypes) {
        handleAvailableGraphBlockTypes(data);

    } else if (message.endpoint == graph::kRegistrySchedulerTypes) {
        handleAvailableGraphSchedulerTypes(data);

    } else if (message.endpoint == "LifecycleState") {
        // Nothing to do for lifecycle state changes
        auto valueIt = data.find("state");
        if (valueIt != data.end() && valueIt->second.value_or(std::string()) == "RUNNING") {
            std::println("Lifecycle state changed to: RUNNING, requesting update");
            requestFullUpdate();
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
            auto graphData = gr::pmt::yaml::deserialize(valueIt->second.value_or(std::string{}));
            if (!graphData) {
                components::Notification::error(std::format("Could not parse flowgraph YAML: {}", graphData.error().message));
                return false;
            }
            saveBlockPositions(*graphData);
            m_localFlowgraphGrc = gr::pmt::yaml::serialize(*graphData);
        } else {
            assert(false);
        }
    } else if (message.endpoint == graph::kSubgraphExportedPort) {
        // read exported ports out of meta information, easier to treat this as
        // source of truth than update our own cache based on these export events
        targetBlock.block->requestBlockUpdate();

    } else if (message.endpoint == scheduler::kBlocksGrouped || message.endpoint == scheduler::kBlocksUngrouped) {
        requestFullUpdate();

    } else {
        if (!message.data) {
            DigitizerUi::components::Notification::error(std::format("Not processed: {} data: {}\n", message.endpoint, message.data.error().message));
        }
        return false;
    }

    return true;
}

void UiGraphModel::requestFullUpdate(std::source_location location) {
    if (rootBlock.newGraphDataBeingSet || requestedFullUpdate) {
        return;
    }

    std::println("!requestFullUpdate: sending message, invoked by {}:{}", location.file_name(), location.line());

    requestedFullUpdate = true;
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

void UiGraphModel::handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData) {
    auto found = recursiveFindBlockByUniqueName(uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block to be updated {}", uniqueName);
        requestFullUpdate();
        return;
    }

    found.block->setBlockData(blockData);
}

void UiGraphModel::handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data) {
    auto found = recursiveFindBlockByUniqueName(uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block to be changed settings {}", uniqueName);
        requestFullUpdate();
        return;
    }

    auto* block = found.block;
    for (const auto& [key, value] : data) {
        if (std::string_view(key) != gr::serialization_fields::BLOCK_UNIQUE_NAME) {
            block->blockSettings.insert_or_assign(key, value);
            block->updateBlockSettingsMetaInformation();
        }
    }
}

void UiGraphModel::handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data) { handleBlockSettingsChanged(uniqueName, data); }

void UiGraphModel::handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data) {
    auto found = recursiveFindBlockByUniqueName(uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block's context change {}", uniqueName);
        requestFullUpdate();
        return;
    }

    const auto ctx  = data.find_value("gr:context").value_or(gr::pmt::Value{}).value_or(std::string());
    auto       time = data.find_value("gr:ctx_time").value_or(gr::pmt::Value{}).value_or(std::uint64_t{0});

    found.block->activeContext = UiGraphBlock::ContextTime{
        .context = ctx,
        .time    = time,
    };
}

void UiGraphModel::handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data) {
    auto found = recursiveFindBlockByUniqueName(uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block's known contexts change {}", uniqueName);
        requestFullUpdate();
        return;
    }

    const gr::pmt::Value contextsPmt = data.find_value("contexts").value_or(gr::pmt::Value{});
    const gr::pmt::Value timesPmt    = data.find_value("times").value_or(gr::pmt::Value{});
    auto                 contexts    = contextsPmt.value_or(gr::Tensor<gr::pmt::Value>{});
    auto                 times       = timesPmt.value_or(gr::Tensor<gr::pmt::Value>{});

    std::vector<UiGraphBlock::ContextTime> contextAndTimes;
    const std::size_t                      n = std::min(contexts.size(), times.size());
    for (std::size_t i = 0UZ; i < n; ++i) {
        const auto context = contexts[i].value_or(std::string());
        const auto ts      = times[i].value_or(std::uint64_t{0});
        contextAndTimes.emplace_back(UiGraphBlock::ContextTime{
            .context = context,
            .time    = ts,
        });
    }
    found.block->contexts = contextAndTimes;
}

void UiGraphModel::handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& /* data */) {
    auto found = recursiveFindBlockByUniqueName(uniqueName);
    if (!found) {
        std::println("!requestFullUpdate reason: requested an unknown block's add/remove context {}", uniqueName);
        requestFullUpdate();
        return;
    }

    found.block->getAllContexts();
    found.block->getActiveContext();
}

std::unique_ptr<UiGraphBlock> UiGraphModel::makeGraphBlock(UiGraphBlock* parent, const gr::property_map& blockData, const std::string& ownerSchedulerUniqueName, const std::string& ownerGraphUniqueName) {
    // Before we properly process the block, we need to set its category
    // and initialize its owner graph and scheduler
    auto newBlock = std::make_unique<UiGraphBlock>(/*owner*/ this, /*parentBlock*/ parent);
    updateFieldFrom(newBlock->blockUniqueName, blockData, {}, gr::serialization_fields::BLOCK_UNIQUE_NAME);
    updateFieldFrom(newBlock->blockCategory, blockData, {}, gr::serialization_fields::BLOCK_CATEGORY);

    if (newBlock->blockCategory == "TransparentBlockGroup") {
        newBlock->blockCategoryInfo = UiGraphBlock::GraphBlockInfo{//
            .ownerSchedulerUniqueName = ownerSchedulerUniqueName};

    } else if (newBlock->blockCategory == "ScheduledBlockGroup") {
        newBlock->blockCategoryInfo = UiGraphBlock::SchedulerBlockInfo{};

    } else {
        newBlock->blockCategoryInfo = UiGraphBlock::NormalBlockInfo{//
            .ownerGraphUniqueName     = ownerGraphUniqueName,       //
            .ownerSchedulerUniqueName = ownerSchedulerUniqueName};
    }

    // Sets the block data, including its children if it is a
    // scheduler or a graph
    newBlock->setBlockData(blockData);

    return newBlock;
}

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree) const { return blockInTree(block, tree, UiGraphPort::Role::Source) || blockInTree(block, tree, UiGraphPort::Role::Destination); }

bool UiGraphModel::blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const {
    if (!block.parentBlock) {
        return false;
    }

    if (&block == &tree) {
        return true;
    }

    const UiGraphPort::Role role1 = direction == UiGraphPort::Role::Source ? UiGraphPort::Role::Destination : UiGraphPort::Role::Source;
    const UiGraphPort::Role role2 = direction;

    auto edges = block.parentBlock->childEdges | std::views::filter([&](const auto& edge) { return edge.getBlock(role1) == &tree; });
    for (auto edge : edges) {
        auto neighbourBlock = edge.getBlock(role2);
        if (neighbourBlock && blockInTree(block, *neighbourBlock, direction)) {
            return true;
        }
    }

    return false;
}

constexpr bool isColourField(const UiGraphBlock::SettingsMetaInformation& meta, std::string_view pattern) {
    auto containsColo = [](std::string_view s) {
        constexpr std::string_view target = "colo";
        return !std::ranges::search(s, target, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }).empty();
    };
    return containsColo(meta.description) || containsColo(pattern);
};

UiGraphBlock::SettingsControlType UiGraphBlock::SettingsMetaInformation::controlType(std::string_view propertyName, const gr::pmt::Value& value) const {
    SettingsControlType out{};
    gr::pmt::ValueVisitor([&](auto& currentValue) {
        using T = std::remove_cvref_t<decltype(currentValue)>;
        if constexpr (std::is_same_v<T, bool>) {
            out = SettingsControlType::Checkbox;
        } else if constexpr (std::integral<T>) {
            if constexpr (std::unsigned_integral<T> && sizeof(T) >= 4) {
                if (isColourField(*this, propertyName)) {
                    out = SettingsControlType::Color;
                    return;
                }
            }
            out = (minValue && maxValue) ? SettingsControlType::Slider : SettingsControlType::Keypad;
        } else if constexpr (std::floating_point<T>) {
            out = (minValue && maxValue) ? SettingsControlType::Slider : SettingsControlType::Keypad;
        } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string>) {
            out = !enumValues.empty() ? SettingsControlType::Combo : SettingsControlType::TextInput;
        }
    }).visit(value);
    return out;
}
