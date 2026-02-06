#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/meta/formatter.hpp>

#include <GrBasicBlocks.hpp>
#include <GrTestingBlocks.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>
#include <memory>
#include <plf_colony.h>

#include "../components/ColourManager.hpp"

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::shared_ptr<DigitizerUi::Dashboard>      dashboard;

    void stopScheduler() { dashboard->scheduler->stop(); }

    void waitForSchedulerActive(std::size_t maxCount = 30UZ, std::source_location location = std::source_location::current()) {
        std::size_t count = 0;
        while (!gr::lifecycle::isActive(dashboard->scheduler->state()) && count < maxCount) {
            dashboard->handleMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;
        }
        if (count >= maxCount) {
            throw gr::exception(std::format("waitForSchedulerActive({}): maxCount exceeded", count), location);
        }
    }
};

TestState g_state;

template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
#pragma GCC diagnostic pop
}

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(1200, 800));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.setDashboard(*g_state.dashboard);
                page.draw();
                ut::expect(!g_state.dashboard->uiWindows.empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                // Find sinks using the SignalSink interface
                auto dipoleSink        = opendigitizer::charts::SinkRegistry::instance().findSink([](const auto& sink) { return sink.name() == "DipoleCurrentSink"; });
                auto intensitySink     = opendigitizer::charts::SinkRegistry::instance().findSink([](const auto& sink) { return sink.name() == "IntensitySink"; });
                auto dipoleDataSetSink = opendigitizer::charts::SinkRegistry::instance().findSink([](const auto& sink) { return sink.name() == "DipoleCurrentDataSetSink"; });

                ut::expect(dipoleSink != nullptr) << "DipoleCurrentSink not found";
                ut::expect(intensitySink != nullptr) << "IntensitySink not found";
                ut::expect(dipoleDataSetSink != nullptr) << "DipoleCurrentDataSetSink not found";

                g_state.waitForSchedulerActive();

                // Wait for sinks to accumulate data (DataSet sink needs the P=2→P=5 tag window at ~1s)
                while (dipoleDataSetSink->dataSetCount() == 0 || dipoleSink->size() == 0 || intensitySink->size() == 0) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                // Verify sinks received data
                expect(dipoleSink->size() > 0) << "DipoleCurrentSink has no data";
                expect(intensitySink->size() > 0) << "IntensitySink has no data";
                expect(dipoleDataSetSink->dataSetCount() > 0) << "DipoleCurrentDataSetSink has no datasets";

                g_state.stopScheduler();
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    [[maybe_unused]] opendigitizer::ColourManager& colourManager = opendigitizer::ColourManager::instance();

    auto& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart_fg_dipole";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto fs      = cmrc::ui_test_assets::get_filesystem();
    auto grcFile = fs.open("examples/fg_dipole_intensity_ramp.grc");

    auto dashboardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(g_state.restClient, dashboardDescription);
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), //
        [](gr::Graph&& grGraph) {                                               //
            g_state.dashboard->emplaceGraph(std::move(grGraph));
        });

    auto result = app.runTests();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
