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

#include <boost/ut.hpp>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

opendigitizer::test::TestDashboardRunner g_state;

void reloadSubgraph() { g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

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
