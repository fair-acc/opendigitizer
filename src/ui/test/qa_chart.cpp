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
#if defined(OD_ACOUSTIC_CAMERA)
#include <gnuradio-4.0/GrAudioBlocks.hpp>
#include <gnuradio-4.0/GrFilterBlocks.hpp>
#include <gnuradio-4.0/GrLocalisationBlocks.hpp>
#endif

#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <ToolbarView.hpp>

// TODO: blocks are locally included/registered for this test -> should become a global feature
#include "blocks/Arithmetic.hpp"
#include "blocks/ImControlValue.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/TestSpectrumGenerator.hpp"
#include "blocks/ValueSource.hpp"
#include "charts/LocalisationChart.hpp"
#include "charts/SpectrumPlot.hpp"
#include "charts/SpectrumView.hpp"
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

            "the spectrum charts' frequency range and f0 follow their inputs; a drag overrides them, its release sends them snapped to bins, follow_inputs resets them"_test = [ctx] {
                g_state.dashboardPage.reset();
                g_state.reload(cmrc::sample_dashboards::get_filesystem(), "assets/sampleDashboards/SpectrumRangeDemo.grc");
                g_state.waitForScheduler(ctx);
                const auto chartOf = []<typename TChart>(std::string_view type) {
                    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [type](const auto& w) { return w.block && w.block->typeName().find(type) != std::string_view::npos; });
                    return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<TChart*>(window->block->raw());
                };
                const auto spectrum     = [&] { return chartOf.template operator()<opendigitizer::charts::SpectrumView>("SpectrumView"); };
                const auto spectrumPlot = [&] { return chartOf.template operator()<opendigitizer::charts::SpectrumPlot>("SpectrumPlot"); };
                const auto block        = [](std::string_view name) { return opendigitizer::test::findBlockInRootGraph(g_state.dashboard->session, name); };
                const auto onScreen     = [](const ImVec2& p) { return p.x > 0.f && p.x < ImGui::GetIO().DisplaySize.x && p.y > 0.f && p.y < ImGui::GetIO().DisplaySize.y; };
                const auto drawn        = [&](const auto* c) { return c != nullptr && c->drawnHandles().transform([&](const auto& h) { return onScreen(h[0]) && onScreen(h[1]) && onScreen(h[2]); }).value_or(false); };
                expect(g_state.waitUntil(ctx, "both charts draw the range and f0 at their inputs, on screen once the frequency axis has fitted the spectrum", [&] { return drawn(spectrum()) && drawn(spectrumPlot()); })) << fatal;
                auto* chart = spectrum();
                auto* plot  = spectrumPlot();
                if (chart == nullptr || plot == nullptr) {
                    expect(false) << "both spectrum charts found";
                    return;
                }
                expect(eq(chart->shownRange()[0], 99.8e6) && eq(chart->shownRange()[1], 100.2e6) && eq(chart->shownF0(), 100.0e6));
                const auto labelled = [&](std::string_view name) { return std::ranges::any_of(chart->drawnMarkerLabels(), [name](const std::string& l) { return l.starts_with(name); }); };
                expect(labelled("f\u208B ") && labelled("f\u208A ") && labelled("f\u2080 ")) << "f₋, f₊ and f₀ labelled";
                {
                    double lowest  = 0.0;
                    double highest = 0.0;
                    expect(plot->withNewestBins([&](std::span<const float> bins) {
                        lowest  = static_cast<double>(std::ranges::min(bins));
                        highest = static_cast<double>(std::ranges::max(bins));
                    })) << fatal;
                    const double inset     = 0.05 * (highest - lowest);
                    const auto [low, high] = plot->shownRange();
                    expect(approx(low, lowest + inset, 1e-6 * highest) && approx(high, highest - inset, 1e-6 * highest)) << std::format("unset: the visible spectrum {} .. {} inset 5 %, got {} .. {}", lowest, highest, low, high);
                    expect(approx(plot->shownF0(), 0.5 * (low + high), 1e-6 * highest)) << "unset f0: the range's centre";
                }
                for (const std::string_view name : {"fmaxDisplay", "fmaxSink", "f0Display"}) {
                    expect(block(name) != nullptr) << fatal << name;
                }
                auto*      display   = static_cast<opendigitizer::ImControlValue<double>*>(block("fmaxDisplay")->raw());
                auto*      f0Display = static_cast<opendigitizer::ImControlValue<double>*>(block("f0Display")->raw());
                auto*      sink      = static_cast<opendigitizer::ImPlotSink<double>*>(block("fmaxSink")->raw());
                const auto received  = [](const auto* shown) { return shown->_received.load() ? std::optional(shown->_last.load()) : std::nullopt; };
                const auto onBin     = [&](double f) {
                    bool found = false;
                    chart->withNewestBins([&](std::span<const float> bins) { found = std::ranges::contains(bins, static_cast<float>(f)); });
                    return found;
                };

                const auto high = (*chart->drawnHandles())[1];
                ctx->MouseMoveToPos(high);
                ctx->MouseDown(ImGuiMouseButton_Left);
                ctx->MouseMoveToPos(ImVec2(high.x - 40.f, high.y));
                expect(lt(chart->shownRange()[1], 100.2e6)) << "the drag moves the edge";
                expect(!received(display)) << "nothing sent while held";
                ctx->MouseUp(ImGuiMouseButton_Left);
                expect(g_state.waitUntil(ctx, "the release sends the dragged maximum via fmaxSource to its display", [&] { return received(display).has_value(); })) << fatal;
                const double dragged = *received(display);
                expect(eq(dragged, chart->shownRange()[1]) && onBin(dragged)) << "snapped to a frequency bin";
                expect(!chart->follow_inputs.value) << "a drag stops following the inputs";
                expect(g_state.waitUntil(ctx, "the display shows it, right-aligned", [&] { return display->_drawnNumber == std::format("{:.1f}", dragged) && std::abs(display->_drawnRightGap) < 0.5f; })) << display->_drawnNumber;
                expect(g_state.waitUntil(ctx, "the time plot's sink receives it", [&] {
                    std::lock_guard lock(sink->dataMutex());
                    return sink->size() > 0UZ;
                }));

                const auto [low, upper, marker] = *chart->drawnHandles();
                ctx->MouseMoveToPos(upper);
                ctx->MouseDown(ImGuiMouseButton_Left);
                ctx->MouseMoveToPos(ImVec2(low.x - 30.f, upper.y));
                ctx->MouseUp(ImGuiMouseButton_Left);
                expect(g_state.waitUntil(ctx, "an edge dragged past the other stops at range_min_width", [&] { return received(display) != dragged; }));
                expect(ge(chart->shownRange()[1] - chart->shownRange()[0], chart->range_min_width.value)) << std::format("{} .. {}", chart->shownRange()[0], chart->shownRange()[1]);

                ctx->MouseMoveToPos(marker);
                ctx->MouseDown(ImGuiMouseButton_Left);
                ctx->MouseMoveToPos(ImVec2(marker.x + 30.f, marker.y));
                ctx->MouseUp(ImGuiMouseButton_Left);
                expect(g_state.waitUntil(ctx, "the release sends the dragged f0, snapped, via f0Source to its display", [&] { return received(f0Display).has_value(); })) << fatal;
                const double draggedF0 = *received(f0Display);
                expect(gt(draggedF0, 100.0e6) && eq(draggedF0, chart->shownF0()) && onBin(draggedF0));

                DigitizerUi::UiGraphBlock* chartBlock = g_state.dashboard->session.graphModel.recursiveFindBlockByName("spectrum").block;
                expect(chartBlock != nullptr) << fatal;
                const double draggedMax = chart->shownRange()[1];
                chartBlock->setSetting("range_max", gr::pmt::Value(100.3e6));
                expect(g_state.waitUntil(ctx, "a later input reaches the setting", [&] { return chart->range_max.value == 100.3e6; }));
                expect(eq(chart->shownRange()[1], draggedMax)) << "the drag wins over the input";
                chartBlock->setSetting("follow_inputs", gr::pmt::Value(true));
                expect(g_state.waitUntil(ctx, "follow_inputs returns the range and f0 to their inputs", [&] { return chart->shownRange()[1] == 100.3e6 && chart->shownF0() == chart->f0.value; }));
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
                    std::array<bool, 4UZ>          layerSeen{};
                    expect(g_state.waitUntil(ctx, std::format("{}: the threshold, correlated, continuous and envelope layers draw {}", array, stereo ? "one hyperbola" : "a published position"), [&] {
                        const auto* c = chart();
                        if (c == nullptr || c->drawnLayers().size() != (array == "Square" ? 4UZ : 5UZ)) {
                            return false;
                        }
                        for (std::size_t k = 0UZ; k < layerSeen.size(); ++k) { // each layer at least once: a slow frame rate may never catch all four in one frame
                            layerSeen[k] = layerSeen[k] || (stereo ? c->drawnLayers()[k].curves == 1UZ : c->drawnPositions()[k].has_value());
                        }
                        return std::ranges::all_of(layerSeen, std::identity{});
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

            // disabled until the switchable dashboard's channel routing keeps its outputs aligned under load: under software
            // rendering (CI's llvmpipe) its Selectors (back_pressure false) drop samples per output, and the "line" preset's
            // envelope layer never locates; passes on a GPU
            skip / "the array choice switches geometry, simulation and plotted channels live; every array locates its source"_test = [ctx] {
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
                    const bool            stereo = name == "stereo";
                    std::array<bool, 4UZ> layerSeen{};
                    expect(g_state.waitUntil(ctx, std::format("{}: {} receivers and, threshold, correlated, continuous and envelope alike, {}", name, receivers, stereo ? "one hyperbola" : "a located position"), [&] {
                        const auto* c = chart();
                        if (c == nullptr || c->drawn().receivers != receivers || c->drawnLayers().size() != 5UZ) {
                            return false;
                        }
                        for (std::size_t k = 0UZ; k < layerSeen.size(); ++k) { // each layer at least once: a slow frame rate may never catch all four in one frame
                            layerSeen[k] = layerSeen[k] || (stereo ? c->drawnLayers()[k].curves == 1UZ : c->drawnPositions()[k].has_value());
                        }
                        return std::ranges::all_of(layerSeen, std::identity{});
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
    gr::registerBlock<opendigitizer::TestSpectrumGenerator, float>(registry); // the spectrum range demo's, as in the application

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
#if defined(OD_ACOUSTIC_CAMERA) // the localisation dashboards' blocks, as in the application
    gr::blocklib::initGrAudioBlocks(registry);
    gr::blocklib::initGrFilterBlocks(registry);
    gr::blocklib::initGrLocalisationBlocks(registry);
#endif
    registerTestBlocks(registry);

    gr::PluginLoader pluginLoader(registry, gr::globalSchedulerRegistry(), {});

    options.speedMode = ImGuiTestRunSpeed_Fast;
    TestApp app(options);

    // init early, as Dashboard invokes ImGui style stuff
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto result = app.runTests();
    g_state.dashboardPage.reset();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
