#include "ImGuiTestApp.hpp"
#include "imgui.h"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <GrBasicBlocks.hpp>
#include <GrFourierBlocks.hpp>
#include <GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>

#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;

    void startScheduler() { dashboard->scheduler()->start(); }
    void stopScheduler() { dashboard->scheduler()->stop(); }

    void waitForScheduler(std::size_t maxCount = 100UZ, std::source_location location = std::source_location::current()) {
        std::size_t count = 0;
        while (!gr::lifecycle::isActive(dashboard->scheduler()->state()) && count < maxCount) {
            // wait until scheduler is started
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;
        }
        if (count >= maxCount) {
            throw gr::exception(std::format("waitForScheduler({}): maxCount exceeded", count), location);
        }
    }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.setDashboard(*g_state.dashboard);
                page.draw();
                ut::expect(!g_state.dashboard->plots().empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                auto* implotSinkRaw = opendigitizer::ImPlotSinkManager::instance().findSink([](const auto& sink) { return sink.name() == "DipoleCurrentSink"; });
                ut::expect(implotSinkRaw);
                auto implotSink = reinterpret_cast<opendigitizer::ImPlotSink<float>*>(implotSinkRaw->raw());

                // g_state.waitForScheduler();

                const int maxSamples = 3000;
                while (implotSink->_yValues.size() < maxSamples) {
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
    auto    restClient = std::make_shared<opencmw::client::RestClient>(opencmw::client::VerifyServerCertificates(false));

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto fs      = cmrc::sample_dashboards::get_filesystem();
    auto grcFile = fs.open("assets/sampleDashboards/DemoDashboard.grc");

    auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [](gr::Graph&& grGraph) { //
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>;
        g_state.dashboard->emplaceScheduler<TScheduler, gr::Graph>(std::move(grGraph));
    });

    g_state.startScheduler();

    return app.runTests() ? 0 : 1;
}
