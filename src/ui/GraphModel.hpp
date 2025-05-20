#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include <map>
#include <memory>
#include <string>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

namespace DigitizerUi {

class UiGraphModel;
struct UiGraphBlock;

struct UiGraphPort {
    enum class Role { Source, Destination };

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

    struct ContextTime {
        std::string   context;
        std::uint64_t time = 1;
    };
    ContextTime              activeContext;
    std::vector<ContextTime> contexts;

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

    struct SettingsMetaInformation {
        std::string unit;
        std::string description;
        bool        isVisible;
    };

    std::map<std::string, SettingsMetaInformation> blockSettingsMetaInformation;
    void                                           updateBlockSettingsMetaInformation();

    std::vector<UiGraphPort> inputPorts;
    std::vector<UiGraphPort> outputPorts;

    // TODO when nested graphs support gets here
    // std::vector<UiGraphBlock> children
    // std::vector<UiGraphEdge> edges

    UiGraphBlock(UiGraphModel* owner) : ownerGraph(owner) {}

    UiGraphBlock(const UiGraphBlock&)            = delete;
    UiGraphBlock& operator=(const UiGraphBlock&) = delete;
    UiGraphBlock(UiGraphBlock&&)                 = delete;
    UiGraphBlock& operator=(UiGraphBlock&&)      = delete;

    void getAllContexts();

    void setActiveContext(const ContextTime& contextTime);
    void getActiveContext();

    void addContext(const ContextTime& contextTime);
    void removeContext(const ContextTime& contextTime);

    bool isConnected() const;
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

    UiGraphEdge(UiGraphModel* owner) : ownerGraph(owner), edgeSourcePortDefinition(std::string()), edgeDestinationPortDefinition(std::string()) {}

    /// returns the source or the destination block
    UiGraphBlock* getBlock(UiGraphPort::Role role) const {
        if (role == UiGraphPort::Role::Source) {
            return edgeSourcePort ? edgeSourcePort->ownerBlock : nullptr;
        } else {
            return edgeDestinationPort ? edgeDestinationPort->ownerBlock : nullptr;
        }
    }
};

class UiGraphModel {
private:
    // We often search by name, but as we don't expect graphs with
    // a large $n$ of blocks, linear search will be fine
    std::vector<std::unique_ptr<UiGraphBlock>> _blocks;
    std::vector<UiGraphEdge>                   _edges;
    bool                                       _newGraphDataBeingSet = false;
    bool                                       _rearrangeBlocks      = false;

public:
    UiGraphModel() {}

    std::function<void(gr::Message)> sendMessage;

    std::string blockUniqueName;
    std::string blockName;
    std::string blockTypeName;

    // Not a multimap as filtered lists like sequence collections
    std::map<std::string, std::set<std::string>> knownBlockTypes;

    UiGraphBlock* selectedBlock = nullptr;

    const auto& blocks() const { return _blocks; }
    auto&       blocks() { return _blocks; }

    const auto& edges() const { return _edges; }
    auto&       edges() { return _edges; }

    void reset() {
        _blocks.clear();
        _edges.clear();
    }

    /**
     * @return true if consumed the message
     */
    bool processMessage(const gr::Message& message);

    void requestGraphUpdate();
    void requestAvailableBlocksTypesUpdate();

    /// Returns whether a block is connected directly or indirectly to another block
    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree) const;

    struct AvailableParametrizationsResult {
        std::string baseType;
        std::string parametrization;
        // We still don't have optional of references
        const std::set<std::string>* availableParametrizations;
    };

    AvailableParametrizationsResult availableParametrizationsFor(const std::string& fullBlockType) const;

    UiGraphBlock* findBlockByUniqueName(const std::string& uniqueName);

    /**
     * @return true when new blocks were added or removed so UI can rearrange them
     */
    bool rearrangeBlocks() const;

    void setRearrangedBlocks();

private:
    auto findBlockIteratorByUniqueName(const std::string& uniqueName);
    auto findPortIteratorByName(auto& ports, const std::string& portName);

    bool handleBlockRemoved(const std::string& uniqueName);
    void handleBlockEmplaced(const gr::property_map& blockData);
    void handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData);
    void handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleEdgeEmplaced(const gr::property_map& data);
    void handleEdgeRemoved(const gr::property_map& data);
    void handleGraphRedefined(const gr::property_map& data);
    void handleAvailableGraphBlockTypes(const gr::property_map& data);

    void               setBlockData(auto& block, const gr::property_map& blockData);
    [[nodiscard]] bool setEdgeData(auto& edge, const gr::property_map& edgeData);
    void               removeEdgesForBlock(UiGraphBlock& block);

    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const;
};

} // namespace DigitizerUi
#endif // include guard
