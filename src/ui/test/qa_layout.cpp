#include "../ui/common/ImguiWrap.hpp"
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
#include "gnuradio-4.0/basic/SignalGenerator.hpp"
#include "imgui_test_engine/imgui_te_internal.h"
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>

#include <cmrc/cmrc.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <memory.h>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::DockingLayoutType          layoutType = DigitizerUi::DockingLayoutType::Row;
};

TestState g_state;

template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<gr::basic::SignalGenerator, float>(registry);
    gr::registerBlock<"gr::basic::ClockSource", gr::basic::DefaultClockSource, std::uint8_t>(registry);
#pragma GCC diagnostic pop
}

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboardpage", "layouting");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            auto&                    vars = ctx->GetVars<TestState>();
            DigitizerUi::IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state.dashboard) {
                DigitizerUi::DashboardPage page;
                page.setLayoutType(vars.layoutType);
                page.draw(*g_state.dashboard);
                ut::expect(!g_state.dashboard->plots().empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "DashboardPage::layout"_test = [ctx] {
                auto& vars = ctx->GetVars<TestState>();
                ctx->SetRef("Test Window");

                vars.layoutType = DigitizerUi::DockingLayoutType::Row;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Column;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Grid;
                captureScreenshot(*ctx);

                vars.layoutType = DigitizerUi::DockingLayoutType::Free;
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboardpage_layout";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    gr::BlockRegistry registry = gr::globalBlockRegistry();
    registerTestBlocks(registry);
    gr::PluginLoader pluginLoader(registry, {});

    auto fs            = cmrc::ui_test_assets::get_filesystem();
    auto grcFile       = fs.open("examples/qa_layout.grc");
    auto dashboardFile = fs.open("examples/qa_layout.yml");

    auto dashBoardDescription       = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard               = DigitizerUi::Dashboard::create(/**fgItem=*/nullptr, dashBoardDescription);
    g_state.dashboard->pluginLoader = std::make_shared<gr::PluginLoader>(std::move(pluginLoader));
    g_state.dashboard->load(std::string(grcFile.begin(), grcFile.end()), std::string(dashboardFile.begin(), dashboardFile.end()));

    return app.runTests() ? 0 : 1;
}
