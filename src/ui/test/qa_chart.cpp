#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"
#include "gnuradio-4.0/basic/ClockSource.hpp"
#include "gnuradio-4.0/basic/FunctionGenerator.hpp"
#include "imgui_test_engine/imgui_te_internal.h"
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

// We derive from gr's scheduler just to make stop() public
template<gr::profiling::ProfilerLike TProfiler = gr::profiling::null::Profiler>
class TestScheduler : public gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler> {
public:
    explicit TestScheduler(gr::Graph&& graph) : gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler>(std::move(graph)) {}

    void stop() { gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreaded, TProfiler>::stop(); }
};

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard>                       dashboard;
    std::function<void()>                                         stopFunction;
    std::thread                                                   schedulerThread;
    std::unique_ptr<TestScheduler<gr::profiling::null::Profiler>> scheduler;

    void assignGraph(gr::Graph&& graph) { scheduler = std::make_unique<TestScheduler<gr::profiling::null::Profiler>>(std::move(graph)); }

    void startScheduler() {
        schedulerThread = std::thread([&] { scheduler->runAndWait(); });
    }

    void waitForScheduler(std::size_t maxCount = 100UZ, std::source_location location = std::source_location::current()) {
        std::size_t count = 0;
        while (!gr::lifecycle::isActive(scheduler->state()) && count < maxCount) {
            // wait until scheduler is started
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;
        }
        if (count >= maxCount) {
            throw gr::exception(fmt::format("waitForScheduler({}): maxCount exceeded", count), location);
        }
    }

    void stopScheduler() {
        scheduler->stop();
        schedulerThread.join();
    }
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.draw(*g_state.dashboard);
                ut::expect(!g_state.dashboard->plots().empty());
            }

            ImGui::End();
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
    gr::registerBlock<gr::basic::FunctionGenerator, float>(registry);
    gr::registerBlock<"gr::basic::ClockSource", gr::basic::DefaultClockSource, std::uint8_t>(registry);
    gr::registerBlock<gr::basic::DefaultClockSource, std::uint8_t, float>(registry);
    gr::registerBlock<gr::blocks::fft::DefaultFFT, float>(registry);
    gr::registerBlock<gr::testing::TagSink, gr::testing::ProcessFunction::USE_PROCESS_BULK, float>(registry);
    gr::registerBlock<gr::testing::TagSink, gr::testing::ProcessFunction::USE_PROCESS_BULK, uint8_t>(registry);
    gr::registerBlock<opendigitizer::Arithmetic, float>(registry);
    gr::registerBlock<opendigitizer::SineSource, float>(registry);
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
    gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float>(registry);

    fmt::print("providedBlocks:\n");
    for (auto& blockName : registry.providedBlocks()) {
        fmt::print("  - {}\n", blockName);
    }
#pragma GCC diagnostic pop
}
} // namespace

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart";

    gr::BlockRegistry registry = gr::globalBlockRegistry();
    registerTestBlocks(registry);
    gr::PluginLoader pluginLoader(registry, {});

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto fs            = cmrc::sample_dashboards::get_filesystem();
    auto grcFile       = fs.open("assets/sampleDashboards/DemoDashboard.grc");
    auto dashboardFile = fs.open("assets/sampleDashboards/DemoDashboard.yml");

    auto dashBoardDescription       = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard               = DigitizerUi::Dashboard::create(/**fgItem=*/nullptr, dashBoardDescription);
    g_state.dashboard->pluginLoader = std::make_shared<gr::PluginLoader>(std::move(pluginLoader));
    g_state.dashboard->load(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()), [](gr::Graph&& grGraph) { g_state.assignGraph(std::move(grGraph)); });

    g_state.startScheduler();

    return app.runTests() ? 0 : 1;
}
