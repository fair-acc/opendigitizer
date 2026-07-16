#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <Dashboard.hpp>
#include <GraphModel.hpp>
#include <common/ImguiWrap.hpp>

#include <blocks/Arithmetic.hpp>
#include <blocks/ImPlotSink.hpp>
#include <blocks/SineSource.hpp>
#include <blocks/TestSpectrumGenerator.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <scope_exit.hpp>

#include <boost/ut.hpp>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace std::string_literals;

opendigitizer::test::TestDashboardRunner g_state;

void reloadSubgraph() { g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }
void reloadGrouping() { g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_grouping.grc", "grouping_test"); }

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

    [[nodiscard]] static bool waitForReplyOnEndpoint(ImGuiTestContext* ctx, std::string_view endpoint) {
        std::optional<gr::Message>   outReply;
        auto                         subscription = g_state.dashboard->graphModel.subscribeToResponses([&outReply, endpoint](const gr::Message& reply) {
            if (reply.endpoint == endpoint) {
                outReply = reply;
            }
        });
        Digitizer::utils::scope_exit unsubscribe  = [subscription] { g_state.dashboard->graphModel.unsubscribeFromResponses(subscription); };

        return awaitCondition(ctx, [&outReply] { return outReply.has_value(); });
    }

    static UiGraphBlock* rootGraph() {
        auto& rootChildren = g_state.dashboard->graphModel.rootBlock.childBlocks;
        expect(rootChildren.size() == 1UZ) << fatal;
        return rootChildren[0].get();
    }

    static UiGraphBlock* findByName(std::string_view name) { return g_state.dashboard->graphModel.recursiveFindBlockByName(name).block; }

    static UiGraphBlock* findSubgraphIn(UiGraphBlock* graph) {
        auto it = std::ranges::find_if(graph->childBlocks, [](const auto& child) { return child->isGraph() || child->isScheduler(); });
        return it == graph->childBlocks.end() ? nullptr : it->get();
    }

    static void sendGroupBlocks(std::vector<std::string> uniqueNames) {
        auto&                      graphModel = g_state.dashboard->graphModel;
        gr::Tensor<gr::pmt::Value> names;
        for (auto& name : uniqueNames) {
            names.push_back(gr::pmt::Value(std::move(name)));
        }
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kGroupBlocks;
        message.serviceName = graphModel.rootBlock.blockUniqueName;
        message.data        = gr::property_map{{"type", "gr::Graph"s}, {"uniqueNames", std::move(names)}, {"_targetGraph", rootGraph()->blockUniqueName}};
        graphModel.sendMessage(std::move(message));
    }

    static void sendUngroupBlocks(const std::string& subgraphUniqueName) {
        auto&       graphModel = g_state.dashboard->graphModel;
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::scheduler::property::kUngroupBlocks;
        message.serviceName = graphModel.rootBlock.blockUniqueName;
        message.data        = gr::property_map{{"uniqueName", subgraphUniqueName}, {"_targetGraph", rootGraph()->blockUniqueName}};
        graphModel.sendMessage(std::move(message));
    }

    static const UiGraphEdge* findEdge(const UiGraphBlock* graph, std::string_view sourceBlockUniqueName, std::string_view destinationBlockUniqueName) {
        auto it = std::ranges::find_if(graph->childEdges, [&](const UiGraphEdge& edge) { //
            return edge.edgeSourceBlockName == sourceBlockUniqueName && edge.edgeDestinationBlockName == destinationBlockUniqueName;
        });
        return it == graph->childEdges.end() ? nullptr : std::addressof(*it);
    }

    void registerTests() override {
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
                reloadSubgraph();
                g_state.waitForScheduler(ctx);
                while (!g_state.hasBlocks()) {
                    ctx->Yield();
                }

                UiGraphBlock* rootBlock = g_state.dashboard->graphModel.recursiveFindBlockByName("simpleScheduler").block;
                expect(rootBlock) << fatal;

                // block with inputs
                UiGraphBlock* inputsBlock = g_state.dashboard->graphModel.recursiveFindBlockByName("gr::basic::DataSink<float32>").block;
                expect(inputsBlock) << fatal;
                // block with outputs
                UiGraphBlock* outputsBlock = g_state.dashboard->graphModel.recursiveFindBlockByName("subgraphSineSource").block;
                expect(outputsBlock) << fatal;

                "returns not-exported for port with no exports"_test = [rootBlock, outputsBlock] {
                    auto* testPort = &outputsBlock->_outputPorts.front();
                    expect(!testPort->isExportedTo(rootBlock)) << "shouldn't be exported";
                };

                "handlePortExported adds to exportedOutputPorts"_test = [rootBlock, outputsBlock] {
                    gr::property_map data{
                        {"portDirection", "output"},
                        {"uniqueBlockName", outputsBlock->blockUniqueName},
                        {"portName", outputsBlock->_outputPorts.front().portName},
                        {"exportedName", "myOut"},
                        {"exportFlag", true},
                    };
                    rootBlock->handlePortExported(data);

                    expect(rootBlock->exportedOutputPorts.contains(outputsBlock->blockUniqueName));
                    auto& portSet = rootBlock->exportedOutputPorts[outputsBlock->blockUniqueName];
                    auto  it      = std::ranges::find_if(portSet, [](const auto& pm) { return pm.internalName == "out"; });
                    expect(it != portSet.end());
                    expect(it->exportedName == std::string("myOut"));

                    data["exportFlag"] = false;
                    rootBlock->handlePortExported(data);
                    expect(rootBlock->exportedOutputPorts[outputsBlock->blockUniqueName].empty()) << "exportFlag: false should un-export";
                };

                "export adds to exportedInputPorts"_test = [rootBlock, inputsBlock] {
                    gr::property_map data{
                        {"portDirection", "input"},
                        {"uniqueBlockName", inputsBlock->blockUniqueName},
                        {"portName", inputsBlock->_inputPorts.front().portName},
                        {"exportedName", "exposedIn"},
                        {"exportFlag", true},
                    };
                    rootBlock->handlePortExported(data);

                    expect(rootBlock->exportedInputPorts.contains(inputsBlock->blockUniqueName));
                    expect(!rootBlock->exportedInputPorts[inputsBlock->blockUniqueName].empty());

                    data["exportFlag"] = false;
                    rootBlock->handlePortExported(data);
                    expect(rootBlock->exportedInputPorts[inputsBlock->blockUniqueName].empty());
                };

                "returns exported info after handlePortExported"_test = [rootBlock, targetBlock = outputsBlock] {
                    DigitizerUi::UiGraphPort* testPort              = &targetBlock->_outputPorts.front();
                    std::string               targetBlockUniqueName = targetBlock->blockUniqueName;

                    gr::property_map exportData{
                        {"portDirection", "output"},
                        {"uniqueBlockName", targetBlockUniqueName},
                        {"portName", testPort->portName},
                        {"exportedName", "myExportedPort"},
                        {"exportFlag", true},
                    };
                    rootBlock->handlePortExported(exportData);

                    expect(testPort->getExportedName(rootBlock).has_value()) << "port should be exported";
                    expect(testPort->getExportedName(rootBlock) == std::string("myExportedPort")) << "exported port should have the correct name";

                    exportData["exportFlag"] = false;
                    rootBlock->handlePortExported(exportData);

                    expect(!testPort->isExportedTo(rootBlock)) << "port should no longer be exported";
                };

                "returns not-exported for null ownerBlock"_test = [rootBlock] {
                    DigitizerUi::UiGraphPort orphanPort(nullptr);
                    orphanPort.portName      = "orphan";
                    orphanPort.portDirection = gr::PortDirection::OUTPUT;

                    expect(!orphanPort.isExportedTo(rootBlock));
                };

                g_state.stopScheduler();
            };
        }

        registerMessageTest("export returns kSubgraphExportedPort and model gets updated accordingly", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadSubgraph);

            UiGraphBlock* subgraph    = findByName("simpleScheduler");
            UiGraphBlock* innerSource = findByName("subgraphSineSource");
            expect(subgraph != nullptr && innerSource != nullptr) << fatal;

            sendExportPort(subgraph, innerSource, "output", "out", "sigOut");
            expect(waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "scheduler never replied about the port export";

            const auto subgraphHasExportedPort = [uniqueName = subgraph->blockUniqueName] {
                UiGraphBlock* block = g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(uniqueName).block;
                return block && std::ranges::count(block->outputPorts(), "sigOut", &DigitizerUi::UiGraphPort::portName) == 1;
            };
            expect(awaitCondition(ctx, subgraphHasExportedPort)) << "exported port did not show up on the subgraph block";

            g_state.stopScheduler();
        });

        registerMessageTest("connect to an exported port", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadSubgraph);

            UiGraphBlock* subgraph    = findByName("simpleScheduler");
            UiGraphBlock* innerSource = findByName("subgraphSineSource");
            expect(subgraph != nullptr && innerSource != nullptr) << fatal;
            const auto subgraphUniqueName = subgraph->blockUniqueName;

            sendExportPort(subgraph, innerSource, "output", "out", "sigOut");
            expect(waitForReplyOnEndpoint(ctx, gr::graph::property::kSubgraphExportedPort)) << "scheduler never replied about the port export";
            expect(awaitCondition(ctx,
                [subgraphUniqueName] {
                    UiGraphBlock* block = g_state.dashboard->graphModel.recursiveFindBlockByUniqueName(subgraphUniqueName).block;
                    return block && !block->outputPorts().empty();
                }))
                << fatal << "exported port did not show up on the subgraph block";

            auto& graphModel = g_state.dashboard->graphModel;
            {
                gr::Message message;
                message.cmd         = gr::message::Command::Set;
                message.endpoint    = gr::scheduler::property::kEmplaceBlock;
                message.serviceName = graphModel.rootBlock.blockUniqueName;
                message.data        = gr::property_map{{"type", "gr::basic::DataSink<float32>"s}, {"_targetGraph", rootGraph()->blockUniqueName}};
                graphModel.sendMessage(std::move(message));
            }

            const auto findOuterSink = [subgraphUniqueName]() -> UiGraphBlock* {
                auto& children = rootGraph()->childBlocks;
                auto  it       = std::ranges::find_if(children, [&](const auto& child) { //
                    return child->blockUniqueName != subgraphUniqueName && child->blockTypeName.starts_with("gr::basic::DataSink");
                });
                return it == children.end() ? nullptr : it->get();
            };
            expect(awaitCondition(ctx, [&] { return findOuterSink() != nullptr; })) << fatal << "emplaced sink did not show up in the root graph";
            const auto outerSinkUniqueName = findOuterSink()->blockUniqueName;

            {
                gr::Message message;
                message.cmd         = gr::message::Command::Set;
                message.endpoint    = gr::scheduler::property::kEmplaceEdge;
                message.serviceName = graphModel.rootBlock.blockUniqueName;
                message.data        = gr::property_map{                                                               //
                    {"_targetGraph", rootGraph()->blockUniqueName},                                            //
                    {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_BLOCK), subgraphUniqueName},       //
                    {std::pmr::string(gr::serialization_fields::EDGE_SOURCE_PORT), "sigOut"s},                 //
                    {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_BLOCK), outerSinkUniqueName}, //
                    {std::pmr::string(gr::serialization_fields::EDGE_DESTINATION_PORT), "in"s},                //
                    {std::pmr::string(gr::serialization_fields::EDGE_MIN_BUFFER_SIZE), gr::Size_t(4096)},      //
                    {std::pmr::string(gr::serialization_fields::EDGE_WEIGHT), 1},                              //
                    {std::pmr::string(gr::serialization_fields::EDGE_NAME), "edge"s}};
                graphModel.sendMessage(std::move(message));
            }

            expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kEdgeEmplaced)) << "edge should be reported as emplaced by the scheduler";

            expect(awaitCondition(ctx, [&] {
                const UiGraphEdge* edge = findEdge(rootGraph(), subgraphUniqueName, outerSinkUniqueName);
                return edge && edge->edgeSourcePort && edge->edgeDestinationPort && edge->edgeSourcePort->portName == "sigOut";
            })) << "edge did not get created to exported port in our graph model";

            g_state.stopScheduler();
        });

        registerMessageTest("group middle blocks of a chain", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* source  = findByName("source");
            UiGraphBlock* middleA = findByName("middleA");
            UiGraphBlock* middleB = findByName("middleB");
            UiGraphBlock* sink    = findByName("sink");
            expect(source != nullptr && middleA != nullptr && middleB != nullptr && sink != nullptr) << fatal;
            const auto sourceUniqueName  = source->blockUniqueName;
            const auto middleAUniqueName = middleA->blockUniqueName;
            const auto middleBUniqueName = middleB->blockUniqueName;
            const auto sinkUniqueName    = sink->blockUniqueName;

            UiGraphBlock* subgraph = groupAndWait(ctx, {middleAUniqueName, middleBUniqueName});
            expect(subgraph != nullptr) << fatal;
            expect(awaitCondition(ctx, [] { return rootGraph()->childEdges.size() == 2UZ; })) << fatal << "boundary edges did not show up in the root graph";

            UiGraphBlock* innerA = subgraph->findBlockByUniqueName(middleAUniqueName);
            UiGraphBlock* innerB = subgraph->findBlockByUniqueName(middleBUniqueName);
            expect(innerA != nullptr && innerB != nullptr) << fatal;

            const UiGraphEdge* interiorEdge = findEdge(subgraph, middleAUniqueName, middleBUniqueName);
            expect(interiorEdge != nullptr) << fatal << "interior edge middleA->middleB should stay inside the subgraph";
            expect(interiorEdge->edgeSourcePort != nullptr && interiorEdge->edgeDestinationPort != nullptr);
            expect(eq(subgraph->childEdges.size(), 1UZ));

            expect(subgraph->inputPorts().size() == 1UZ) << fatal << "middleA's input should be the only exported input";
            expect(subgraph->outputPorts().size() == 1UZ) << fatal << "middleB's output should be the only exported output";
            expect(subgraph->inputPorts().front().portName.starts_with("middleA.")) << "exported input should be named after middleA";
            expect(subgraph->outputPorts().front().portName.starts_with("middleB.")) << "exported output should be named after middleB";
            expect(innerA->_inputPorts.front().getExportedName(subgraph) == subgraph->inputPorts().front().portName);
            expect(innerB->_outputPorts.front().getExportedName(subgraph) == subgraph->outputPorts().front().portName);

            const UiGraphEdge* inEdge = findEdge(rootGraph(), sourceUniqueName, subgraph->blockUniqueName);
            expect(inEdge != nullptr) << fatal << "source should be connected to the subgraph";
            expect(inEdge->edgeDestinationPort != nullptr && inEdge->edgeDestinationPort->ownerBlock == subgraph);
            expect(inEdge->edgeDestinationPort->portName == subgraph->inputPorts().front().portName);

            const UiGraphEdge* outEdge = findEdge(rootGraph(), subgraph->blockUniqueName, sinkUniqueName);
            expect(outEdge != nullptr) << fatal << "subgraph should be connected to the sink";
            expect(outEdge->edgeSourcePort != nullptr && outEdge->edgeSourcePort->ownerBlock == subgraph);
            expect(outEdge->edgeSourcePort->portName == subgraph->outputPorts().front().portName);

            g_state.stopScheduler();
        });

        registerMessageTest("group and ungroup unconnected blocks", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* loner1 = findByName("loner1");
            UiGraphBlock* loner2 = findByName("loner2");
            expect(loner1 != nullptr && loner2 != nullptr) << fatal;
            const auto loner1UniqueName = loner1->blockUniqueName;
            const auto loner2UniqueName = loner2->blockUniqueName;

            UiGraphBlock* subgraph = groupAndWait(ctx, {loner1UniqueName, loner2UniqueName});
            expect(subgraph != nullptr) << fatal;

            expect(subgraph != nullptr) << fatal;

            expect(subgraph->isGraph()) << "grouping into gr::Graph should create a transparent subgraph";
            expect(subgraph->inputPorts().empty()) << "no connections + nothing to export";
            expect(subgraph->outputPorts().empty()) << "no connections + nothing to export";
            expect(subgraph->childEdges.empty());
            expect(subgraph->exportedInputPorts.empty());
            expect(subgraph->exportedOutputPorts.empty());
            expect(subgraph->findBlockByUniqueName(loner1UniqueName) != nullptr);
            expect(subgraph->findBlockByUniqueName(loner2UniqueName) != nullptr);
            expect(rootGraph()->findBlockByUniqueName(loner1UniqueName) == nullptr) << "grouped block should no longer be a direct child of the root graph";
            expect(eq(rootGraph()->childEdges.size(), 3UZ)) << "other edges should still be intact after grouping";

            sendUngroupBlocks(subgraph->blockUniqueName);
            expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlocksUngrouped)) << "scheduler did not confirm ungrouping";

            expect(awaitCondition(ctx, [loner1UniqueName, loner2UniqueName] {
                UiGraphBlock* root = rootGraph();
                return root->findBlockByUniqueName(loner1UniqueName) != nullptr && //
                       root->findBlockByUniqueName(loner2UniqueName) != nullptr && //
                       findSubgraphIn(root) == nullptr;
            })) << "ungrouped blocks should return to the root graph and the empty subgraph should disappear";

            g_state.stopScheduler();
        });

        registerMessageTest("ungroup restores boundary connections", [](ImGuiTestContext* ctx) {
            reloadAndWait(ctx, reloadGrouping);

            UiGraphBlock* source  = findByName("source");
            UiGraphBlock* middleA = findByName("middleA");
            UiGraphBlock* middleB = findByName("middleB");
            UiGraphBlock* sink    = findByName("sink");
            expect(source != nullptr && middleA != nullptr && middleB != nullptr && sink != nullptr) << fatal;
            const auto sourceUniqueName  = source->blockUniqueName;
            const auto middleAUniqueName = middleA->blockUniqueName;
            const auto middleBUniqueName = middleB->blockUniqueName;
            const auto sinkUniqueName    = sink->blockUniqueName;

            UiGraphBlock* subgraph = groupAndWait(ctx, {middleAUniqueName, middleBUniqueName});
            expect(subgraph != nullptr) << fatal;

            sendUngroupBlocks(subgraph->blockUniqueName);
            expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlocksUngrouped)) << "scheduler did not confirm ungrouping";

            expect(awaitCondition(ctx,
                [middleAUniqueName] {
                    UiGraphBlock* root = rootGraph();
                    return root->findBlockByUniqueName(middleAUniqueName) != nullptr && findSubgraphIn(root) == nullptr && root->childEdges.size() == 3UZ;
                }))
                << fatal << "chain was not restored in the root graph";

            for (const auto& [from, to] : {std::pair{sourceUniqueName, middleAUniqueName}, {middleAUniqueName, middleBUniqueName}, {middleBUniqueName, sinkUniqueName}}) {
                const UiGraphEdge* edge = findEdge(rootGraph(), from, to);
                expect(edge != nullptr) << std::format("edge {} -> {} should be restored", from, to);
                expect(edge == nullptr || (edge->edgeSourcePort != nullptr && edge->edgeDestinationPort != nullptr)) << "restored edge should resolve its ports";
            }

            g_state.stopScheduler();
        });
    }

    static void reloadAndWait(ImGuiTestContext* ctx, void (*reloadFunction)()) {
        reloadFunction();
        g_state.waitForScheduler(ctx);
        while (!g_state.hasBlocks()) {
            ctx->Yield();
        }
    }

    static void sendExportPort(UiGraphBlock* subgraph, UiGraphBlock* innerBlock, const std::string& direction, const std::string& portName, const std::string& exportedName) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = gr::graph::property::kSubgraphExportPort;
        message.serviceName = subgraph->blockUniqueName;
        message.data        = gr::property_map{{"uniqueBlockName", innerBlock->blockUniqueName}, {"portDirection", direction}, {"portName", portName}, {"exportedName", exportedName}, {"exportFlag", true}};
        g_state.dashboard->graphModel.sendMessage(std::move(message));
    }

    static UiGraphBlock* groupAndWait(ImGuiTestContext* ctx, const std::vector<std::string>& uniqueNames) {
        sendGroupBlocks(uniqueNames);
        expect(waitForReplyOnEndpoint(ctx, gr::scheduler::property::kBlocksGrouped)) << "scheduler should confirm grouping";

        const bool grouped = awaitCondition(ctx, [expectedChildren = uniqueNames.size()] {
            UiGraphBlock* subgraph = findSubgraphIn(rootGraph());
            return subgraph && subgraph->childBlocks.size() == expectedChildren;
        });
        expect(grouped) << "subgraph with grouped blocks should also appear in the graph model";
        return grouped ? findSubgraphIn(rootGraph()) : nullptr;
    }

    void registerMessageTest(const char* name, ImGuiTestTestFunc testFunc) {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "graphmodel", name);
        t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();
        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));
            g_state.dashboard->handleMessages();
        };
        t->TestFunc = testFunc;
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
