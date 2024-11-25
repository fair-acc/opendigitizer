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

    UiGraphEdge(UiGraphModel* owner) : ownerGraph(owner), edgeSourcePortDefinition(std::string()), edgeDestinationPortDefinition(std::string()) {}
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

    // void setPluginLoader(std::shared_ptr<gr::PluginLoader> loader);
    // void parse(const std::filesystem::path& file);
    // void parse(const std::string& str);
    // void clear();

    const auto& blocks() const { return m_blocks; }
    auto&       blocks() { return m_blocks; }

    const auto& edges() const { return m_edges; }
    auto&       edges() { return m_edges; }

    void processMessage(const gr::Message& message);

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
    void handleEdgeEmplaced(const gr::property_map& data);
    void handleEdgeRemoved(const gr::property_map& data);
    void handleGraphRedefined(const gr::property_map& data);

    void               setBlockData(auto& block, const gr::property_map& blockData);
    [[nodiscard]] bool setEdgeData(auto& edge, const gr::property_map& edgeData);
    void               removeEdgesForBlock(UiGraphBlock& block);
};

#endif // include guard
