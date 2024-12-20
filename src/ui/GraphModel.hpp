#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include <string>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

class UiGraphModel;
struct UiGraphBlock;

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

    std::vector<UiGraphPort> inputPorts;
    std::vector<UiGraphPort> outputPorts;

    // Rendering-related members
    std::vector<float> inputPortWidths;
    std::vector<float> outputPortWidths;

    // TODO when nested graphs support gets here
    // std::vector<UiGraphBlock> children
    // std::vector<UiGraphEdge> edges

    UiGraphBlock(UiGraphModel* owner) : ownerGraph(owner) {}

    void getAllContexts();

    void setActiveContext(const ContextTime& contextTime);
    void getActiveContext();

    void addContext(const ContextTime& contextTime);
    void removeContext(const ContextTime& contextTime);
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
};

class UiGraphModel {
private:
    // We often search by name, but as we don't expect graphs with
    // a large $n$ of blocks, linear search will be fine
    std::vector<UiGraphBlock> _blocks;
    std::vector<UiGraphEdge>  _edges;
    bool                      _newGraphDataBeingSet = false;

public:
    UiGraphModel() {}

    std::string blockUniqueName;
    std::string blockName;
    std::string blockTypeName;

    // void setPluginLoader(std::shared_ptr<gr::PluginLoader> loader);
    // void parse(const std::filesystem::path& file);
    // void parse(const std::string& str);
    // void clear();

    const auto& blocks() const { return _blocks; }
    auto&       blocks() { return _blocks; }

    const auto& edges() const { return _edges; }
    auto&       edges() { return _edges; }

    /**
     * @return true if consumed the message
     */
    bool processMessage(const gr::Message& message);

    void requestGraphUpdate();

    // TODO
    // requestEmplaceBlock
    // requestEmplaceEdge
    // ...

private:
    auto findBlockByName(const std::string& uniqueName);
    auto findPortByName(auto& ports, const std::string& uniqueName);

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

    void               setBlockData(auto& block, const gr::property_map& blockData);
    [[nodiscard]] bool setEdgeData(auto& edge, const gr::property_map& edgeData);
    void               removeEdgesForBlock(UiGraphBlock& block);
};

#endif // include guard
