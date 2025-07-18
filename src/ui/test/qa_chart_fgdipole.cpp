#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Message.hpp>
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
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <memory.h>

#include <plf_colony.h>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "../components/ColourManager.hpp"

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::shared_ptr<DigitizerUi::Dashboard>      dashboard;
    std::function<void()>                        stopFunction;

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
                ut::expect(!g_state.dashboard->plots().empty());
            }
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
                g_state.stopScheduler();
                expect(g_state.dashboard->scheduler()->state() == gr::lifecycle::STOPPED) << "g_state.scheduler not in STOPPED state";
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
        [](gr::Graph&& grGraph) {
            using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded>;
            g_state.dashboard->emplaceScheduler<TScheduler, gr::Graph>(std::move(grGraph));
        });

    return app.runTests() ? 0 : 1;
}
