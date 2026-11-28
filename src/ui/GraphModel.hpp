#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "UniqueNameRegistry.hpp"
#include "blocks/DrawableBlockConfig.hpp"
#include "blocks/TargetMap.hpp"

namespace gr::od {

/// e.g. "opendigitizer::ImPlotSink" for "opendigitizer::ImPlotSink<float32>"
[[nodiscard]] constexpr std::string_view withoutTemplateArguments(std::string_view typeName) { return typeName.substr(0UZ, typeName.find('<')); }

/// the C++ type name without its template parameters, e.g. "gr::od::ImPlotSink" for ImPlotSink<float>
template<typename TBlock>
[[nodiscard]] consteval std::string_view baseTypeName() {
    return withoutTemplateArguments(gr::meta::type_name<TBlock>());
}

/// accept the registered id as well as the C++ type name

[[nodiscard]] bool isChartTypeName(std::string_view typeName);
[[nodiscard]] bool isPlotSinkTypeName(std::string_view typeName);
[[nodiscard]] bool isDataSetPlotSinkTypeName(std::string_view typeName);

struct UiGraphModel;
struct UiGraphBlock;

struct UiGraphPort {
    enum class Role { Source, Destination };

    UiGraphBlock* ownerBlock = nullptr;

    std::string       portName;
    std::string       portType;
    gr::PortDirection portDirection;

    UiGraphPort(UiGraphBlock* owner) : ownerBlock(owner) {}

    /// the exported name in `exportedTo`, the root block (graph or scheduler); nullopt when not exported
    [[nodiscard]] std::optional<std::string> getExportedName(const UiGraphBlock* exportedTo) const;
    [[nodiscard]] bool                       isExportedTo(const UiGraphBlock* exportedTo) const { return getExportedName(exportedTo).has_value(); }
};

struct UiGraphEdge {
    UiGraphPort* edgeSourcePort      = nullptr;
    UiGraphPort* edgeDestinationPort = nullptr;

    std::string        edgeSourceBlockUniqueName;
    gr::PortDefinition edgeSourcePortDefinition;
    std::string        edgeDestinationBlockUniqueName;
    gr::PortDefinition edgeDestinationPortDefinition;

    std::int32_t edgeWeight        = 0;
    gr::Size_t   edgeMinBufferSize = 0;
    gr::Size_t   edgeBufferSize    = 0;
    gr::Size_t   edgeNReaders      = 0;
    gr::Size_t   edgeNWriters      = 0;
    std::string  edgeName;
    std::string  edgeState;
    std::string  edgeType;

    UiGraphEdge() : edgeSourcePortDefinition(std::string()), edgeDestinationPortDefinition(std::string()) {}

    UiGraphBlock* getBlock(UiGraphPort::Role role) const {
        if (role == UiGraphPort::Role::Source) {
            return edgeSourcePort ? edgeSourcePort->ownerBlock : nullptr;
        } else {
            return edgeDestinationPort ? edgeDestinationPort->ownerBlock : nullptr;
        }
    }
};

struct UiGraphBlock {
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

    enum class SearchProperty { UniqueName, Name };

    struct ContextTime {
        std::string   context;
        std::uint64_t time = 1;
    };

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

        SettingsControlType controlType(const gr::pmt::Value& value) const;
    };

    struct PortNameMapper {
        std::string internalName;
        std::string exportedName;
        auto        operator<=>(const PortNameMapper&) const = default;
        bool        operator==(const PortNameMapper&) const  = default;
    };

    struct StoredXY {
        float x = 0;
        float y = 0;
    };

    UiGraphModel* ownerGraph  = nullptr;
    UiGraphBlock* parentBlock = nullptr;

    std::string blockUniqueName;

    std::variant<std::monostate, NormalBlockInfo, GraphBlockInfo, SchedulerBlockInfo> blockCategoryInfo;

    std::string blockName;
    std::string blockTypeName;
    std::string blockCategory;

    // searched by name: linear search suffices for graphs of few blocks
    std::vector<std::unique_ptr<UiGraphBlock>> childBlocks;
    std::vector<UiGraphEdge>                   childEdges;

    bool newGraphDataBeingSet = false;

    ContextTime              activeContext;
    std::vector<ContextTime> contexts;

    gr::property_map blockSettings;
    gr::property_map blockMetaInformation;

    std::string computeDomainFallbackReason; // GR4's ComputeDomainFallback for the current run: ran on the host instead; empty: none

    std::map<std::string, SettingsMetaInformation> blockSettingsMetaInformation;

    std::map<std::string, std::set<PortNameMapper>> exportedInputPorts;
    std::map<std::string, std::set<PortNameMapper>> exportedOutputPorts;

    std::optional<StoredXY> storedXY;

    std::string storedEditorSettings;

    std::vector<UiGraphPort> _inputPorts;
    std::vector<UiGraphPort> _outputPorts;

    UiGraphBlock(UiGraphModel* ownerGraph_, UiGraphBlock* parentBlock_) : ownerGraph(ownerGraph_), parentBlock(parentBlock_) {}

    ~UiGraphBlock();

    UiGraphBlock(const UiGraphBlock&)            = delete;
    UiGraphBlock& operator=(const UiGraphBlock&) = delete;
    UiGraphBlock(UiGraphBlock&&)                 = delete;
    UiGraphBlock& operator=(UiGraphBlock&&)      = delete;

    std::string ownerSchedulerUniqueName() const {
        return std::visit(gr::meta::overloaded{[this](const SchedulerBlockInfo&) { return blockUniqueName; }, //
                              [](std::monostate) {
                                  assert(false && "monostate can not be info for an initialized block");
                                  return std::string();
                              }, //
                              [](const auto& info) { return info.ownerSchedulerUniqueName; }},
            blockCategoryInfo);
    }

    const std::vector<UiGraphPort>& inputPorts() { return _inputPorts; }
    const std::vector<UiGraphPort>& outputPorts() { return _outputPorts; }

    void setSetting(std::string_view keyToUpdate, gr::pmt::Value&& updatedValue);
    void setBlockData(const gr::property_map& data);
    void setBasicBlockData(const gr::property_map& blockData);
    void setBlockName(std::string newBlockName);
    /// rebuilds _inputPorts and _outputPorts: graphs and schedulers from their children's exported ports, other blocks from their own port list;
    /// true when derived from exported ports, i.e. when the ports may have changed
    [[nodiscard]] bool         updatePorts(const gr::property_map& blockData);
    void                       setGraphChildren(const gr::property_map& data);
    void                       setSchedulerGraph(const gr::property_map& data);
    std::optional<UiGraphEdge> parseEdgeData(const gr::property_map& edgeData);

    /// subgraph blocks change their ports with their exported and imported ports
    void graphResolveEdgePortPointersAndRemoveIfInvalid();

    [[nodiscard]] bool           isPlotSink() const { return isPlotSinkTypeName(blockTypeName); }
    [[nodiscard]] bool           isChart() const { return isChartTypeName(blockTypeName); }
    [[nodiscard]] bool           isUiControl() const;
    [[nodiscard]] bool           isViewOnlyUiControl() const;
    [[nodiscard]] bool           usesTargetMap() const { return blockSettings.contains("target_map"); }
    [[nodiscard]] constexpr bool isScheduler() const { return std::holds_alternative<SchedulerBlockInfo>(blockCategoryInfo); }
    [[nodiscard]] constexpr bool isGraph() const { return std::holds_alternative<GraphBlockInfo>(blockCategoryInfo); } // unmanaged/unscheduled

    [[nodiscard]] std::string                           uiControlCompatibleTypeMap() const;
    [[nodiscard]] std::expected<TargetMap, std::string> uiControlTargetMap() const {
        if (!usesTargetMap()) {
            return std::unexpected("Attempt to get target map for a block which does not implement it");
        }
        return TargetMap::fromString(blockSettings.value_or<std::string>("target_map", std::string{}));
    }
    /// "enum" instead of "string" when the meta information lists enumValues
    [[nodiscard]] std::string              propertyTypeName(std::string_view property) const;
    [[nodiscard]] bool                     isConnectableProperty(std::string_view property, std::string_view compatibleTypeMap) const;
    [[nodiscard]] std::vector<std::string> connectableProperties(std::string_view compatibleTypeMap) const;
    [[nodiscard]] BlockPropertyHints       propertyHints(std::string_view property) const;

    void emplaceChild(const gr::property_map& blockData);
    void emplaceEdge(const gr::property_map& data);
    bool removeChild(const std::string& uniqueName);
    void applySettings(const gr::property_map& data);

    UiGraphBlock* findBlockByUniqueName(const std::string& uniqueName);

    auto         findBlockIteratorBy(std::initializer_list<SearchProperty>, std::string_view value);
    auto         findPortIteratorByName(auto& ports, const std::string& portName);
    UiGraphPort* resolveChildPort(const std::string& childNameOrUniqueName, gr::PortDirection direction, const gr::PortDefinition& portDefinition);

    void removeEdgesForBlock(UiGraphBlock& block);

    void updateBlockSettingsMetaInformation();

    void requestBlockUpdate();

    void getAllContexts();

    void setActiveContext(const ContextTime& contextTime);
    void getActiveContext();

    void addContext(const ContextTime& contextTime);
    void removeContext(const ContextTime& contextTime);
};

[[nodiscard]] inline gr::Message makeMessage(gr::message::Command cmd, std::string serviceName, std::string endpoint, gr::property_map data = {}, std::string clientRequestID = {}) { //
    return gr::Message{.cmd = cmd, .serviceName = std::move(serviceName), .clientRequestID = std::move(clientRequestID), .endpoint = std::move(endpoint), .data = std::move(data)};
}

[[nodiscard]] inline gr::Message subscriptionMessageForAllBlocksSettings() { return makeMessage(gr::message::Command::Subscribe, "", gr::block::property::kSetting, {}, "UI"); }

/// the kEmplaceEdge payload the UI sends: minimum buffer 4096, weight 1, name "edge"
[[nodiscard]] gr::property_map emplaceEdgeData(std::string sourceBlock, std::string sourcePort, std::string destinationBlock, std::string destinationPort);

struct UiGraphModel {
    struct AvailableParametrizationsResult {
        std::string baseType;
        std::string parametrization;
        // no std::optional of references yet
        const std::set<std::string>* availableParametrizations;
    };

    struct FindBlockResult {
        UiGraphBlock* parentGraph = nullptr;
        UiGraphBlock* block       = nullptr;

        operator bool() const { return block != nullptr; }
    };

    enum class VisitorResult {
        Recurse,
        Continue, // to the sibling, skipping the children
        Break,
    };

    struct ParsedUiControlTargetMap {
        UiGraphBlock* control = nullptr;
        TargetMap     targets;
    };

    using ControlledPropertyMap = std::map<std::string, std::vector<UiGraphBlock*>, std::less<>>;

    struct BlockPointerPropertyNamePair {
        const UiGraphBlock* block = nullptr;
        std::string         property;
    };

    std::function<void(gr::Message)> sendMessage;

    UiGraphBlock rootBlock;

    UniqueNameRegistry nameRegistry;

    std::string localFlowgraphGrc;
    bool        requestedFullUpdate = false;

    // not a multimap, as filtered lists like sequence collections
    std::map<std::string, std::set<std::string>> knownBlockTypes;
    std::map<std::string, std::set<std::string>> knownSchedulerTypes;

    UiGraphBlock* selectedBlock = nullptr;

    // incremented by destroyed child blocks: a generation that tells the editor its NodeId pointers are invalid
    std::uint64_t blockDestructionCount = 0;

    std::uint64_t topologyGeneration = 0;

    /// only for tests waiting for a scheduler reply
    std::unordered_map<std::size_t, std::function<void(const gr::Message&)>> _testResponseSubscriptions;
    std::size_t                                                              _lastResponseSubscriptionId = 0;

    UiGraphModel() : rootBlock(this, nullptr) {}

    void send(gr::message::Command cmd, std::string serviceName, std::string endpoint, gr::property_map data = {}, std::string clientRequestID = {}) { sendMessage(makeMessage(cmd, std::move(serviceName), std::move(endpoint), std::move(data), std::move(clientRequestID))); }

    bool apply(const gr::Message& message); // true if consumed

    void onTopologyChanged();
    void rebuildNameRegistry();
    void requestFullUpdate(std::source_location location = std::source_location::current());
    void requestAvailableBlocksTypesUpdate();
    void saveBlockPositions(gr::property_map& graphData);

    /// whether `block` connects to `tree`, directly or indirectly
    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree) const;

    AvailableParametrizationsResult availableParametrizationsFor(const std::string& fullBlockType) const;

    /// breadth-first from rootBlock (included); `fn` returns a VisitorResult or nothing (= Recurse)
    template<typename Fn>
    void recursiveForEachBlock(Fn&& fn) {
        const auto visit = [&fn](const FindBlockResult& element) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn&, const FindBlockResult&>>) {
                fn(element);
                return VisitorResult::Recurse;
            } else {
                return fn(element);
            }
        };
        if (visit({.block = std::addressof(rootBlock)}) != VisitorResult::Recurse) {
            return;
        }
        std::deque<UiGraphBlock*> toProcess{std::addressof(rootBlock)};
        while (!toProcess.empty()) {
            UiGraphBlock* currentGraph = toProcess.front();
            toProcess.pop_front();
            for (const auto& block : currentGraph->childBlocks) {
                switch (visit({.parentGraph = currentGraph, .block = block.get()})) {
                case VisitorResult::Break: return;
                case VisitorResult::Continue: continue;
                case VisitorResult::Recurse: break;
                }
                if (!block->childBlocks.empty()) {
                    toProcess.push_back(block.get());
                }
            }
        }
    }
    FindBlockResult recursiveFindBlockByUniqueName(std::string_view uniqueName);
    FindBlockResult recursiveFindBlockByName(std::string_view name);

    [[nodiscard]] UiGraphBlock* rootGraphBlock();

    [[nodiscard]] std::vector<ParsedUiControlTargetMap> parseAllUiControlTargetMaps();

    /// the target block's properties, each with the UI control blocks connected to it
    [[nodiscard]] static ControlledPropertyMap uiControlledProperties(const UiGraphBlock& targetBlock, std::span<const ParsedUiControlTargetMap> parsedUiControlTargetMaps);
    [[nodiscard]] ControlledPropertyMap        uiControlledProperties(const UiGraphBlock& targetBlock) { return uiControlledProperties(targetBlock, parseAllUiControlTargetMaps()); }

    /// every property of every block whose type the control's "compatible_type_map" setting lists
    [[nodiscard]] std::vector<std::string> globConnectableProperties(const UiGraphBlock& control);

    /// every block property the target map refers to, except those of `control`
    [[nodiscard]] std::vector<BlockPointerPropertyNamePair> resolveControlledProperties(const UiGraphBlock& control, const TargetMap& targets);

    /// parses the control's TargetMap and searches the graph for matching properties; an error when the TargetMap does not parse
    [[nodiscard]] auto resolveControlledProperties(const UiGraphBlock& control) {
        return control.uiControlTargetMap().transform([this, &control](const TargetMap& targets) { return resolveControlledProperties(control, targets); });
    }

    /// fills first_controlled_property of `config` and combines the hints and type information of all controlled properties into the acceptable min/max range and choices
    void aggregateAndApplyControlledProperties(DrawableBlockConfig& config, const UiGraphBlock& control);

    /// whether the control, with the controlled properties from resolveControlledProperties(), can control the given property
    [[nodiscard]] static bool isCompatibleWithControlledProperties(const UiGraphBlock& control, std::span<const BlockPointerPropertyNamePair> controlled, const BlockPointerPropertyNamePair& property);

    /// whether any property of `candidate` passes isCompatibleWithControlledProperties
    [[nodiscard]] static bool doesBlockHaveACompatibleProperty(const UiGraphBlock& control, std::span<const BlockPointerPropertyNamePair> controlled, const UiGraphBlock& candidate);

    void removeUiControlConnection(const std::string& controlUniqueName, std::string_view blockSelector, const std::string& property);

    std::vector<UiGraphBlock*> recursiveGatherPlotSinks();

    std::unique_ptr<UiGraphBlock> makeGraphBlock(UiGraphBlock* parent, const gr::property_map& blockData, const std::string& ownerSchedulerUniqueName, const std::string& ownerGraphUniqueName);

    bool blockInTree(const UiGraphBlock& block, const UiGraphBlock& tree, UiGraphPort::Role direction) const;

    std::size_t subscribeToResponses(std::function<void(const gr::Message& incoming)> listener);

    // used by tests
    [[nodiscard]] static DrawableBlockConfig::ControlledProperty firstControlledPropertyConfig(std::span<const BlockPointerPropertyNamePair> controlled);
};

} // namespace gr::od
#endif // include guard
