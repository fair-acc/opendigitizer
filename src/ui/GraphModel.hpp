#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include <filesystem>
#include <memory>
#include <string>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

class UiGraphModel;
struct UiGraphBlock;

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

struct UiGraphPort {
    UiGraphBlock* ownerBlock = nullptr;

    std::string       portName;
    std::string       portType;
    bool              portTypeIsDataset = false;
    gr::PortDirection portDirection;

    UiGraphPort(UiGraphBlock* owner) : ownerBlock(owner) {}
};

// TODO: Merge UiGraphBlock and UiGraphModel for nested graph
// support
struct UiGraphBlock {
    UiGraphModel* ownerGraph = nullptr;

    std::string blockUniqueName;
    std::string blockName;
    std::string blockTypeName;
    std::string blockCategory;
    std::string blockUiCategory;

    bool blockIsBlocking = false;

    struct ViewData {
        float x      = 0;
        float y      = 0;
        float width  = 0;
        float height = 0;
    };
    std::optional<ViewData> view;

    gr::property_map blockSettings;
    gr::property_map blockMetaInformation;

    std::vector<UiGraphPort> inputPorts;
    std::vector<UiGraphPort> outputPorts;

    // Rendering-related members
    std::vector<float> inputPortWidths;
    std::vector<float> outputPortWidths;

    // TODO when nested graphs support gets here
    // std::vector<UiGraphBlock> children
    // std::vector<UiGraphEdge> edges

    UiGraphBlock(UiGraphModel* owner) : ownerGraph(owner) {}
};

struct UiGraphEdge {
    UiGraphModel* ownerGraph = nullptr;

    UiGraphPort* edgeSourcePort      = nullptr;
    UiGraphPort* edgeDestinationPort = nullptr;

    std::string        edgeSourceBlockName;
    gr::PortDefinition edgeSourcePortDefinition;
    std::string        edgeDestinationBlockName;
    gr::PortDefinition edgeDestinationPortDefinition;

    std::int32_t edgeWeight        = 0;
    std::size_t  edgeMinBufferSize = 0;
    std::size_t  edgeBufferSize    = 0;
    std::size_t  edgeNReaders      = 0;
    std::size_t  edgeNWriters      = 0;
    std::string  edgeName;
    std::string  edgeState;
    std::string  edgeType;

    UiGraphEdge(UiGraphModel* owner) : ownerGraph(owner), edgeSourcePortDefinition(""s), edgeDestinationPortDefinition(""s) {}
};

class UiGraphModel {
private:
    // We often search by name, but as we don't expect graphs with
    // a large $n$ of blocks, linear search will be fine
    std::vector<UiGraphBlock> m_blocks;
    std::vector<UiGraphEdge>  m_edges;

public:
    UiGraphModel() {}

    std::string blockUniqueName;
    std::string blockName;
    std::string blockTypeName;

    void setPluginLoader(std::shared_ptr<gr::PluginLoader> loader);
    void parse(const std::filesystem::path& file);
    void parse(const std::string& str);
    void clear();

    const auto& blocks() const { return m_blocks; }
    auto&       blocks() { return m_blocks; }

    const auto& edges() const { return m_edges; }
    auto&       edges() { return m_edges; }

    void processMessage(const gr::Message& message) {
        fmt::print("\u001b[32m>> GraphModel got {} for {}\n\u001b[0m", message.endpoint, message.serviceName);
        namespace graph = gr::graph::property;
        namespace block = gr::block::property;

        if (!message.data) {
            // TODO report error
        }

        const auto& data = *message.data;

        auto uniqueName = [&data](std::string key = "uniqueName"s) {
            auto it = data.find("uniqueName"s);
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
            fmt::print("\u001b[32mStaged settings: {}\n\u001b[0m", data);
            handleBlockSettingsStaged(message.serviceName, data);

        } else if (message.endpoint == graph::kEdgeEmplaced) {
            // register the new edge
        } else if (message.endpoint == graph::kEdgeRemoved) {
            // find edge and remove
        } else if (message.endpoint == graph::kGraphInspected) {
            handleGraphRedefined(data);

        } else if (message.endpoint == "LifecycleState") {
            fmt::print("\u001b[32mNew state {}\n\u001b[0m", data);

        } else {
            fmt::print("\u001b[32mNot processed: {}\n\u001b[0m", message.endpoint);
        }
        //
    }

    // TODO
    // requestEmplaceBlock
    // requestEmplaceEdge
    // ...

private:
    auto findBlockByName(const std::string& uniqueName) {
        auto it = std::ranges::find_if(m_blocks, [&](const auto& block) { return block.blockUniqueName == uniqueName; });
        return std::make_pair(it, it != m_blocks.end());
    }

    bool handleBlockRemoved(const std::string& uniqueName) {
        auto [blockIt, found] = findBlockByName(uniqueName);
        if (!found) {
            // TODO warning
            fmt::print("\u001b[32mWARNING: Unknown block removed {}\n\u001b[0m", uniqueName);
            return false;
        }

        // Delete edges for the removed block
        removeEdgesForBlock(*blockIt);
        m_blocks.erase(blockIt);
        return true;
    }

    void handleBlockEmplaced(const gr::property_map& blockData) {
        const auto uniqueName  = getProperty<std::string>(blockData, "uniqueName"s);
        const auto [it, found] = findBlockByName(uniqueName);
        if (found) {
            setBlockData(*it, blockData);
        } else {
            auto& newBlock = m_blocks.emplace_back(/*owner*/ this);
            setBlockData(newBlock, blockData);
        }
    }

    void handleBlockDataUpdated(std::string uniqueName, const gr::property_map& blockData) {
        auto [blockIt, found] = findBlockByName(uniqueName);
        if (!found) {
            // TODO warning
            fmt::print("\u001b[32mWARNING: Data updated for an unknown block {}\n\u001b[0m", uniqueName);
            return;
        }

        setBlockData(*blockIt, blockData);
    }

    void handleBlockSettingsChanged(std::string uniqueName, const gr::property_map& data) {
        auto [blockIt, found] = findBlockByName(uniqueName);
        if (!found) {
            // TODO warning
            fmt::print("\u001b[32mWARNING: Settings updated for an unknown block {} -> {}\n\u001b[0m", uniqueName, data);
            return;
        }
        fmt::print("\u001b[32mNew settings for {} are {}\n\u001b[0m", uniqueName, data);
    }

    void handleBlockSettingsStaged(std::string uniqueName, const gr::property_map& data) {
        auto [blockIt, found] = findBlockByName(uniqueName);
        if (!found) {
            // TODO warning
            fmt::print("\u001b[32mWARNING: Settings staged for an unknown block {} -> {}\n\u001b[0m", uniqueName, data);
            return;
        }
        fmt::print("\u001b[32mNew settings for {} are {}\n\u001b[0m", uniqueName, data);
    }

    void handleGraphRedefined(const gr::property_map& data) {
        // Strictly speaking, UiGraphModel is not a block even if
        // gr::Graph is a gr::Block, but we can set some basic
        // properties like this
        setBlockData(*this, data);

        // Update or create blocks that GR knows
        const auto& children = getProperty<gr::property_map>(data, "children");
        for (const auto& [blockUniqueName, blockData] : children) {
            fmt::print("\u001b[32mGraph contains child {}\n\u001b[0m", blockUniqueName);
            const auto [blockIt, found] = findBlockByName(blockUniqueName);
            if (found) {
                fmt::print("\u001b[32mBlock {} exists, updating...\n\u001b[0m", blockUniqueName);
                setBlockData(*blockIt, std::get<gr::property_map>(blockData));
            } else {
                fmt::print("\u001b[32mBlock {} does not exist, creating...\n\u001b[0m", blockUniqueName);
                handleBlockEmplaced(std::get<gr::property_map>(blockData));
            }
        }

        // Delete blocks that GR doesn't know about.
        // This is similar to erase-remove, but we need the list of blocks
        // we want to delete in order to disconnect them first.
        auto toRemove = std::partition(m_blocks.begin(), m_blocks.end(), [&children](const auto& block) { //
            return children.contains(block.blockUniqueName);
        });
        for (; toRemove != m_blocks.end(); ++toRemove) {
            fmt::print("\u001b[32mBlock {} no longer exists, removing...\n\u001b[0m", toRemove->blockUniqueName);
            removeEdgesForBlock(*toRemove);
        }
        m_blocks.erase(toRemove, m_blocks.end());

        // Establish new edges
        const auto& edges = getProperty<gr::property_map>(data, "edges");
        for (const auto& [index, edgeData_] : edges) {
            const auto edgeData = std::get<gr::property_map>(edgeData_);

            UiGraphEdge edge(this);
            updateFieldFrom(edge.edgeSourceBlockName, edgeData, "sourceBlock"s);
            updateFieldFrom(edge.edgeDestinationBlockName, edgeData, "destinationBlock"s);

            auto portDefinition = [&edgeData](std::string key) -> gr::PortDefinition {
                auto stringPortDefinition = getOptionalProperty<std::string>(edgeData, key);
                if (stringPortDefinition) {
                    return gr::PortDefinition(*stringPortDefinition);
                } else {
                    auto topLevel = getProperty<std::size_t>(edgeData, key + ".topLevel");
                    auto subIndex = getProperty<std::size_t>(edgeData, key + ".subIndex");
                    return gr::PortDefinition(topLevel, subIndex);
                }
            };

            edge.edgeSourcePortDefinition      = portDefinition("sourcePort"s);
            edge.edgeDestinationPortDefinition = portDefinition("destinationPort"s);

            fmt::print("\u001b[32mGraph contains edge {}: {} -> {}\n\u001b[0m", index, edge.edgeSourceBlockName, edge.edgeDestinationBlockName);

            auto findPortFor = [this](std::string& currentBlockName, auto member, const gr::PortDefinition& portDefinition) -> UiGraphPort* {
                auto [it, found] = findBlockByName(currentBlockName);
                if (!found) {
                    // TODO report error
                    fmt::print("\u001b[32mGot an unknown block in a connection {}\n\u001b[0m", currentBlockName);
                    return nullptr;
                }

                auto& block = *it;
                auto& ports = std::invoke(member, block);

                return std::visit(gr::meta::overloaded{//
                                      [&ports](const gr::PortDefinition::IndexBased& indexBasedDefinition) -> UiGraphPort* {
                                          // TODO: sub-index for ports -- when we add UI support for
                                          // port arrays
                                          if (indexBasedDefinition.topLevel >= ports.size()) {
                                              fmt::print("\u001b[32mPort {} not found in in block while trying to connect\n\u001b[0m", indexBasedDefinition.topLevel);
                                              return nullptr;
                                          }

                                          return std::addressof(ports[indexBasedDefinition.topLevel]);
                                      },
                                      [&ports](const gr::PortDefinition::StringBased& stringBasedDefinition) -> UiGraphPort* { //
                                          auto portIt = std::ranges::find_if(ports, [&](const auto& port) {                    //
                                              return port.portName == stringBasedDefinition.name;
                                          });
                                          if (portIt == ports.end()) {
                                              fmt::print("\u001b[32mPort {} not found in in block while trying to connect\n\u001b[0m", stringBasedDefinition.name);
                                              return nullptr;
                                          }
                                          return std::addressof(*portIt);
                                      }},
                    portDefinition.definition);
            };

            edge.edgeSourcePort      = findPortFor(edge.edgeSourceBlockName, &UiGraphBlock::outputPorts, edge.edgeSourcePortDefinition);
            edge.edgeDestinationPort = findPortFor(edge.edgeDestinationBlockName, &UiGraphBlock::inputPorts, edge.edgeDestinationPortDefinition);

            if (!edge.edgeSourcePort || !edge.edgeDestinationPort) {
                continue;
            }

            updateFieldFrom(edge.edgeWeight, edgeData, "weight"s);
            updateFieldFrom(edge.edgeName, edgeData, "edgeName"s);
            updateFieldFrom(edge.edgeType, edgeData, "edgeType"s);
            updateFieldFrom(edge.edgeMinBufferSize, edgeData, "minBufferSize"s);
            updateFieldFrom(edge.edgeBufferSize, edgeData, "bufferSize"s);
            updateFieldFrom(edge.edgeState, edgeData, "edgeState"s);
            updateFieldFrom(edge.edgeNReaders, edgeData, "nReaders"s);
            updateFieldFrom(edge.edgeNWriters, edgeData, "nWriters"s);

            m_edges.emplace_back(std::move(edge));
        }
    }

    void setBlockData(auto& block, const gr::property_map& blockData) {
        updateFieldFrom(block.blockUniqueName, blockData, "uniqueName"s);
        updateFieldFrom(block.blockName, blockData, "name"s);
        updateFieldFrom(block.blockTypeName, blockData, "typeName"s);

        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(block)>, UiGraphBlock>) {
            updateFieldFrom(block.blockSettings, blockData, "settings"s);
            updateFieldFrom(block.blockMetaInformation, blockData, "metaInformation"s);

            updateFieldFrom(block.blockCategory, blockData, "blockCategory"s);
            updateFieldFrom(block.blockUiCategory, blockData, "uiCategory"s);
            updateFieldFrom(block.blockIsBlocking, blockData, "isBlocking"s);

            auto processPorts = [&block, &blockData](auto& portsCollection, std::string portsField, gr::PortDirection direction) {
                for (const auto& [portName, portData_] : getProperty<gr::property_map>(blockData, portsField)) {
                    const auto& portData = std::get<gr::property_map>(portData_);
                    fmt::print("\u001b[32mEncoded input port: {} {}\n\u001b[0m", portName, portData);
                    auto& port         = portsCollection.emplace_back(/*owner*/ std::addressof(block));
                    port.portName      = portName;
                    port.portType      = getProperty<std::string>(portData, "type"s);
                    port.portDirection = direction;
                }
            };

            processPorts(block.inputPorts, "inputPorts"s, gr::PortDirection::INPUT);
            processPorts(block.outputPorts, "outputPorts"s, gr::PortDirection::OUTPUT);
            block.inputPortWidths.clear();
            block.outputPortWidths.clear();
        }

        // TODO: When adding support for nested graphs, need to process
        // getProperty<gr::property_map>(blockData, "children"s))
        // getProperty<gr::property_map>(blockData, "edges"s))
    }

    void removeEdgesForBlock(UiGraphBlock& block) {
        std::erase_if(m_edges, [blockPtr = std::addressof(block)](const auto& edge) {
            return edge.edgeSourcePort->ownerBlock == blockPtr || //
                   edge.edgeDestinationPort->ownerBlock == blockPtr;
        });
    }

    // void handleEdgeRemoved()
};

#endif // include guard
