#include "Dashboard.hpp"
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/TestSpectrumGenerator.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <cmrc/cmrc.hpp>

#include <memory>
#include <string>
#include <thread>

CMRC_DECLARE(sample_dashboards);

#if __has_include(<imgui_node_editor.h>)
#error "opendigitizer::dashboard must not expose the flowgraph editor's imgui-node-editor"
#endif

using namespace boost::ut;
using gr::lifecycle::State;

namespace {

void registerDemoDashboardBlocks() {
    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    std::ignore = gr::registerBlock<opendigitizer::Arithmetic, float>(registry);
    std::ignore = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
    std::ignore = gr::registerBlock<opendigitizer::TestSpectrumGenerator, float>(registry);
}

std::string demoDashboardGrc() {
    const auto file = cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/DemoDashboard.grc");
    return {file.begin(), file.end()};
}

void loadGrc(DigitizerUi::Dashboard& dashboard, const std::string& grc) {
    dashboard.loadAndThen(grc, [&dashboard](gr::Graph&& graph) { dashboard.emplaceGraph(std::move(graph)); });
}

// no deadline: a state that is never reached is caught by the ctest timeout
template<typename Condition>
void waitUntil(DigitizerUi::Dashboard& dashboard, Condition&& condition) {
    while (!condition(dashboard.scheduler->state())) {
        dashboard.handleMessages();
        std::this_thread::yield();
    }
}

constexpr auto kIsActive  = [](State state) { return gr::lifecycle::isActive(state); };
constexpr auto kIsStopped = [](State state) { return state == State::STOPPED; };

void stopAndWait(DigitizerUi::Dashboard& dashboard) {
    waitUntil(dashboard, kIsActive);
    expect(dashboard.scheduler.stopUnlessPending().has_value());
    waitUntil(dashboard, kIsStopped);
}

} // namespace

// tests run inside main(): the dashboard uses function-local statics (e.g. ColourManager) that are already destroyed
// when statically registered suites run at exit
int main() {
    ImGui::CreateContext();
    ImPlot::CreateContext();
    registerDemoDashboardBlocks();
    const std::string grc        = demoDashboardGrc();
    const auto        restClient = std::make_shared<opencmw::client::RestClient>();

    "demo dashboard loads through opendigitizer::dashboard alone"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("consumer"));
        loadGrc(*dashboard, grc);

        expect(static_cast<bool>(dashboard->scheduler)) << "scheduler created from the .grc";
        expect(!dashboard->uiWindows.empty()) << "chart windows created from the .grc layout";

        dashboard.reset(); // stops the scheduler, also while its start-up thread is still initialising
    };

    "a stopped scheduler runs again after start()"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("restart"));
        loadGrc(*dashboard, grc);
        stopAndWait(*dashboard);

        expect(dashboard->scheduler->start().has_value());
        waitUntil(*dashboard, kIsActive);
    };

    "a dashboard loads again after its scheduler stopped"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("reload"));
        loadGrc(*dashboard, grc);
        stopAndWait(*dashboard);

        loadGrc(*dashboard, grc);
        waitUntil(*dashboard, kIsActive);
        expect(!dashboard->uiWindows.empty());
    };

    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
