#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/FunctionGenerator.hpp>
#include <gnuradio-4.0/basic/clock_source.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/meta/formatter.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include "imgui_test_engine/imgui_te_internal.h"

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    std::function<void()>                   stopFunction;

    void startScheduler() { dashboard->scheduler()->start(); }
    void stopScheduler() { dashboard->scheduler()->stop(); }
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
            ImGui::SetWindowSize(ImVec2(1200, 400));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.setDashboard(*g_state.dashboard);
                page.draw();
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

                while (gr::lifecycle::isActive(implotSink->state())) {
                    ImGuiTestEngine_Yield(ctx->Engine);
                }

                g_state.stopScheduler();
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto& registry = gr::globalBlockRegistry();
    gr::registerBlock<"gr::basic::ClockSource", gr::basic::DefaultClockSource, std::uint8_t>(registry);
    gr::registerBlock<gr::basic::FunctionGenerator, float>(registry);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart_fg_dipole";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto fs            = cmrc::ui_test_assets::get_filesystem();
    auto grcFile       = fs.open("examples/fg_dipole_intensity_ramp.grc");
    auto dashboardFile = fs.open("examples/fg_dipole_intensity_ramp.yml");

    auto dashboardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(dashboardDescription);
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()), //
        [](gr::Graph&& grGraph) {
            fmt::print("!!! Setting the graph... !!!\n");
            g_state.dashboard->emplaceScheduler(std::move(grGraph));
        });

    g_state.startScheduler();

    return app.runTests() ? 0 : 1;
}
