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
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/meta/formatter.hpp>
#include <gnuradio-4.0/testing/ImChartMonitor.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
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
        schedulerThread = std::thread([&] {
            auto ret = scheduler->runAndWait();
            expect(ret.has_value()) << [&ret]() { return std::format("TestScheduler returned with: {} in {}:{}", ret.error().message, ret.error().srcLoc(), ret.error().methodName()); };
        });
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

template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<gr::basic::FunctionGenerator, float>(registry);
    registry.template addBlockType<gr::basic::DefaultClockSource<std::uint8_t>>("gr::basic::ClockSource");
    gr::registerBlock<gr::basic::StreamToDataSet, float>(registry);
    gr::registerBlock<"gr::basic::StreamToDataSet", gr::basic::StreamToDataSet, float>(registry);
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
    gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float>(registry);
    gr::registerBlock<gr::blocks::fft::DefaultFFT, float>(registry);
    gr::registerBlock<gr::testing::TagSink, gr::testing::ProcessFunction::USE_PROCESS_BULK, float>(registry);
    gr::registerBlock<gr::testing::TagSink, gr::testing::ProcessFunction::USE_PROCESS_BULK, uint8_t>(registry);
    gr::registerBlock<gr::testing::NullSink, float, uint8_t, uint16_t, gr::DataSet<float>, gr::DataSet<uint8_t>, gr::DataSet<uint16_t>>(registry);
    registry.template addBlockType<gr::testing::ImChartMonitor<float, false>>("gr::basic::ConsoleDebugSink<float>");
    registry.template addBlockType<gr::testing::ImChartMonitor<gr::DataSet<float>, false>>("gr::basic::ConsoleDebugSink<gr::DataSet<float>>");
#pragma GCC diagnostic pop
}

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(1200, 800));

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

                auto getUiSink = [](auto typeTag, std::string_view name) {
                    using SinkType      = decltype(typeTag);
                    auto* implotSinkRaw = opendigitizer::ImPlotSinkManager::instance().findSink([name](const auto& sink) { return sink.name() == name; });

                    ut::expect(implotSinkRaw);
                    return reinterpret_cast<opendigitizer::ImPlotSink<SinkType>*>(implotSinkRaw->raw());
                };

                auto implotDipoleSink        = getUiSink(float{}, "DipoleCurrentSink");
                auto implotIntensitySink     = getUiSink(float{}, "IntensitySink");
                auto implotDipoleDataSetSink = getUiSink(float{}, "DipoleCurrentDataSetSink");

                g_state.waitForScheduler(10);

                std::size_t count = 0UZ;
                while (!isActive(implotDipoleSink->state()) || count < 20) { // wait until scheduler is started
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    count++;
                }

                while (isActive(implotDipoleSink->state()) || isActive(implotIntensitySink->state()) || isActive(implotDipoleDataSetSink->state())) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                expect(implotDipoleSink->state() == gr::lifecycle::STOPPED) << "implotDipoleSink not in STOPPED state";
                expect(implotIntensitySink->state() == gr::lifecycle::STOPPED) << "implotIntensitySink not in STOPPED state";
                expect(implotDipoleDataSetSink->state() == gr::lifecycle::STOPPED) << "implotDipoleDataSetSink not in STOPPED state";
                expect(g_state.scheduler->state() == gr::lifecycle::STOPPED) << "g_state.scheduler not in STOPPED state";
                g_state.stopScheduler();
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {

    gr::BlockRegistry registry = gr::globalBlockRegistry();
    registerTestBlocks(registry);
    gr::PluginLoader pluginLoader(registry, {});

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart_fg_dipole";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto fs            = cmrc::ui_test_assets::get_filesystem();
    auto grcFile       = fs.open("examples/fg_dipole_intensity_ramp.grc");
    auto dashboardFile = fs.open("examples/fg_dipole_intensity_ramp.yml");

    auto dashBoardDescription       = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard               = DigitizerUi::Dashboard::create(/**fgItem=*/nullptr, dashBoardDescription);
    g_state.dashboard->pluginLoader = std::make_shared<gr::PluginLoader>(std::move(pluginLoader));
    g_state.dashboard->load(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()), [](gr::Graph&& grGraph) { g_state.assignGraph(std::move(grGraph)); });

    g_state.startScheduler();

    return app.runTests() ? 0 : 1;
}
