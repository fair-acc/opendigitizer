#include "Dashboard.hpp"
#include "LogHistory.hpp"
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
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

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
    dashboard.loadAndThen(grc, [&dashboard](gr::Graph&& graph) { dashboard.session.emplaceGraph(std::move(graph)); });
}

// no deadline: a state that is never reached is caught by the ctest timeout
template<typename Condition>
void waitUntil(DigitizerUi::Dashboard& dashboard, Condition&& condition) {
    while (!condition(dashboard.session.state())) {
        dashboard.handleMessages();
        std::this_thread::yield();
    }
}

constexpr auto kIsActive  = [](State state) { return gr::lifecycle::isActive(state); };
constexpr auto kIsStopped = [](State state) { return state == State::STOPPED; };

void stopAndWait(DigitizerUi::Dashboard& dashboard) {
    waitUntil(dashboard, kIsActive);
    dashboard.session.stop();
    waitUntil(dashboard, kIsStopped);
}

} // namespace

// tests run inside main(): the dashboard uses function-local statics (e.g. ColourManager) that are already destroyed
// when statically registered suites run at exit
int main() {
    ImGui::CreateContext();
    constexpr ImVec4 kHostWindowBg{0.25f, 0.5f, 0.75f, 1.f};
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

    "a dashboard's load error reaches the log history and the toast list"_test = [&] {
        const auto recordCount = [] {
            const auto counts = DigitizerUi::logHistory().counts();
            return std::accumulate(counts.begin(), counts.end(), std::uint64_t{0});
        };
        const auto recordsBefore = recordCount();
        const auto toastsBefore  = ImGui::notifications.size();

        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("invalid"));
        loadGrc(*dashboard, "blocks: [ this is not a flowgraph");

        expect(recordCount() > recordsBefore) << "the load error is in the log history";
        expect(ImGui::notifications.size() > toastsBefore) << "and a toast is queued";
    };

    "a saved dashboard lists its flowgraph's plot sinks and keeps the layout it is given"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("serialise"));
        loadGrc(*dashboard, grc);
        while (dashboard->session.graphModel.recursiveGatherPlotSinks().size() < 7UZ) {
            dashboard->handleMessages();
            std::this_thread::yield();
        }

        const auto field = [](const gr::property_map& map, std::string_view key) { return map.find_value(std::string(key), std::pmr::get_default_resource()).value_or(gr::pmt::Value{}); };

        const gr::property_map windowLayout{{"marker", std::string("live layout")}};
        dashboard->layoutType      = DigitizerUi::DockingLayoutType::Grid;
        dashboard->windowLayout    = windowLayout;
        const auto [header, graph] = dashboard->serialise();
        const auto section         = field(graph, "dashboard").value_or(gr::property_map{});
        expect(field(section, "layout").value_or(std::string{}) == "Grid");
        expect(field(section, "windowLayout").value_or(gr::property_map{}) == windowLayout);

        std::set<std::string> sourceNames;
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

    "the charts run in the scheduler's graph and are saved as plots, not as flowgraph blocks"_test = [&] {
        const auto field      = [](const gr::property_map& map, std::string_view key) { return map.find_value(std::string(key), std::pmr::get_default_resource()).value_or(gr::pmt::Value{}); };
        const auto list       = [&field](const gr::property_map& map, std::string_view key) { return field(map, key).value_or(gr::Tensor<gr::pmt::Value>{}); };
        const auto blockNames = [&](const gr::property_map& graph) {
            std::set<std::string> names;
            for (const auto& block : list(graph, "blocks")) {
                names.insert(field(field(block.value_or(gr::property_map{}), "parameters").value_or(gr::property_map{}), "name").value_or(std::string{}));
            }
            return names;
        };
        const auto plots = [&](const gr::property_map& graph) {
            std::map<std::string, std::vector<std::string>> byName;
            for (const auto& plot : list(field(graph, "dashboard").value_or(gr::property_map{}), "plots")) {
                const auto plotMap = plot.value_or(gr::property_map{});
                auto&      sources = byName[field(plotMap, "name").value_or(std::string{})];
                for (const auto& source : list(plotMap, "sources")) {
                    sources.push_back(source.value_or(std::string{}));
                }
            }
            return byName;
        };
        const gr::property_map input = gr::pmt::yaml::deserialize(grc).value();

        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("roundTrip"));
        loadGrc(*dashboard, grc);
        const std::size_t chartsInGraph = static_cast<std::size_t>(std::ranges::count_if(dashboard->session.graph().blocks(), [](const auto& block) { return DigitizerUi::isChartTypeName(block->typeName()); }));
        expect(eq(chartsInGraph, plots(input).size())) << "one chart block per plot of the .grc, in the scheduler's graph";

        const auto [header, saved] = dashboard->serialise();
        expect(std::ranges::equal(blockNames(saved), blockNames(input))) << "the saved blocks are the .grc's blocks, without the charts";
        expect(std::ranges::equal(plots(saved), plots(input))) << "the saved plots are the .grc's plots with their sources";

        stopAndWait(*dashboard);
        dashboard->session.start();
        waitUntil(*dashboard, kIsActive);
        const auto inputPlots = plots(input);
        for (const auto& window : dashboard->uiWindows) {
            expect(window.block != nullptr && std::ranges::contains(dashboard->session.graph().blocks(), window.block)) << std::format("'{}' is still bound to a block of the graph", window.window->name);
            const auto plot = inputPlots.find(window.window->name);
            expect(plot != inputPlots.end() && window.block && eq(grc_compat::getBlockSinkNames(window.block.get()).size(), plot->second.size())) << std::format("'{}' keeps its sinks", window.window->name);
        }
        const auto [headerAfterRestart, savedAfterRestart] = dashboard->serialise();
        expect(std::ranges::equal(blockNames(savedAfterRestart), blockNames(input))) << "after a restart the saved blocks are still the .grc's";
        expect(std::ranges::equal(plots(savedAfterRestart), inputPlots)) << "after a restart the saved plots are still the .grc's";
        stopAndWait(*dashboard);
    };

    "demo dashboard loads through opendigitizer::dashboard alone"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("consumer"));
        loadGrc(*dashboard, grc);

        expect(static_cast<bool>(dashboard->session)) << "scheduler created from the .grc";
        expect(!dashboard->uiWindows.empty()) << "chart windows created from the .grc layout";
        expect(hostStyleKept()) << "creating a dashboard keeps the host's style";

        dashboard.reset(); // stops the scheduler, also while its start-up thread is still initialising
    };

    "a grid layout needs no per-plot rect"_test = [&] {
        constexpr std::string_view kGridWithoutRects = R"(blocks:
  - id: gr::basic::SignalGenerator<float32>
    parameters:
      name: gridSource
      sample_rate: 1000
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: gridSink
connections:
  - [ gridSource, 0, gridSink, 0 ]
dashboard:
  layout: Grid
  plots:
    - name: First
      sources:
        - gridSink
    - name: Second
      sources:
        - gridSink
)";
        auto                       dashboard         = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("grid"));
        loadGrc(*dashboard, std::string(kGridWithoutRects));
        expect(dashboard->isInitialised.load()) << "loaded";
        expect(eq(dashboard->uiWindows.size(), 2UZ)) << "both plots placed by the grid";
    };

    "a stopped scheduler runs again after start()"_test = [&] {
        auto dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("restart"));
        loadGrc(*dashboard, grc);
        stopAndWait(*dashboard);

        dashboard->session.start();
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
