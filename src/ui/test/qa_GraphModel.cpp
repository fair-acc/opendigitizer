#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <Dashboard.hpp>
#include <GraphModel.hpp>
#include <MapUtils.hpp>
#include <common/ImguiWrap.hpp>
#include <components/ImGuiNotify.hpp>

#include <blocks/Arithmetic.hpp>
#include <blocks/ImPlotSink.hpp>
#include <blocks/SineSource.hpp>
#include <blocks/TestSpectrumGenerator.hpp>

#include <gnuradio-4.0/AtomicRef.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

#include <scope_exit.hpp>

#include <boost/ut.hpp>

#include <array>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace std::string_literals;

opendigitizer::test::TestDashboardRunner g_state;

void reloadSubgraph() { g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }
void reloadGrouping() { g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_grouping.grc", "grouping_test"); }

struct SubgraphBlockTypenameAndDescription {
    std::string_view description;
    std::string_view typeName;
};

constexpr std::array kSubgraphBlockTypenameAndDescriptions{
    SubgraphBlockTypenameAndDescription{"unmanaged subgraph", "gr::Graph"},
    SubgraphBlockTypenameAndDescription{"managed subgraph", "gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>"},
};

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    static bool awaitCondition(ImGuiTestContext* ctx, const std::function<bool()>& condition, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
        const auto start = std::chrono::high_resolution_clock::now();
        while (!condition() && (std::chrono::high_resolution_clock::now() - start < timeout)) {
            ctx->Yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return condition();
    }

    [[nodiscard]] static bool waitForReplyOnEndpoint(ImGuiTestContext* ctx, std::string_view endpoint, std::size_t count = 1UZ) {
        const bool replied = awaitCondition(ctx, [endpoint, count] { //
            return static_cast<std::size_t>(std::ranges::count_if(g_state.collectedMessages, [endpoint](const gr::Message& message) { return message.endpoint == endpoint; })) >= count;
        });
        g_state.clearMessages(); // each wait only sees messages since the last wait
        return replied;
    }

    static UiGraphBlock* rootGraph() {
        auto& rootChildren = g_state.dashboard->graphModel.rootBlock.childBlocks;
        expect(rootChildren.size() == 1UZ) << fatal;
        return rootChildren[0].get();
    }

    static UiGraphBlock* findByName(std::string_view name) { return g_state.dashboard->graphModel.recursiveFindBlockByName(name).block; }

    static std::vector<std::string> takeAllErrorNotifications() {
        const auto result = std::move(ImGui::notifications)                                                                        //
                            | std::views::filter([](const ImGuiToast& toast) { return toast.getType() == ImGuiToastType::Error; }) //
                            | std::views::transform([](const ImGuiToast& toast) { return std::string(toast.getContent()); })       //
                            | std::ranges::to<std::vector>();
        ImGui::notifications.clear();
        return result;
    }

    // equivalent to .graph() on a scheduler or gr::Graph
    static UiGraphBlock* getGraph(UiGraphBlock* subgraph) {
        if (!subgraph->isScheduler()) {
            return subgraph;
        }
        return subgraph->childBlocks.empty() ? nullptr : subgraph->childBlocks.front().get();
    }

    static UiGraphBlock* findSubgraphIn(UiGraphBlock* graph) {
        auto it = std::ranges::find_if(graph->childBlocks, [](const auto& child) { return child->isGraph() || child->isScheduler(); });
        return it == graph->childBlocks.end() ? nullptr : it->get();
    }

    // like FlowgraphEditor::ownersForRoot()
    struct Owners {
        std::string scheduler;
        std::string graph;
    };

    static Owners ownersFor(UiGraphBlock* graphOrScheduler) {
        expect(graphOrScheduler != nullptr) << fatal;
        if (graphOrScheduler->isScheduler()) {
            expect(!graphOrScheduler->childBlocks.empty()) << fatal << "a scheduler block must have its graph loaded";
            return Owners{graphOrScheduler->blockUniqueName, graphOrScheduler->childBlocks.front()->blockUniqueName};
        }
        return Owners{graphOrScheduler->ownerSchedulerUniqueName(), graphOrScheduler->blockUniqueName};
    }

    static Owners rootOwners() { return ownersFor(rootGraph()); }

    static void sendGroupBlocks(const std::vector<std::string>& uniqueNames, const std::string& graphType, const Owners& owners) {
        gr::Tensor<gr::pmt::Value> names(gr::extents_from, {uniqueNames.size()});
        for (std::size_t i = 0; i < uniqueNames.size(); ++i) {
            names[i] = uniqueNames[i];
        }
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kGroupBlocks;
        message.serviceName = owners.scheduler;
        message.data        = gr::property_map{{"type", graphType}, {"uniqueNames", std::move(names)}, {"_targetGraph", owners.graph}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static void sendUngroupBlocks(const std::string& subgraphUniqueName, const Owners& owners) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kUngroupBlocks;
        message.serviceName = owners.scheduler;
        message.data        = gr::property_map{{"uniqueName", subgraphUniqueName}, {"_targetGraph", owners.graph}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static void sendRemoveBlock(const std::string& uniqueName, const Owners& owners) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kRemoveBlock;
        message.serviceName = owners.scheduler;
        message.data        = gr::property_map{{"uniqueName", uniqueName}, {"_targetGraph", owners.graph}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static void sendEmplaceBlock(const std::string& type, const Owners& owners) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kEmplaceBlock;
        message.serviceName = owners.scheduler;
        message.data        = gr::property_map{{"type", type}, {"_targetGraph", owners.graph}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static void sendEmplaceEdge(const std::string& sourceUniqueName, const std::string& sourcePort, const std::string& destinationUniqueName, const std::string& destinationPort, const Owners& owners) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kEmplaceEdge;
        message.serviceName = owners.scheduler;
        message.data        = gr::property_map{                                                                 //
            {"_targetGraph", owners.graph},                                                              //
            {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), sourceUniqueName},           //
            {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), sourcePort},                  //
            {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), destinationUniqueName}, //
            {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), destinationPort},        //
            {std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},        //
            {std::pmr::string(gr::serialization_fields::EDGE_WEIGHT), 1},                                //
            {std::pmr::string(gr::serialization_fields::EDGE_NAME), "edge"s}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static const UiGraphEdge* findEdge(const UiGraphBlock* graph, std::string_view sourceBlockUniqueName, std::string_view destinationBlockUniqueName) {
        auto it = std::ranges::find_if(graph->childEdges, [&](const UiGraphEdge& edge) { //
            return edge.edgeSourceBlockUniqueName == sourceBlockUniqueName && edge.edgeDestinationBlockUniqueName == destinationBlockUniqueName;
        });
        return it == graph->childEdges.end() ? nullptr : std::to_address(it);
    }

    // get unique name given regular name
    static std::string uniqueNameOf(std::string_view name) {
        UiGraphBlock* block = findByName(name);
        expect(block != nullptr) << fatal << std::format("expected a block named {}", name);
        return block->blockUniqueName;
    }

    // does a block with the given name exist, and is it a child of the root graph
    static bool isRootChild(std::string_view name) {
        const auto found = g_state.dashboard->graphModel.recursiveFindBlockByName(name);
        return found.block != nullptr && found.parentGraph == rootGraph();
    }

    // the normal configuration for qa_grouping, which is useful to verify that the graph is in (or has returned to) its normal state
    static void expectEdges_qa_grouping() {
        for (const auto& [from, to] : {std::pair{"source", "middleA"}, {"middleA", "middleB"}, {"middleB", "sink"}}) {
            const UiGraphEdge* edge = findEdge(rootGraph(), uniqueNameOf(from), uniqueNameOf(to));
            expect(edge != nullptr) << std::format("edge {} -> {} should be present in the root graph", from, to);
            expect(edge == nullptr || (edge->edgeSourcePort != nullptr && edge->edgeDestinationPort != nullptr)) << "the edge should resolve its ports";
        }
    }

    using CountingSink = gr::testing::AtomicCountingSink<float>;

    // qa_grouping.grc has a counting sink in it, find the actual BlockModel so we can read the # of samples processed
    static CountingSink* findCountingSink(std::string_view uniqueName) {
        auto blocks     = g_state.dashboard->scheduler->graph().blocks();
        auto findResult = std::ranges::find(blocks, uniqueName, &gr::BlockModel::uniqueName);
        return findResult == std::end(blocks) ? nullptr : static_cast<CountingSink*>((*findResult)->raw());
    }

    static gr::Size_t sampleCount(CountingSink* sink) { return gr::atomic_ref(sink->count.value).load_acquire(); }

    static bool waitForSamples(ImGuiTestContext* ctx, CountingSink* sink) {
        const gr::Size_t baseline = sampleCount(sink);
        return awaitCondition(ctx, [sink, baseline] { return sampleCount(sink) > baseline; });
    }

    /// Returns true if any edges are pointing to blocks that don't exist
    [[nodiscard]] static bool hasInvalidEdges() {
        bool hasInvalid = false;
        g_state.dashboard->graphModel.recursiveForEachBlock([&hasInvalid](const UiGraphModel::FindBlockResult& element) {
            for (const UiGraphEdge& edge : element.block->childEdges) {
                if (edge.edgeSourcePort == nullptr || edge.edgeDestinationPort == nullptr) {
                    hasInvalid = true;
                    return UiGraphModel::VisitorResult::Break;
                }
            }
            return UiGraphModel::VisitorResult::Recurse;
        });
        return hasInvalid;
    }

    static void expectGraphRunningAndConnected(ImGuiTestContext* ctx, CountingSink* sink, std::string_view stage) {
        expect(gr::lifecycle::isActive(g_state.dashboard->scheduler->state())) << std::format("{}: the scheduler should still be active", stage);
        expect(!hasInvalidEdges()) << std::format("{}: every edge should resolve to real ports, unresolved", stage);
        expect(waitForSamples(ctx, sink)) << std::format("{}: samples should keep arriving at the counting sink", stage);
    }

    void registerTests() override { // NOSONAR (cognitive complexity)
        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "property decoding");
            t->TestFunc  = [](ImGuiTestContext*) {
                "tensor property decoding"_test = [] {
                    using Tensor = gr::Tensor<gr::pmt::Value>;
                    const gr::property_map child{{"unique_name", "test-block"}};
                    const gr::property_map data{{"graph", gr::property_map{{"blocks", Tensor(gr::data_from, {gr::pmt::Value(child)})}}}};
                    const auto             children = getOptionalProperty<Tensor>(data, "graph", "blocks");
                    expect(children.has_value()) << fatal;
                    expect(eq(children->size(), 1UZ)) << fatal;
                    const auto block = (*children)[0].get_if<gr::property_map>();
                    expect(block.has_value()) << fatal;
                    expect(eq(getProperty<std::string>(*block, "unique_name"), std::string("test-block")));
                    expect(!getOptionalProperty<Tensor>(gr::property_map{{"blocks", "not a tensor"}}, "blocks"));
                };
                "registry type decoding"_test = [] {
                    UiGraphModel           model;
                    const gr::property_map data{{"types", gr::Tensor<gr::pmt::Value>(gr::data_from, {gr::pmt::Value("opendigitizer::SineSource<float32>")})}};
                    model.handleAvailableGraphBlockTypes(data);
                    expect(eq(model.knownBlockTypes.size(), 1UZ));
                    const auto type = model.knownBlockTypes.find("opendigitizer::SineSource");
                    expect(type != model.knownBlockTypes.end()) << fatal;
                    expect(eq(type->second.size(), 1UZ));
                    expect(type->second.contains("<float32>"));

                    const gr::property_map schedulerData{{"types", gr::Tensor<gr::pmt::Value>(gr::data_from, {gr::pmt::Value("gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>")})}};
                    model.handleAvailableGraphSchedulerTypes(schedulerData);
                    const std::map<std::string, std::set<std::string>> expectedSchedulers{{"gr::Graph", {"<>"}}, {"gr::scheduler::Simple", {"<gr::scheduler::ExecutionPolicy::singleThreaded>"}}};
                    expect(model.knownSchedulerTypes == expectedSchedulers);
                };
            };
        }
        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "flowgraph", "port exporting from subgraphs");
            t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();

            t->GuiFunc = [](ImGuiTestContext*) {
                IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetWindowPos({0, 0});
                ImGui::SetWindowSize(ImVec2(800, 800));
                g_state.dashboard->handleMessages();
            };

            t->TestFunc = [](ImGuiTestContext* ctx) { // NOSONAR test lambda length
                reloadAndWait(ctx, reloadSubgraph);
                Digitizer::utils::scope_exit stopScheduler = [] { g_state.stopScheduler(); };

                UiGraphBlock* rootBlock = g_state.dashboard->graphModel.recursiveFindBlockByName("simpleScheduler").block;
                expect(rootBlock) << fatal;

                // block with outputs
                UiGraphBlock* outputsBlock = g_state.dashboard->graphModel.recursiveFindBlockByName("subgraphSineSource").block;
                expect(outputsBlock) << fatal;

                "returns not-exported for port with no exports"_test = [rootBlock, outputsBlock] {
                    auto* testPort = &outputsBlock->_outputPorts.front();
                    expect(!testPort->isExportedTo(rootBlock)) << "shouldn't be exported";
                };

                "returns not-exported for null ownerBlock"_test = [rootBlock] {
                    DigitizerUi::UiGraphPort orphanPort(nullptr);
                    orphanPort.portName      = "orphan";
                    orphanPort.portDirection = gr::PortDirection::OUTPUT;

                    expect(!orphanPort.isExportedTo(rootBlock));
                };

                "transparent subgraph exposes exported ports after emplace data"_test = [] {
                    DigitizerUi::UiGraphModel model;

                    const auto makePortData        = [](std::string_view portType) { return gr::property_map{{"type", std::string(portType)}}; };
                    const auto makeNormalBlockData = [&](const std::string& uniqueName, gr::property_map inputPorts, gr::property_map outputPorts) {
                        return gr::property_map{
                            {std::pmr::string(gr::serialization_fields::BLOCK_UNIQUE_NAME), uniqueName},
                            {std::pmr::string(gr::serialization_fields::BLOCK_CATEGORY), "NormalBlock"},
                            {"name", uniqueName},
                            {"type_name", "test::Block"},
                            {std::pmr::string(gr::serialization_fields::BLOCK_INPUT_PORTS), std::move(inputPorts)},
                            {std::pmr::string(gr::serialization_fields::BLOCK_OUTPUT_PORTS), std::move(outputPorts)},
                        };
                    };

                    const std::string sourceUniqueName = "source#1";
                    const std::string sinkUniqueName   = "sink#1";

                    const auto subgraphData = gr::property_map{
                        {std::pmr::string(gr::serialization_fields::BLOCK_UNIQUE_NAME), "subgraph#1"},
                        {std::pmr::string(gr::serialization_fields::BLOCK_CATEGORY), "TransparentBlockGroup"},
                        {"name", "subgraph"},
                        {"type_name", "SUBGRAPH"},
                        {std::pmr::string(gr::serialization_fields::BLOCK_META_INFORMATION),
                            gr::property_map{
                                {"exportedInputPorts", gr::property_map{{sinkUniqueName, gr::property_map{{"in", gr::property_map{{"exportedName", "exposedIn"}}}}}}},
                                {"exportedOutputPorts", gr::property_map{{sourceUniqueName, gr::property_map{{"out", gr::property_map{{"exportedName", "exposedOut"}}}}}}},
                            }},
                        {"children",
                            gr::property_map{
                                {sourceUniqueName, makeNormalBlockData(sourceUniqueName, {}, gr::property_map{{"out", makePortData("float32")}})},
                                {sinkUniqueName, makeNormalBlockData(sinkUniqueName, gr::property_map{{"in", makePortData("float32")}}, {})},
                            }},
                    };

                    auto subgraph = model.makeGraphBlock(&model.rootBlock, subgraphData, "scheduler#1", "parentGraph#1");

                    expect(subgraph->inputPorts().size() == 1UZ);
                    expect(subgraph->outputPorts().size() == 1UZ);
                    expect(subgraph->inputPorts().front().portName == std::string("exposedIn"));
                    expect(subgraph->outputPorts().front().portName == std::string("exposedOut"));
                    expect(subgraph->inputPorts().front().portType == std::string("float32"));
                    expect(subgraph->outputPorts().front().portType == std::string("float32"));

                    UiGraphBlock* source = subgraph->findBlockByUniqueName(sourceUniqueName);
                    UiGraphBlock* sink   = subgraph->findBlockByUniqueName(sinkUniqueName);
                    expect(source && sink) << fatal;
                    expect(sink->_inputPorts.front().getExportedName(subgraph.get()) == std::string("exposedIn"));
                    expect(source->_outputPorts.front().getExportedName(subgraph.get()) == std::string("exposedOut"));
                    expect(sink->_inputPorts.front().isExportedTo(subgraph.get()));
                    expect(!source->_outputPorts.front().isExportedTo(nullptr));
                };

                g_state.stopScheduler();
            };
        }

        registerMessageTest("export returns kSubgraphExportedPort and model gets updated accordingly", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadSubgraph);

            UiGraphBlock* subgraph    = findByName("simpleScheduler");
            UiGraphBlock* innerSource = findByName("subgraphSineSource");
            expect(subgraph != nullptr && innerSource != nullptr) << fatal;

            sendExportPort(subgraph->blockUniqueName, innerSource->blockUniqueName, "output", "out", "sigOut");
            expect(waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "scheduler never replied about the port export";

            const auto subgraphHasExportedPort = [uniqueName = subgraph->blockUniqueName] {
                UiGraphBlock* block = g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(uniqueName).block;
                return block && std::ranges::count(block->outputPorts(), "sigOut", &DigitizerUi::UiGraphPort::portName) == 1;
            };
            expect(awaitCondition(ctx, subgraphHasExportedPort)) << "exported port did not show up on the subgraph block";

            g_state.stopScheduler();
        });

        registerMessageTest("connect to an exported port", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadSubgraph);

            UiGraphBlock* subgraph    = findByName("simpleScheduler");
            UiGraphBlock* innerSource = findByName("subgraphSineSource");
            expect(subgraph != nullptr && innerSource != nullptr) << fatal;
            const auto& subgraphUniqueName = subgraph->blockUniqueName;

            sendExportPort(subgraph->blockUniqueName, innerSource->blockUniqueName, "output", "out", "sigOut");
            expect(waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "scheduler never replied about the port export";
            expect(awaitCondition(ctx,
                [subgraphUniqueName] {
                    UiGraphBlock* block = g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(subgraphUniqueName).block;
                    return block && !block->outputPorts().empty();
                }))
                << fatal << "exported port did not show up on the subgraph block";

            sendEmplaceBlock("gr::basic::DataSink<float32>", rootOwners());

            const auto findOuterSink = [subgraphUniqueName] {
                auto& children = rootGraph()->childBlocks;
                auto  it       = std::ranges::find_if(children, [&](const auto& child) { //
                    return child->blockUniqueName != subgraphUniqueName && child->blockTypeName.starts_with("gr::basic::DataSink");
                });
                return it == children.end() ? nullptr : it->get();
            };
            expect(awaitCondition(ctx, [&] { return findOuterSink() != nullptr; })) << fatal << "emplaced sink did not show up in the root graph";
            const auto& outerSinkUniqueName = findOuterSink()->blockUniqueName;

            sendEmplaceEdge(subgraphUniqueName, "sigOut", outerSinkUniqueName, "in", rootOwners());

            expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kEdgeEmplaced)) << "edge should be reported as emplaced by the scheduler";

            expect(awaitCondition(ctx, [&] {
                const UiGraphEdge* edge = findEdge(rootGraph(), subgraphUniqueName, outerSinkUniqueName);
                return edge && edge->edgeSourcePort && edge->edgeDestinationPort && edge->edgeSourcePort->portName == "sigOut";
            })) << "edge did not get created to exported port in our graph model";

            g_state.stopScheduler();
        });

        registerGroupingTest("group middle blocks of a chain", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);
            CountingSink* sink = findCountingSink(uniqueNameOf("sink"));
            expect(sink != nullptr) << fatal << "qa_grouping.grc should end in an AtomicCountingSink";

            ImGui::notifications.clear();

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("middleA"), uniqueNameOf("middleB")});
            expect(subgraph != nullptr) << fatal;
            expect(isManagedVariant(ctx) ? subgraph->isScheduler() : subgraph->isGraph()) << fatal << "grouping did not create the expected type of block";
            expect(awaitCondition(ctx, [] { return rootGraph()->childEdges.size() == 2UZ; })) << fatal << "boundary edges did not show up in the root graph";

            UiGraphBlock* interior = getGraph(subgraph);
            expect(interior != nullptr) << fatal;
            UiGraphBlock* innerA = interior->findBlockByUniqueName(uniqueNameOf("middleA"));
            UiGraphBlock* innerB = interior->findBlockByUniqueName(uniqueNameOf("middleB"));
            expect(innerA != nullptr && innerB != nullptr) << fatal;

            const UiGraphEdge* interiorEdge = findEdge(interior, uniqueNameOf("middleA"), uniqueNameOf("middleB"));
            expect(interiorEdge != nullptr) << fatal << "interior edge middleA->middleB should stay inside the graph";
            expect(interiorEdge->edgeSourcePort != nullptr && interiorEdge->edgeDestinationPort != nullptr);
            expect(eq(interior->childEdges.size(), 1UZ));

            expect(subgraph->inputPorts().size() == 1UZ) << fatal << "middleA's input should be the only exported input";
            expect(subgraph->outputPorts().size() == 1UZ) << fatal << "middleB's output should be the only exported output";
            expect(subgraph->inputPorts().front().portName.starts_with("middleA.")) << "exported input should be named after middleA";
            expect(subgraph->outputPorts().front().portName.starts_with("middleB.")) << "exported output should be named after middleB";
            expect(innerA->_inputPorts.front().getExportedName(subgraph) == subgraph->inputPorts().front().portName);
            expect(innerB->_outputPorts.front().getExportedName(subgraph) == subgraph->outputPorts().front().portName);

            const UiGraphEdge* inEdge = findEdge(rootGraph(), uniqueNameOf("source"), subgraph->blockUniqueName);
            expect(inEdge != nullptr) << fatal << "source should be connected to the subgraph";
            expect(inEdge->edgeDestinationPort != nullptr && inEdge->edgeDestinationPort->ownerBlock == subgraph);
            expect(inEdge->edgeDestinationPort->portName == subgraph->inputPorts().front().portName);

            const UiGraphEdge* outEdge = findEdge(rootGraph(), subgraph->blockUniqueName, uniqueNameOf("sink"));
            expect(outEdge != nullptr) << fatal << "subgraph should be connected to the sink";
            expect(outEdge->edgeSourcePort != nullptr && outEdge->edgeSourcePort->ownerBlock == subgraph);
            expect(outEdge->edgeSourcePort->portName == subgraph->outputPorts().front().portName);

            expectGraphRunningAndConnected(ctx, sink, "after grouping the middle of the chain");

            const auto errors = takeAllErrorNotifications();
            expect(errors.empty()) << std::format("grouping reported errors: {}", errors);

            g_state.stopScheduler();
        });

        registerGroupingTest("group and ungroup a single block", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);
            CountingSink* sink = findCountingSink(uniqueNameOf("sink"));
            expect(sink != nullptr) << fatal;

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("middleA")});
            expect(subgraph != nullptr) << fatal;
            const std::string subgraphUniqueName = subgraph->blockUniqueName;

            UiGraphBlock* interior = getGraph(subgraph);
            expect(interior != nullptr) << fatal;
            expect(eq(interior->childBlocks.size(), 1UZ)) << "only middleA should have moved into the subgraph";
            expect(interior->childEdges.empty()) << "a single grouped block cannot have interior edges";

            expect(awaitCondition(ctx, [] { return rootGraph()->childEdges.size() == 3UZ; })) << fatal << "the chain should still have three edges, now routed through the subgraph";
            expect(subgraph->inputPorts().size() == 1UZ) << fatal << "middleA's input should be exported";
            expect(subgraph->outputPorts().size() == 1UZ) << fatal << "middleA's output should be exported";
            expect(findEdge(rootGraph(), uniqueNameOf("source"), subgraphUniqueName) != nullptr) << "source should feed the subgraph";
            expect(findEdge(rootGraph(), subgraphUniqueName, uniqueNameOf("middleB")) != nullptr) << "the subgraph should feed middleB";
            expect(!isRootChild("middleA")) << "middleA should no longer be a direct child of the root graph";

            expectGraphRunningAndConnected(ctx, sink, "after grouping a single block");

            ungroupAndWait(ctx, subgraphUniqueName);

            expect(awaitCondition(ctx, [] { return isRootChild("middleA") && rootGraph()->childEdges.size() == 3UZ; })) << fatal << "middleA and the original chain should be restored";
            expectEdges_qa_grouping();
            expectGraphRunningAndConnected(ctx, sink, "after ungrouping a single block");

            g_state.stopScheduler();
        });

        registerGroupingTest("group and ungroup unconnected blocks", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);
            CountingSink* sink = findCountingSink(uniqueNameOf("sink"));
            expect(sink != nullptr) << fatal;

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("loner1"), uniqueNameOf("loner2")});
            expect(subgraph != nullptr) << fatal;
            const std::string subgraphUniqueName = subgraph->blockUniqueName;

            expect(isManagedVariant(ctx) ? subgraph->isScheduler() : subgraph->isGraph()) << "grouping did not create the expected type of block";
            expect(subgraph->inputPorts().empty()) << "no connections + nothing to export";
            expect(subgraph->outputPorts().empty()) << "no connections + nothing to export";
            expect(subgraph->exportedInputPorts.empty());
            expect(subgraph->exportedOutputPorts.empty());

            // blocks and edges live in the graph, which for a managed subgraph is the scheduler's only child
            UiGraphBlock* interior = getGraph(subgraph);
            expect(interior != nullptr) << fatal;
            expect(interior->childEdges.empty());
            expect(interior->findBlockByUniqueName(uniqueNameOf("loner1")) != nullptr);
            expect(interior->findBlockByUniqueName(uniqueNameOf("loner2")) != nullptr);
            expect(!isRootChild("loner1")) << "grouped block should no longer be a direct child of the root graph";
            expect(eq(rootGraph()->childEdges.size(), 3UZ)) << "other edges should still be intact after grouping";
            expectGraphRunningAndConnected(ctx, sink, "after grouping unconnected blocks");

            ungroupAndWait(ctx, subgraphUniqueName);

            expect(awaitCondition(ctx, [] { return isRootChild("loner1") && isRootChild("loner2"); })) << "ungrouped blocks should return to the root graph";
            expectEdges_qa_grouping();
            expectGraphRunningAndConnected(ctx, sink, "after ungrouping unconnected blocks");

            g_state.stopScheduler();
        });

        registerGroupingTest("ungroup restores boundary connections", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);
            CountingSink* sink = findCountingSink(uniqueNameOf("sink"));
            expect(sink != nullptr) << fatal;

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("middleA"), uniqueNameOf("middleB")});
            expect(subgraph != nullptr) << fatal;

            ungroupAndWait(ctx, subgraph->blockUniqueName);

            expect(awaitCondition(ctx, [] { return isRootChild("middleA") && rootGraph()->childEdges.size() == 3UZ; })) << fatal << "chain was not restored in the root graph";
            expectEdges_qa_grouping();
            expectGraphRunningAndConnected(ctx, sink, "after ungrouping");

            g_state.stopScheduler();
        });

        registerGroupingTest("group blocks inside a subgraph", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);
            CountingSink* sink = findCountingSink(uniqueNameOf("sink"));
            expect(sink != nullptr) << fatal;

            // group last three, source -> (middleA -> middleB -> sink)
            UiGraphBlock* outer = groupAndWait(ctx, {uniqueNameOf("middleA"), uniqueNameOf("middleB"), uniqueNameOf("sink")});
            expect(outer != nullptr) << fatal;
            const std::string outerUniqueName = outer->blockUniqueName;

            UiGraphBlock* outerInterior = getGraph(outer);
            expect(outerInterior != nullptr) << fatal;
            const std::string outerInteriorUniqueName = outerInterior->blockUniqueName;
            expect(eq(outerInterior->childBlocks.size(), 3UZ));
            expect(eq(outerInterior->childEdges.size(), 2UZ)) << "middleA->middleB and middleB->sink should stay inside";
            expect(eq(outer->inputPorts().size(), 1UZ)) << fatal << "only middleA's input goes between graphs";
            expect(outer->outputPorts().empty()) << "there should be no output from the outer subgraph. the sink is grouped";
            expect(eq(rootGraph()->childEdges.size(), 1UZ));
            expect(findEdge(rootGraph(), uniqueNameOf("source"), outerUniqueName) != nullptr) << fatal << "there should be an edge between the source block and the outer subgraph";
            expectGraphRunningAndConnected(ctx, sink, "after making a group");

            // now group two of the blocks that are already inside that subgraph
            UiGraphBlock* inner = groupAndWait(ctx, {uniqueNameOf("middleB"), uniqueNameOf("sink")}, outerInterior);
            expect(inner != nullptr) << fatal;
            const std::string innerUniqueName = inner->blockUniqueName;

            UiGraphBlock* innerInterior = getGraph(inner);
            expect(innerInterior != nullptr) << fatal;
            expect(eq(innerInterior->childBlocks.size(), 2UZ));
            expect(eq(innerInterior->childEdges.size(), 1UZ)) << "middleB->sink should stay inside the inner subgraph";
            expect(eq(inner->inputPorts().size(), 1UZ)) << fatal << "only middleB's input goes between the inner subgraph and outer subgraph";
            expect(inner->outputPorts().empty());

            UiGraphBlock* reOuterInterior = g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(outerInteriorUniqueName).block;
            expect(reOuterInterior != nullptr) << fatal;
            expect(eq(reOuterInterior->childBlocks.size(), 2UZ)) << "middleA and the new inner subgraph";
            expect(eq(reOuterInterior->childEdges.size(), 1UZ));
            expect(findEdge(reOuterInterior, uniqueNameOf("middleA"), innerUniqueName) != nullptr) << "middleA should now go into a port of the innermost subgraph";
            expect(eq(rootGraph()->childEdges.size(), 1UZ)) << "the root graph looks the same";
            expectGraphRunningAndConnected(ctx, sink, "after making a nested group");

            g_state.stopScheduler();
        });

        registerGroupingTest("exported ports are removed when unexporting a port in a subgraph", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("middleA")});
            expect(subgraph != nullptr) << fatal;
            const std::string subgraphUniqueName = subgraph->blockUniqueName;
            expect(eq(subgraph->inputPorts().size(), 1UZ)) << fatal;
            expect(eq(subgraph->outputPorts().size(), 1UZ)) << fatal;
            expect(findEdge(rootGraph(), uniqueNameOf("source"), subgraphUniqueName) != nullptr) << fatal;

            // unexporting takes the internal port name, see FlowgraphEditor::drawPortsMenu
            sendExportPort(subgraphUniqueName, uniqueNameOf("middleA"), "input", "in", "", /*exportFlag*/ false);
            expect(waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "scheduler did not confirm unexporting";

            const auto findSubgraphBlock = [subgraphUniqueName] { return g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(subgraphUniqueName).block; };
            expect(awaitCondition(ctx,
                [&findSubgraphBlock] {
                    UiGraphBlock* block = findSubgraphBlock();
                    return block && block->inputPorts().empty();
                }))
                << fatal << "the unexported input should be removed from the subgraph block";

            UiGraphBlock* unexported = findSubgraphBlock();
            expect(unexported != nullptr) << fatal;
            expect(eq(unexported->outputPorts().size(), 1UZ)) << "the output export should be the same";
            expect(unexported->exportedInputPorts.empty()) << "the input export mapping should be gone";
            expect(findEdge(rootGraph(), uniqueNameOf("source"), subgraphUniqueName) == nullptr) << "the edge into the unexported port should be gone";
            expect(findEdge(rootGraph(), subgraphUniqueName, uniqueNameOf("middleB")) != nullptr) << "the other edge what connects to an exported port should not have been destroyed/removed";
            expect(!hasInvalidEdges()) << "no edge should be left pointing at a port that no longer exists";
            expect(gr::lifecycle::isActive(g_state.dashboard->scheduler->state())) << "unexporting a port should not stop/pause/error the scheduler";

            g_state.stopScheduler();
        });

        registerMessageTest("removing the selected block clears the selection", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* loner1 = findByName("loner1");
            expect(loner1 != nullptr) << fatal;

            g_state.dashboard->graphModel.selectedBlock = loner1;

            sendRemoveBlock(loner1->blockUniqueName, rootOwners());
            expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlockRemoved)) << "scheduler did not confirm block removal";
            expect(awaitCondition(ctx, [] { return findByName("loner1") == nullptr; })) << fatal << "removed block should disappear from the graph model";

            expect(g_state.dashboard->graphModel.selectedBlock == nullptr) << "selection should be cleared when the selected block is deleted";
            g_state.stopScheduler();
        });

        registerGroupingTest("removing a subgraph clears the selection of an inner block", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);

            const auto groupAndRemoveWithSelection = [ctx](bool selectInnerBlock) {
                UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("loner1"), uniqueNameOf("loner2")});
                expect(subgraph != nullptr) << fatal;
                const std::string subgraphUniqueName = subgraph->blockUniqueName;

                UiGraphBlock* innerBlock = findByName("loner1");
                expect(innerBlock != nullptr) << fatal << "the grouped block should still exist, one level deeper";
                g_state.dashboard->graphModel.selectedBlock = selectInnerBlock ? innerBlock : subgraph;

                sendRemoveBlock(subgraphUniqueName, rootOwners());
                expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlockRemoved)) << "scheduler did not confirm subgraph removal";
                expect(awaitCondition(ctx, [subgraphUniqueName] { return rootGraph()->findBlockByUniqueName(subgraphUniqueName) == nullptr; })) << fatal << "removed subgraph should disappear from the graph model";

                expect(g_state.dashboard->graphModel.selectedBlock == nullptr) << (selectInnerBlock ? "selection should be cleared when a block nested inside the deleted subgraph is selected" : "selection should be cleared when the deleted subgraph itself is selected");
            };

            groupAndRemoveWithSelection(/*selectInnerBlock*/ false);

            reloadAndWait(ctx, reloadGrouping);
            groupAndRemoveWithSelection(/*selectInnerBlock*/ true);

            g_state.stopScheduler();
        });

        registerMessageTest("reinspection not reporting the selected block clears the selection", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* loner1 = findByName("loner1");
            expect(loner1 != nullptr) << fatal;
            g_state.dashboard->graphModel.selectedBlock = loner1;

            // simulate an inspection reply which no longer reports any of the child blocks
            rootGraph()->setGraphChildren(gr::property_map{});
            expect(rootGraph()->childBlocks.empty()) << fatal;

            expect(g_state.dashboard->graphModel.selectedBlock == nullptr) << "selection should be cleared when the selected block is deleted";
            g_state.stopScheduler();
        });

        registerGroupingTest("ungrouping clears the selection of an inner block", [](ImGuiTestContext* ctx) { // NOSONAR (cognitive complexity)
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* subgraph = groupAndWait(ctx, {uniqueNameOf("loner1"), uniqueNameOf("loner2")});
            expect(subgraph != nullptr) << fatal;

            g_state.dashboard->graphModel.selectedBlock = subgraph;

            ungroupAndWait(ctx, subgraph->blockUniqueName);
            expect(awaitCondition(ctx, [] { return isRootChild("loner1") && isRootChild("loner2"); })) << fatal << "ungrouped blocks should return to the root graph";

            expect(g_state.dashboard->graphModel.selectedBlock == nullptr) << "selection should be cleared when the selected block is deleted";
            g_state.stopScheduler();
        });
    }

    static void reloadAndWait(ImGuiTestContext* ctx, const std::function<void()>& reloadFunction) {
        reloadFunction();
        g_state.waitForScheduler(ctx);
        while (!g_state.hasBlocks()) {
            ctx->Yield();
        }
    }

    static void sendExportPort(const std::string& subgraphUniqueName, const std::string& innerBlockUniqueName, const std::string& direction, const std::string& portName, const std::string& exportedName, bool exportFlag = true) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::graph::property::kSubgraphExportPort;
        message.serviceName = subgraphUniqueName;
        message.data        = gr::property_map{{"uniqueBlockName", innerBlockUniqueName}, {"portDirection", direction}, {"portName", portName}, {"exportedName", exportedName}, {"exportFlag", exportFlag}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static std::string graphTypeFor(ImGuiTestContext* ctx) { return std::string(kSubgraphBlockTypenameAndDescriptions[static_cast<std::size_t>(ctx->Test->ArgVariant)].typeName); }

    static bool isManagedVariant(ImGuiTestContext* ctx) { return !graphTypeFor(ctx).starts_with("gr::Graph"); }

    // group some blocks and return the ui graphmodel representation of the resulting subgraph. which type of scheduler/graph is determined by the test params
    static UiGraphBlock* groupAndWait(ImGuiTestContext* ctx, const std::vector<std::string>& uniqueNames, UiGraphBlock* parentGraph = nullptr) {
        const std::string parentUniqueName = (parentGraph ? parentGraph : rootGraph())->blockUniqueName;
        const auto        reFindParent     = [parentUniqueName] { return g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(parentUniqueName).block; };

        sendGroupBlocks(uniqueNames, graphTypeFor(ctx), ownersFor(reFindParent()));
        expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlocksGrouped)) << "scheduler should confirm grouping";

        const bool grouped = awaitCondition(ctx, [&] {
            UiGraphBlock* parent   = reFindParent();
            UiGraphBlock* subgraph = parent ? findSubgraphIn(parent) : nullptr;
            UiGraphBlock* interior = subgraph ? getGraph(subgraph) : nullptr;
            if (!interior || interior->childBlocks.size() != uniqueNames.size()) {
                return false;
            }
            return std::ranges::all_of(uniqueNames, [interior](const std::string& name) { return interior->findBlockByUniqueName(name) != nullptr; });
        });
        expect(grouped) << "subgraph with grouped blocks should also appear in the graph model";
        return grouped ? findSubgraphIn(reFindParent()) : nullptr;
    }

    static void ungroupAndWait(ImGuiTestContext* ctx, const std::string& subgraphUniqueName, UiGraphBlock* parentGraph = nullptr) {
        const std::string parentUniqueName = (parentGraph ? parentGraph : rootGraph())->blockUniqueName;
        const auto        reFindParent     = [parentUniqueName] { return g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(parentUniqueName).block; };

        sendUngroupBlocks(subgraphUniqueName, ownersFor(reFindParent()));
        expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlocksUngrouped)) << "scheduler did not confirm ungrouping";
        expect(awaitCondition(ctx,
            [&] {
                UiGraphBlock* parent = reFindParent();
                return parent && parent->findBlockByUniqueName(subgraphUniqueName) == nullptr;
            }))
            << fatal << "the graph block from an ungrouped subgraph should no longer exist";
    }

    static void messageTestGuiFunc(ImGuiTestContext*) {
        IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowPos({0, 0});
        ImGui::SetWindowSize(ImVec2(800, 800));
        g_state.dashboard->handleMessages();
    }

    void registerMessageTest(const char* name, ImGuiTestTestFunc testFunc) {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "graphmodel", name);
        t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();
        t->GuiFunc  = &messageTestGuiFunc;
        t->TestFunc = testFunc;
    }

    /// run test foreach item in kSubgraphBlockTypenameAndDescriptions
    void registerGroupingTest(const char* name, ImGuiTestTestFunc testFunc) {
        for (std::size_t variant = 0UZ; variant < kSubgraphBlockTypenameAndDescriptions.size(); ++variant) {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "graphmodel", name);
            t->SetOwnedName(std::format("{}, {}", name, kSubgraphBlockTypenameAndDescriptions[variant].description).c_str());
            t->ArgVariant = static_cast<int>(variant);
            t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();
            t->GuiFunc  = &messageTestGuiFunc;
            t->TestFunc = testFunc;
        }
    }
};

namespace {
template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<opendigitizer::Arithmetic, float>(registry);
    gr::registerBlock<opendigitizer::SineSource, float>(registry);
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
    // TODO: fix gnuradio having this keyed under CountingSinkImpl
    gr::registerBlock<"gr::testing::AtomicCountingSink", gr::testing::AtomicCountingSink, float>(registry);

    std::print("Available blocks:\n");
    for (auto& blockName : registry.keys()) {
        std::print("  - {}\n", blockName);
    }
#pragma GCC diagnostic pop
}
} // namespace

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "GraphModel";

    // This is not a globalBlockRegistry, but a copy of it
    gr::BlockRegistry&     registry          = gr::globalBlockRegistry();
    gr::SchedulerRegistry& schedulerRegistry = gr::globalSchedulerRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    // qa_subgraph.grc uses the singlethreaded simple scheduler
    schedulerRegistry.insert<gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded>>();

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    g_state.reload(cmrc::sample_dashboards::get_filesystem(), "assets/sampleDashboards/DemoDashboard.grc");

    auto result = app.runTests();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
