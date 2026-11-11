#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"
#include "TestingBlockInspectionUtils.hpp"
#include "imgui.h"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <ToolbarView.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/TestSpectrumGenerator.hpp"
#include "charts/LocalisationChart.hpp"
#include "charts/YYChart.hpp"

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

CMRC_DECLARE(sample_dashboards);
CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState : public opendigitizer::test::TestDashboardRunner {
    std::shared_ptr<DigitizerUi::DashboardPage> dashboardPage;
};

TestState g_state;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "DashboardPage::drawPlot");
        t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 800));

            if (g_state.dashboard) {
                if (g_state.dashboard->isInitialised) {
                    static DigitizerUi::ToolbarView toolbar;
                    toolbar.draw(g_state.dashboard->session, false);
                }
                if (!g_state.dashboardPage) {
                    g_state.dashboardPage = std::make_shared<DigitizerUi::DashboardPage>();
                    g_state.dashboardPage->setDashboard(*g_state.dashboard);
                }
                g_state.dashboardPage->draw();
                ut::expect(!g_state.dashboard->uiWindows.empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_chart.grc");
            g_state.waitForScheduler(ctx);
            while (!g_state.hasBlocks()) {
                ctx->Yield();
            }

            "DashboardPage::drawPlot"_test = [ctx] {
                ctx->SetRef("Test Window");

                auto sinkPtr = opendigitizer::charts::SinkRegistry::instance().findSink([](const auto& sink) { return sink.name() == "sinesSink"; });
                ut::expect(sinkPtr != nullptr);

                opendigitizer::test::waitUntilAllSamplesDrawn(ctx, *g_state.dashboard);
                ut::expect(ut::eq(sinkPtr->size(), 2000UZ)) << "n_samples_max of SineClock in qa_chart.grc";

                captureScreenshot(*ctx);
            };

            "YYChart scatter draws one marker per sine-cosine sample"_test = [ctx] {
                g_state.dashboardPage.reset();
                g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_yy_scatter.grc");
                g_state.waitForScheduler(ctx);
                expect(g_state.waitUntil(ctx, "200 markers drawn", [] {
                    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("YYChart") != std::string_view::npos; });
                    return window != g_state.dashboard->uiWindows.end() && static_cast<const opendigitizer::charts::YYChart*>(window->block->raw())->drawnMarkers() == 200UZ;
                }));
                captureScreenshot(*ctx);
            };

            "LocalisationChart draws the hyperbolas of the arrival events and the position the locator published"_test = [ctx] {
                g_state.dashboardPage.reset();
                g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_localisation.grc");
                g_state.waitForScheduler(ctx);
                const auto chart = [] {
                    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("LocalisationChart") != std::string_view::npos; });
                    return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<opendigitizer::charts::LocalisationChart*>(window->block->raw());
                };
                expect(g_state.waitUntil(ctx, "three sequential-pair hyperbolas and the published position drawn", [&] { return chart() != nullptr && chart()->drawn().curves == 3UZ && chart()->drawnPosition().has_value(); }));
                if (chart() == nullptr || !chart()->drawnPosition()) {
                    return;
                }
                const auto [x, y] = *chart()->drawnPosition();
                expect(lt(std::hypot(x - 0.3, y - 0.2), 1e-6)) << std::format("({}, {})", x, y);
                std::optional<std::uint64_t> lastGeneration;
                std::size_t                  lastTraced = 0UZ;
                std::size_t                  changes    = 0UZ;
                std::size_t                  retraced   = 0UZ; // traces at an unchanged event generation
                expect(g_state.waitUntil(ctx, "three new event generations, each traced once", [&] {
                    const auto* c = chart();
                    if (c == nullptr || !c->tracedGeneration()) {
                        return false;
                    }
                    if (lastGeneration == c->tracedGeneration()) {
                        retraced += c->tracedEvents() - lastTraced;
                    } else if (lastGeneration) {
                        ++changes;
                    }
                    lastGeneration = c->tracedGeneration();
                    lastTraced     = c->tracedEvents();
                    return changes >= 3UZ;
                }));
                expect(eq(retraced, 0UZ)) << "traced once per received event, not per draw";
                const auto& drawn = chart()->drawn();
                expect(eq(drawn.receivers, 4UZ) && gt(drawn.intersections, 0UZ) && drawn.status && eq(drawn.curveLabels, 0UZ)) << "the defaults: receivers, intersections, status; no curve labels";
                captureScreenshot(*ctx);

                auto& shown              = *chart();
                shown.show_curves        = false;
                shown.show_intersections = false;
                shown.show_receivers     = false;
                shown.show_position      = false;
                shown.show_status        = false;
                expect(g_state.waitUntil(ctx, "every layer switched off", [&] {
                    const auto& d = chart()->drawn();
                    return d.curves == 0UZ && d.intersections == 0UZ && d.receivers == 0UZ && !d.position && !d.status;
                }));
                shown.show_curves       = true;
                shown.show_curve_labels = true;
                expect(g_state.waitUntil(ctx, "each curve labelled", [&] {
                    const auto& d = chart()->drawn();
                    return d.curves == 3UZ && d.curveLabels == 3UZ;
                }));
            };

#if defined(OD_ACOUSTIC_CAMERA) && defined(OD_TRIGGER_BLOCKS)
            "each localisation dashboard locates its simulated orbiting source in its four broadband layers (the fork arrays add a phase layer), stereo draws its one hyperbola, each shown channel marks its onset"_test = [ctx] {
                for (const std::string_view array : {"Square", "Tetrahedron", "Line", "Uma16", "Stereo"}) {
                    g_state.dashboardPage.reset();
                    g_state.reload(cmrc::sample_dashboards::get_filesystem(), std::format("assets/sampleDashboards/XtraLocalisation{}.grc", array).c_str());
                    g_state.waitForScheduler(ctx);
                    const auto chart = [] {
                        const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("LocalisationChart") != std::string_view::npos; });
                        return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<const opendigitizer::charts::LocalisationChart*>(window->block->raw());
                    };
                    const bool                     stereo = array == "Stereo";
                    const std::vector<std::size_t> shown  = array == "Stereo" ? std::vector<std::size_t>{0UZ, 1UZ} : array == "Uma16" ? std::vector<std::size_t>{1UZ, 14UZ, 7UZ, 8UZ} : std::vector<std::size_t>{0UZ};
                    expect(g_state.waitUntil(ctx, std::format("{}: the threshold, correlated, continuous and envelope layers draw {}", array, stereo ? "one hyperbola" : "a published position"), [&] {
                        const auto* c = chart();
                        return c != nullptr && c->drawnLayers().size() == (array == "Square" ? 4UZ : 5UZ) && std::ranges::all_of(std::views::iota(0UZ, 4UZ), [&](std::size_t k) { return stereo ? c->drawnLayers()[k].curves == 1UZ : c->drawnPositions()[k].has_value(); });
                    })) << array;
                    const auto newest = [](std::string_view name) -> std::optional<gr::DataSet<float>> {
                        const auto block = opendigitizer::test::findBlockInRootGraph(g_state.dashboard->session, name);
                        if (block == nullptr) {
                            return std::nullopt;
                        }
                        const auto*     sink = static_cast<const opendigitizer::ImPlotSink<gr::DataSet<float>>*>(block->raw());
                        std::lock_guard lock(sink->dataMutex());
                        return sink->dataSets().empty() ? std::nullopt : std::optional(sink->dataSets().back());
                    };
                    expect(g_state.waitUntil(ctx, std::format("{}: a triggered snapshot and a spectrum", array), [&] { return newest(std::format("snapshotSink{}", shown.front())) && newest("spectrum"); })) << array;
                    if (array != "Square") { // the fork arrays: the first two band-passed channels against each other
                        expect(g_state.waitUntil(ctx, std::format("{}: the Lissajous view draws its markers", array), [] {
                            const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("YYChart") != std::string_view::npos; });
                            return window != g_state.dashboard->uiWindows.end() && static_cast<const opendigitizer::charts::YYChart*>(window->block->raw())->drawnMarkers() > 0UZ;
                        })) << array;
                    }
                    const auto onsetAt = [](std::string name) {
                        return [name = std::move(name)](const gr::DataSet<float>& ds) -> std::optional<std::ptrdiff_t> {
                            for (const auto& [index, map] : ds.timing_events | std::views::join) {
                                if (const auto it = map.find(gr::tag::kTriggerName); it != map.end() && it->second.value_or(std::string_view{}) == name) {
                                    return index;
                                }
                            }
                            return std::nullopt;
                        };
                    };
                    const std::ptrdiff_t rate           = array == "Uma16" ? 48000 : 96000; // Hz: the UMA16v2's maximum, the other soundcards'
                    const std::ptrdiff_t cut            = rate / 100;                       // n_pre
                    const std::ptrdiff_t acrossAperture = rate / 1000;                      // 1 ms: 0.34 m, wider than either array
                    for (const std::size_t c : shown | std::views::drop(1)) {
                        std::ptrdiff_t onset = 0;
                        expect(g_state.waitUntil(ctx, std::format("{}: audio{}'s snapshot, cut at audio{}'s onset, marks its own", array, c, shown.front()), [&] {
                            const auto at = newest(std::format("snapshotSink{}", c)).and_then(onsetAt(std::format("audio{}", c)));
                            onset         = at.value_or(0);
                            return at.has_value();
                        })) << array;
                        expect(std::abs(onset - cut) <= acrossAperture) << std::format("{}: audio{} {} samples from the cut", array, c, onset - cut);
                    }
                    captureScreenshot(*ctx);
                }
            };

            "the array choice switches geometry, simulation and plotted channels live; every array locates its source"_test = [ctx] {
                g_state.dashboardPage.reset();
                g_state.reload(cmrc::sample_dashboards::get_filesystem(), "assets/sampleDashboards/XtraLocalisation.grc");
                g_state.waitForScheduler(ctx);
                const auto chart = [] {
                    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("LocalisationChart") != std::string_view::npos; });
                    return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<const opendigitizer::charts::LocalisationChart*>(window->block->raw());
                };
                const auto onsetFromCut = [](std::size_t slot, std::size_t channel) -> std::optional<std::ptrdiff_t> {
                    const auto block = opendigitizer::test::findBlockInRootGraph(g_state.dashboard->session, std::format("snapshotSink{}", slot));
                    if (block == nullptr) {
                        return std::nullopt;
                    }
                    const auto*     sink = static_cast<const opendigitizer::ImPlotSink<gr::DataSet<float>>*>(block->raw());
                    std::lock_guard lock(sink->dataMutex());
                    if (sink->dataSets().empty()) {
                        return std::nullopt;
                    }
                    for (const auto& [index, map] : sink->dataSets().back().timing_events | std::views::join) {
                        if (const auto it = map.find(gr::tag::kTriggerName); it != map.end() && it->second.value_or(std::string_view{}) == std::format("audio{}", channel)) {
                            return index - 1440; // n_pre
                        }
                    }
                    return std::nullopt;
                };
                struct Preset {
                    std::string_view         name;
                    std::size_t              receivers;
                    std::vector<std::size_t> shown;
                    double                   aperture; // m, between the farthest shown microphones
                };
                for (const auto& [name, receivers, shown, aperture] : {Preset{"square", 4UZ, {0UZ, 1UZ, 2UZ, 3UZ}, 1.7}, Preset{"stereo", 2UZ, {0UZ, 1UZ}, 0.3}, Preset{"line", 4UZ, {0UZ, 1UZ, 2UZ, 3UZ}, 9.0}, Preset{"line 0.3 m", 4UZ, {0UZ, 1UZ, 2UZ, 3UZ}, 0.3}, Preset{"tetrahedron", 4UZ, {0UZ, 1UZ, 2UZ, 3UZ}, 0.3}, Preset{"UMA16", 16UZ, {1UZ, 14UZ, 7UZ, 8UZ}, 0.18}}) {
                    if (name != "square") { // the default, applied at start
                        const DigitizerUi::UiGraphBlock* control = g_state.dashboard->session.graphModel.recursiveFindBlockByName("array").block;
                        expect(control != nullptr) << fatal;
                        ctx->ItemClick(opendigitizer::test::controlItemId(ctx, "//Test Window/##Toolbar", control->blockUniqueName, "##array"));
                        ctx->ItemClick(std::format("//$FOCUSED/{}", name).c_str());
                    }
                    const bool stereo = name == "stereo";
                    expect(g_state.waitUntil(ctx, std::format("{}: {} receivers and, threshold, correlated, continuous and envelope alike, {}", name, receivers, stereo ? "one hyperbola" : "a located position"), [&] {
                        const auto* c = chart();
                        return c != nullptr && c->drawn().receivers == receivers && c->drawnLayers().size() == 5UZ && std::ranges::all_of(std::views::iota(0UZ, 4UZ), [&](std::size_t k) { return stereo ? c->drawnLayers()[k].curves == 1UZ : c->drawnPositions()[k].has_value(); });
                    })) << name;
                    const auto acrossAperture = static_cast<std::ptrdiff_t>(std::ceil(aperture / 343.0 * 48000.0)) + 2; // samples
                    for (std::size_t slot = 1UZ; slot < shown.size(); ++slot) {
                        expect(g_state.waitUntil(ctx, std::format("{}: audio{} marks its onset in slot {}, within {} samples of the cut", name, shown[slot], slot, acrossAperture), [&] { //
                            return onsetFromCut(slot, shown[slot]).transform([&](std::ptrdiff_t fromCut) { return std::abs(fromCut) <= acrossAperture; }).value_or(false);
                        })) << name;
                    }
                    captureScreenshot(*ctx);
                }
            };

            "the line's band choice 'fork 440 Hz' moves to the 0.3 m line and a struck fork, which the continuous layer locates"_test = [ctx] {
                g_state.dashboardPage.reset();
                g_state.reload(cmrc::sample_dashboards::get_filesystem(), "assets/sampleDashboards/XtraLocalisationLine.grc");
                g_state.waitForScheduler(ctx);
                const auto chart = [] {
                    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("LocalisationChart") != std::string_view::npos; });
                    return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<const opendigitizer::charts::LocalisationChart*>(window->block->raw());
                };
                const DigitizerUi::UiGraphBlock* control = g_state.dashboard->session.graphModel.recursiveFindBlockByName("band").block;
                expect(control != nullptr) << fatal;
                ctx->ItemClick(opendigitizer::test::controlItemId(ctx, "//Test Window/##Toolbar", control->blockUniqueName, "##band"));
                ctx->ItemClick("//$FOCUSED/fork 440 Hz");
                constexpr double kOrbitRadius   = 0.2;  // m, LINE_FORK's orbit around (0.2, 0.5)
                constexpr double kForkTolerance = 0.05; // m, the fork's bound in qa_DelayLocalisation
                expect(g_state.waitUntil(ctx, "the chart's receivers move to the 0.3 m line", [&] {
                    const auto* c = chart();
                    return c != nullptr && std::ranges::all_of(opendigitizer::localisation::pointsOf(c->element_positions.value), [](const auto& p) { return std::abs(p[0]) <= 0.15 + 1e-9; });
                }));
                const auto onOrbit = [&](std::size_t layer) {
                    const auto* c = chart();
                    return c != nullptr && c->drawnPositions().size() == 5UZ && c->drawnPositions()[layer].transform([&](const auto& p) { return std::abs(std::hypot(p[0] - 0.2, p[1] - 0.5) - kOrbitRadius) < kForkTolerance; }).value_or(false);
                };
                expect(g_state.waitUntil(ctx, "the continuous layer locates the fork on its orbit", [&] { return onOrbit(2UZ); }));
                expect(g_state.waitUntil(ctx, "the phase layer, framed at the fork's envelope onsets, locates it on its orbit", [&] { return onOrbit(4UZ); }));
                captureScreenshot(*ctx);
            };
#endif
        };
    }
};

namespace {
template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<opendigitizer::Arithmetic, float>(registry);
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

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto result = app.runTests();
    g_state.dashboardPage.reset();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
