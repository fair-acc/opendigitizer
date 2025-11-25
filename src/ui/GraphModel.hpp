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

struct UiGraphEdge {
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

    UiGraphEdge() : edgeSourcePortDefinition(std::string()), edgeDestinationPortDefinition(std::string()) {}

    /// returns the source or the destination block
    UiGraphBlock* getBlock(UiGraphPort::Role role) const {
        if (role == UiGraphPort::Role::Source) {
            return edgeSourcePort ? edgeSourcePort->ownerBlock : nullptr;
        } else {
            return edgeDestinationPort ? edgeDestinationPort->ownerBlock : nullptr;
        }
    }
};

struct UiGraphBlock {
    UiGraphModel* ownerGraph = nullptr;

    // Basic block information

    std::string blockUniqueName;

    // If this block is a scheduler, the blockUniqueName will contain
    // the unique name of the nested graph block and schedulerUniqueName
    // will contain the unique name of the scheduler.
    std::optional<std::string> schedulerUniqueName;
    std::string                ownerSchedulerUniqueName;

    std::string blockName;
    std::string blockTypeName;
    std::string blockCategory;
    std::string blockUiCategory;

    bool blockIsBlocking = false;

    std::vector<UiGraphPort> inputPorts;
    std::vector<UiGraphPort> outputPorts;

    void setBlockData(const gr::property_map& blockData);
    bool setEdgeData(auto& edge, const gr::property_map& edgeData);

    // Nested blocks and sub-graph info

    std::optional<std::string> managedGraphType;

    // We often search by name, but as we don't expect graphs with
    // a large $n$ of blocks, linear search will be fine
    std::vector<std::unique_ptr<UiGraphBlock>> childBlocks;
    std::vector<UiGraphEdge>                   childEdges;

    bool newGraphDataBeingSet  = false;
    bool shouldRearrangeBlocks = false;

    void setGraphData(const gr::property_map& data);

    void handleChildBlockEmplaced(const gr::property_map& blockData);

    UiGraphBlock* findBlockByUniqueName(const std::string& uniqueName);
    auto          findBlockIteratorByUniqueName(const std::string& uniqueName);
    auto          findPortIteratorByName(auto& ports, const std::string& portName);

    void removeEdgesForBlock(UiGraphBlock& block);

    // Settings and contexts

    struct ContextTime {
        std::string   context;
        std::uint64_t time = 1;
    };
    ContextTime              activeContext;
    std::vector<ContextTime> contexts;

    gr::property_map blockSettings;
    gr::property_map blockMetaInformation;

    struct SettingsMetaInformation {
        std::string unit;
        std::string description;
        bool        isVisible;
    };

    std::map<std::string, SettingsMetaInformation> blockSettingsMetaInformation;
    void                                           updateBlockSettingsMetaInformation();

    std::map<std::string, std::set<std::string>> exportedInputPorts;
    std::map<std::string, std::set<std::string>> exportedOutputPorts;
    void                                         updateExportedPorts();

    // UI-related data

    struct ViewData {
        float x      = 0;
        float y      = 0;
        float width  = 0;
        float height = 0;
    };
    std::optional<ViewData> view;

    struct StoredXY {
        float x = 0;
        float y = 0;
    };
    std::optional<StoredXY> storedXY;

    bool updatePosition = false;
    void storeXY();

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

class UiGraphModel {
public:
    UiGraphModel() : rootBlock(this) {}

    std::function<void(gr::Message)> sendMessage;

    UiGraphBlock rootBlock;

    std::string m_localFlowgraphGrc;

    // Not a multimap as filtered lists like sequence collections
    std::map<std::string, std::set<std::string>> knownBlockTypes;
    std::map<std::string, std::set<std::string>> knownSchedulerTypes;

    UiGraphBlock* selectedBlock = nullptr;

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

    auto recursiveFindBlockByUniqueName(const std::string& uniqueName);

private:
    bool handleBlockRemoved(const std::string& uniqueName);
    void handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData);
    void handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleEdgeEmplaced(const gr::property_map& data);
    void handleEdgeRemoved(const gr::property_map& data);
    void handleAvailableGraphBlockTypes(const gr::property_map& data);
    void handleAvailableGraphSchedulerTypes(const gr::property_map& data);

    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const;
};

} // namespace DigitizerUi
#endif // include guard
