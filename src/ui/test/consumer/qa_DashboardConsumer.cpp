#include "Dashboard.hpp"
#include "Setup.hpp"
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/TestSpectrumGenerator.hpp"
#include "components/ImGuiNotify.hpp"

#include <boost/ut.hpp>
#include <implot3d.h>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <set>
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
    constexpr ImVec4 kHostWindowBg{0.25f, 0.5f, 0.75f, 1.f}; // a host theme OpenDigitizer must not overwrite
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = kHostWindowBg;
    const auto hostStyleKept                    = [&] {
        const ImVec4 c = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        return c.x == kHostWindowBg.x && c.y == kHostWindowBg.y && c.z == kHostWindowBg.z && c.w == kHostWindowBg.w;
    };

    DigitizerUi::initialise();
    registerDemoDashboardBlocks();
    const std::string grc        = demoDashboardGrc();
    const auto        restClient = std::make_shared<opencmw::client::RestClient>();

    "initialise() keeps the host's style and creates the plot contexts"_test = [&] {
        expect(hostStyleKept());
        expect(ImPlot::GetCurrentContext() != nullptr);
        expect(ImPlot3D::GetCurrentContext() != nullptr);
    };

    "registerDashboardBlocks() registers every chart type and the plot sink, whatever the caller includes"_test = [] {
        gr::BlockRegistry registry;
        DigitizerUi::registerDashboardBlocks(registry);
        for (std::string_view chart : {"XYChart", "YYChart", "SpectrumPlot", "SpectrumView", "SpectrumDensity", "WaterfallPlot", "SurfacePlot"}) {
            expect(registry.contains(std::format("opendigitizer::charts::{}", chart))) << chart;
        }
        expect(std::ranges::any_of(registry.keys(), [](const auto& key) { return std::string_view(key).starts_with("opendigitizer::ImPlotSink"); })) << "ImPlotSink";
    };

    "a host receives dashboard notifications through its sink instead of the toast list"_test = [&] {
        std::vector<std::string> received;
        DigitizerUi::components::Notification::sink = [&received](ImGuiToastType, std::string_view text) { received.emplace_back(text); };
        const auto toastsBefore                     = ImGui::notifications.size();

        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("invalid"));
        loadGrc(*dashboard, "blocks: [ this is not a flowgraph");

        DigitizerUi::components::Notification::sink = nullptr;
        expect(!received.empty()) << "the load error reached the sink";
        expect(eq(ImGui::notifications.size(), toastsBefore)) << "no toast queued";
    };

    "a saved dashboard lists its flowgraph's plot sinks and keeps the layout it is given"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("serialise"));
        loadGrc(*dashboard, grc);
        while (dashboard->graphModel.recursiveGatherPlotSinks().size() < 7UZ) { // the model fills from scheduler replies
            dashboard->handleMessages();
            std::this_thread::yield();
        }

        const auto field = [](const gr::property_map& map, std::string_view key) { return map.find_value(std::string(key), std::pmr::get_default_resource()).value_or(gr::pmt::Value{}); };

        const gr::property_map windowLayout{{"marker", std::string("live layout")}};
        const auto [header, graph] = dashboard->serialise(DigitizerUi::DockingLayoutType::Grid, windowLayout);
        const auto section         = field(graph, "dashboard").value_or(gr::property_map{});
        expect(field(section, "layout").value_or(std::string{}) == "Grid");
        expect(field(section, "windowLayout").value_or(gr::property_map{}) == windowLayout);

        std::set<std::string> sourceNames; // the ImPlotSink blocks of DemoDashboard.grc, listed by hand
        for (const auto& source : field(section, "sources").value_or(gr::Tensor<gr::pmt::Value>{})) {
            sourceNames.insert(field(source.value_or(gr::property_map{}), "name").value_or(std::string{}));
        }
        expect(sourceNames == std::set<std::string>{"sinesSink", "sineSink1", "sineSink2", "fftSink", "DipoleCurrentSink", "IntensitySink", "spectrumSink"});

        auto reloaded = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("reloaded"));
        loadGrc(*reloaded, gr::pmt::yaml::serialize(graph));
        expect(reloaded->layoutType == DigitizerUi::DockingLayoutType::Grid) << "the saved layout is what a reload applies";
        stopAndWait(*reloaded);
        stopAndWait(*dashboard);
    };

    "demo dashboard loads through opendigitizer::dashboard alone"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("consumer"));
        loadGrc(*dashboard, grc);

        expect(static_cast<bool>(dashboard->scheduler)) << "scheduler created from the .grc";
        expect(!dashboard->uiWindows.empty()) << "chart windows created from the .grc layout";
        expect(hostStyleKept()) << "creating a dashboard keeps the host's style";

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

    ImPlot3D::DestroyContext();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}
