#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include <map>
#include <memory>
#include <optional>
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
    UiGraphModel* ownerGraph  = nullptr;
    UiGraphBlock* parentBlock = nullptr;

    std::string blockUniqueName;

    struct NormalBlockInfo {
        std::string ownerGraphUniqueName;
        std::string ownerSchedulerUniqueName;
    };
    struct GraphBlockInfo {
        std::string ownerSchedulerUniqueName;
    };
    struct SchedulerBlockInfo {
        bool childrenLoaded = false;
    };
    std::variant<std::monostate, NormalBlockInfo, GraphBlockInfo, SchedulerBlockInfo> blockCategoryInfo;

    std::string ownerSchedulerUniqueName() const {
        return std::visit(gr::meta::overloaded{[this](const SchedulerBlockInfo&) { return blockUniqueName; }, //
                              [](std::monostate) {
                                  assert(false && "monostate can not be info for an initialized block");
                                  return std::string();
                              }, //
                              [](const auto& info) { return info.ownerSchedulerUniqueName; }},
            blockCategoryInfo);
    }

    std::string blockName;
    std::string blockTypeName;
    std::string blockCategory;
    std::string blockUiCategory;

    bool blockIsBlocking = false;

    std::vector<UiGraphPort> _inputPorts;
    std::vector<UiGraphPort> _outputPorts;

    const std::vector<UiGraphPort>& inputPorts() { return _inputPorts; }
    const std::vector<UiGraphPort>& outputPorts() { return _outputPorts; }

    void                       setSetting(std::string_view keyToUpdate, gr::pmt::Value&& updatedValue);
    void                       setBlockData(const gr::property_map& data);
    void                       setBasicBlockData(const gr::property_map& blockData);
    void                       setGraphChildren(const gr::property_map& data);
    void                       setSchedulerGraph(const gr::property_map& data);
    std::optional<UiGraphEdge> parseEdgeData(const gr::property_map& edgeData);

    [[nodiscard]] constexpr bool isPlotSink() const { return this->blockTypeName.starts_with("opendigitizer::ImPlotSink"); }
    [[nodiscard]] constexpr bool isScheduler() const { return std::holds_alternative<SchedulerBlockInfo>(blockCategoryInfo); }
    [[nodiscard]] constexpr bool isGraph() const { return std::holds_alternative<GraphBlockInfo>(blockCategoryInfo); } // unmanaged/unscheduled

    // We often search by name, but as we don't expect graphs with
    // a large $n$ of blocks, linear search will be fine
    std::vector<std::unique_ptr<UiGraphBlock>> childBlocks;
    std::vector<UiGraphEdge>                   childEdges;

    bool newGraphDataBeingSet  = false;
    bool shouldRearrangeBlocks = false;

    // Handlers for graph and schdeuler events
    void handleChildBlockEmplaced(const gr::property_map& blockData);
    void handleChildEdgeEmplaced(const gr::property_map& data);
    bool handleChildBlockRemoved(const std::string& uniqueName);
    void handleChildEdgeRemoved(const gr::property_map& data);
    void handlePortExported(const gr::property_map& data);

    UiGraphBlock* findBlockByUniqueName(const std::string& uniqueName);

private:
    enum class SearchProperty { UniqueName, Name };
    auto findBlockIteratorBy(std::initializer_list<SearchProperty>, std::string_view value);
    auto findBlockIteratorByUniqueName(std::string_view uniqueName);
    auto findPortIteratorByName(auto& ports, const std::string& portName);

public:
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

    enum class SettingsControlType {
        Color,
        Checkbox,
        Slider,
        Keypad,
        Combo,
        TextInput,
    };

    struct SettingsMetaInformation {
        std::string              unit;
        std::string              description;
        bool                     isVisible = false;
        std::optional<double>    minValue;
        std::optional<double>    maxValue;
        std::vector<std::string> enumValues;

        SettingsControlType controlType(std::string_view propertyName, const gr::pmt::Value& value) const;
    };

    struct ExportedProperty {
        // if null, this is not visible on any control
        std::optional<std::size_t> windowId;
    };

    template<typename Key, typename Value>
    using UnorderedMap = std::unordered_map<Key, Value, gr::pmt::Value::MapHash, gr::pmt::Value::MapEqual>;

    UnorderedMap<std::string, ExportedProperty>    exportedProperties;
    std::map<std::string, SettingsMetaInformation> blockSettingsMetaInformation;
    void                                           updateBlockSettingsMetaInformation();

    struct PortNameMapper {
        std::string internalName;
        std::string exportedName;
        auto        operator<=>(const PortNameMapper&) const = default;
        bool        operator==(const PortNameMapper&) const  = default;
    };
    std::map<std::string, std::set<PortNameMapper>> exportedInputPorts;
    std::map<std::string, std::set<PortNameMapper>> exportedOutputPorts;

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

    UiGraphBlock(UiGraphModel* ownerGraph_, UiGraphBlock* parentBlock_) : ownerGraph(ownerGraph_), parentBlock(parentBlock_) {}

    UiGraphBlock(const UiGraphBlock&)            = delete;
    UiGraphBlock& operator=(const UiGraphBlock&) = delete;
    UiGraphBlock(UiGraphBlock&&)                 = delete;
    UiGraphBlock& operator=(UiGraphBlock&&)      = delete;

    void requestBlockUpdate();

    void getAllContexts();

    void setActiveContext(const ContextTime& contextTime);
    void getActiveContext();

    void addContext(const ContextTime& contextTime);
    void removeContext(const ContextTime& contextTime);

    bool isConnected() const;
};

class UiGraphModel {
public:
    UiGraphModel() : rootBlock(this, nullptr) {}

    std::function<void(gr::Message, std::source_location)> sendMessage_;

    void sendMessage(gr::Message message, std::source_location location = std::source_location::current()) { sendMessage_(std::move(message), std::move(location)); }

    UiGraphBlock rootBlock;

    std::string m_localFlowgraphGrc;
    bool        requestedFullUpdate = false;

    // Not a multimap as filtered lists like sequence collections
    std::map<std::string, std::set<std::string>> knownBlockTypes;
    std::map<std::string, std::set<std::string>> knownSchedulerTypes;

    UiGraphBlock* selectedBlock = nullptr;

    /**
     * @return true if consumed the message
     */
    bool processMessage(const gr::Message& message);

    void requestFullUpdate(std::source_location location = std::source_location::current());
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

    struct FindBlockResult {
        operator bool() const { return block != nullptr; }

        UiGraphBlock*                                 parentGraph        = nullptr;
        UiGraphBlock*                                 block              = nullptr;
        decltype(UiGraphBlock::childBlocks)*          owningCollection   = nullptr;
        decltype(UiGraphBlock::childBlocks)::iterator owningCollectionIt = {};
    };
    FindBlockResult recursiveFindBlockByUniqueName(std::string_view uniqueName);
    FindBlockResult recursiveFindBlockByName(std::string_view name);

    struct ExportedPropertyMatchResult {
        UiGraphBlock* block;
        std::string   propertyName;
    };

    using ExportedPropertiesView = std::unordered_map<std::string_view, UiGraphBlock::UnorderedMap<std::string, UiGraphBlock::ExportedProperty>*>;
    /// Returns a map of block names to their exported properties, if the exported properties are not empty
    ExportedPropertiesView                   recursiveGatherExportedProperties();
    std::vector<ExportedPropertyMatchResult> recursiveGatherMatchingExportedProperties(std::size_t id, UiGraphBlock* exclude);
    std::vector<UiGraphBlock*>               recursiveGatherPlotSinks();

    std::unique_ptr<UiGraphBlock> makeGraphBlock(UiGraphBlock* parent, const gr::property_map& blockData, const std::string& ownerSchedulerUniqueName, const std::string& ownerGraphUniqueName);

private:
    void handleBlockDataUpdated(const std::string& uniqueName, const gr::property_map& blockData);
    void handleBlockSettingsChanged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockSettingsStaged(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockActiveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAllContexts(const std::string& uniqueName, const gr::property_map& data);
    void handleBlockAddOrRemoveContext(const std::string& uniqueName, const gr::property_map& data);
    void handleAvailableGraphBlockTypes(const gr::property_map& data);
    void handleAvailableGraphSchedulerTypes(const gr::property_map& data);

    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const;

    enum class VisitorResult {
        Recurse,  // recurse into children
        Continue, // continue to sibling and ignore children of this block
        Break,
    };
    void recursiveForEachBlock(const std::function<VisitorResult(const FindBlockResult& element)>& callback);
};

} // namespace DigitizerUi
#endif // include guard
