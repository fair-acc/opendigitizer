#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"
#include "imgui.h"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "blocks/TestSpectrumGenerator.hpp"

#include <cmrc/cmrc.hpp>

#include <memory>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

opendigitizer::test::TestDashboardRunner g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.setDashboard(*g_state.dashboard);
                page.draw();
                ut::expect(!g_state.dashboard->uiWindows.empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            g_state.reload();
            g_state.waitForScheduler(ctx);
            while (!g_state.hasBlocks()) {
                ctx->Yield();
            }

            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                auto sinkPtr = opendigitizer::charts::SinkRegistry::instance().findSink([](const auto& sink) { return sink.name() == "DipoleCurrentSink"; });
                ut::expect(sinkPtr != nullptr);

                // Wait for samples to accumulate using the SignalSink interface
                const std::size_t maxSamples = 3000;
                while (sinkPtr->size() < maxSamples) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                g_state.stopScheduler();
                captureScreenshot(*ctx);
            };
        };
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
    options.screenshotPrefix = "chart";

    // This is not a globalBlockRegistry, but a copy of it
    auto& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    gr::PluginLoader pluginLoader(registry, gr::globalSchedulerRegistry(), {});

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto result = app.runTests();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
