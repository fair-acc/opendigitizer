#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"
#include "imgui.h"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#if defined(OD_ACOUSTIC_CAMERA)
#include <gnuradio-4.0/GrAudioBlocks.hpp>
#include <gnuradio-4.0/GrFilterBlocks.hpp>
#include <gnuradio-4.0/GrLocalisationBlocks.hpp>
#endif
#include <gnuradio-4.0/Scheduler.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <ToolbarView.hpp>

#include "TestingBlockInspectionUtils.hpp"
#include "blocks/ImControlCamera.hpp"
#include "blocks/TestSpectrumGenerator.hpp"
#include "charts/ImageChart.hpp"
#include "charts/TensorRowsChart.hpp"

#include <opendigitizer/video/BrowserMicrophonePairs.hpp>
#include <opendigitizer/video/TestCard.hpp>

#include <cmrc/cmrc.hpp>

#include <array>
#include <cmath>
#include <memory>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestState : public opendigitizer::test::TestDashboardRunner {
    std::shared_ptr<DigitizerUi::DashboardPage> dashboardPage;
    DigitizerUi::ToolbarView                    toolbar;
};

TestState g_state;

namespace {
[[nodiscard]] opendigitizer::charts::ImageChart* chartNamed(std::string_view name);

[[nodiscard]] std::vector<opendigitizer::charts::ImageChart*> imageCharts() {
    std::vector<opendigitizer::charts::ImageChart*> charts;
    for (auto& window : g_state.dashboard->uiWindows) {
        if (window.block && window.block->typeName().find("ImageChart") != std::string_view::npos) {
            charts.push_back(static_cast<opendigitizer::charts::ImageChart*>(window.block->raw()));
        }
    }
    return charts;
}

[[nodiscard]] const opendigitizer::charts::TensorRowsChart* rowsChart() {
    const auto window = std::ranges::find_if(g_state.dashboard->uiWindows, [](const auto& w) { return w.block && w.block->typeName().find("TensorRowsChart") != std::string_view::npos; });
    return window == g_state.dashboard->uiWindows.end() ? nullptr : static_cast<const opendigitizer::charts::TensorRowsChart*>(window->block->raw());
}

opendigitizer::charts::ImageChart* chartNamed(std::string_view name) {
    const auto charts = imageCharts();
    const auto it     = std::ranges::find_if(charts, [name](const auto* chart) { return chart->chart_name.value == name; });
    return it == charts.end() ? nullptr : *it;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "image_chart", "ImageChart::draw");
        t->SetVarsDataType<opendigitizer::test::TestDashboardRunner>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(800, 400));
            if (g_state.dashboard) {
                if (!g_state.dashboardPage) {
                    g_state.dashboardPage = std::make_shared<DigitizerUi::DashboardPage>();
                    g_state.dashboardPage->setDashboard(*g_state.dashboard);
                }
                if (g_state.dashboard->isInitialised) {
                    g_state.toolbar.draw(g_state.dashboard->session, false);
                }
                g_state.dashboardPage->draw();
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_image_chart.grc");
            g_state.waitForScheduler(ctx);

            "Tensor frames reach the image charts through the GR4 stream"_test = [ctx] {
                ctx->SetRef("Test Window");
                using opendigitizer::charts::SinkRegistry;
                for (const std::string_view sinkName : {"rgbSink", "lumaSink"}) {
                    const auto sink = SinkRegistry::instance().findSink([sinkName](const auto& s) { return s.name() == sinkName; });
                    expect(sink != nullptr) << sinkName;
                    if (sink) {
                        expect(g_state.waitUntil(ctx, std::format("{} holds a frame", sinkName), [&sink] {
                            auto guard = sink->dataGuard();
                            return sink->imageFrame().generation > 0U;
                        }));
                        auto       guard = sink->dataGuard();
                        const auto frame = sink->imageFrame();
                        expect(eq(frame.rows, 48UZ) && eq(frame.columns, 64UZ)) << sinkName;
                        expect(sink->signalKind() == opendigitizer::SignalKind::Image2D) << sinkName;
                    }
                }
                expect(g_state.waitUntil(ctx, "all camera charts show a frame", [] {
                    return std::ranges::all_of(std::array{"Camera RGB", "Camera Luma", "Camera Luma CPU", "Tiles", "Overlay", "Overlay CPU"}, [](const char* name) {
                        const auto* chart = chartNamed(name);
                        return chart != nullptr && std::ranges::all_of(chart->layers(), [](const auto& layer) { return layer.shown > 0U && layer.rows == 48UZ; }) && !chart->layers().empty();
                    });
                }));
                const auto* rgb  = chartNamed("Camera RGB");
                const auto* luma = chartNamed("Camera Luma");
                const auto* cpu  = chartNamed("Camera Luma CPU");
                expect(rgb != nullptr && luma != nullptr && cpu != nullptr) << fatal;
                expect(rgb->rendersOnGpu() && luma->rendersOnGpu() && !cpu->rendersOnGpu());
                captureScreenshot(*ctx);
            };

            "the test card's frame barcode is read back from the screen"_test = [ctx] {
                namespace test_card = opendigitizer::video::test_card;
                const auto* chart   = chartNamed("Camera RGB");
                expect(chart != nullptr) << fatal;
                std::vector<std::uint64_t> barcodes;
                expect(g_state.waitUntil(ctx, "four successive frames read from the screen", [&] {
                    const CapturedPixels screen = capturePixels(*ctx, ImRect({0.f, 0.f}, ImGui::GetIO().DisplaySize));
                    const auto           value  = test_card::readBarcode({64UZ, 48UZ}, [&](std::size_t x, std::size_t y) {
                        const ImVec2 at = chart->screenPosition(static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5);
                        return screen.rgba[static_cast<std::size_t>(at.y) * static_cast<std::size_t>(screen.width) + static_cast<std::size_t>(at.x)];
                    });
                    if (value && (barcodes.empty() || *value != barcodes.back())) {
                        barcodes.push_back(*value);
                    }
                    return barcodes.size() >= 4UZ;
                }));
                expect(std::ranges::adjacent_find(barcodes, std::greater_equal{}) == barcodes.end()) << std::format("frame sequence advances: {}", barcodes);
            };

            "several inputs are tiled or overlaid"_test = [] {
                const auto* tiles   = chartNamed("Tiles");
                const auto* overlay = chartNamed("Overlay");
                expect(tiles != nullptr && overlay != nullptr) << fatal;
                expect(eq(tiles->layers().size(), 2UZ) && eq(overlay->layers().size(), 2UZ));
                expect(eq(tiles->tileOrigin(0UZ).x, 0.0) && gt(tiles->tileOrigin(1UZ).x, 64.0) && eq(tiles->tileOrigin(1UZ).y, 0.0)) << "second tile right of the first";
                expect(eq(overlay->tileOrigin(1UZ).x, 0.0)) << "layers share the origin";
                const auto [lo, hi] = overlay->transparencyRange(1UZ);
                expect(eq(lo, 0.5) && std::isinf(hi) && std::isinf(overlay->transparencyRange(0UZ).first)) << "per-layer range, NaN = none";
                const auto& luma        = overlay->layers()[1];
                const auto  transparent = std::ranges::count_if(luma.rgba, [](std::uint32_t p) { return (p >> 24U) == 0U; });
                expect(gt(transparent, 0) && lt(transparent, static_cast<std::ptrdiff_t>(luma.rgba.size()))) << "pixels below the range are transparent, others not";
            };

            "the CPU fallback leaves an overlay's out-of-range cells transparent"_test = [ctx] {
                const auto* chart = chartNamed("Overlay CPU");
                expect(chart != nullptr && !chart->rendersOnGpu()) << fatal;
                const auto&          luma        = chart->layers()[1];
                const CapturedPixels screen      = capturePixels(*ctx, ImRect({0.f, 0.f}, ImGui::GetIO().DisplaySize));
                std::size_t          transparent = 0UZ, grey = 0UZ;
                for (std::size_t r = luma.rows / 2UZ; r < opendigitizer::video::test_card::Layout{luma.columns, luma.rows}.motionTop(); ++r) { // below the status text, above the moving strips
                    for (std::size_t c = 0UZ; c < luma.columns; ++c) {
                        if ((luma.rgba[r * luma.columns + c] >> 24U) != 0U) {
                            continue;
                        }
                        ++transparent;
                        const ImVec2       at      = chart->screenPosition(static_cast<double>(c) + 0.5, static_cast<double>(r) + 0.5);
                        const unsigned int p       = screen.rgba[static_cast<std::size_t>(at.y) * static_cast<std::size_t>(screen.width) + static_cast<std::size_t>(at.x)];
                        const auto         channel = [p](unsigned int shift) { return static_cast<int>((p >> shift) & 0xFFU); };
                        grey += std::abs(channel(0U) - channel(8U)) < 16 && std::abs(channel(8U) - channel(16U)) < 16 ? 1UZ : 0UZ; // the grey luma heatmap below, not the Hot colormap
                    }
                }
                expect(gt(transparent, 0UZ) && ge(grey * 10UZ, transparent * 9UZ)) << std::format("{} of {} cells above layer_max show the layer below", grey, transparent);
            };

            "TensorRowsChart draws each row of the luma frame as a trace"_test = [ctx] {
                expect(g_state.waitUntil(ctx, "48 traces drawn", [] {
                    const auto* chart = rowsChart();
                    return chart != nullptr && chart->drawnTraces() == 48UZ;
                }));
            };

            "a backlog shows every frame in order"_test = [ctx] {
                expect(g_state.waitUntil(ctx, "all 30 frames displayed", [] {
                    const auto* chart = chartNamed("Backlog");
                    return chart != nullptr && !chart->layers().empty() && chart->layers().front().displayed == 30U;
                }));
                expect(eq(chartNamed("Backlog")->layers().front().skipped, 0U));
                captureScreenshot(*ctx);
            };

            "the camera control receives the camera's devices and status"_test = [ctx] {
                auto&                            model = g_state.dashboard->session.graphModel;
                DigitizerUi::DrawableBlockConfig config;
                expect(g_state.waitUntil(ctx, "camera settings reach the control", [&] {
                    const DigitizerUi::UiGraphBlock* control = model.recursiveFindBlockByName("cameraControl").block;
                    if (control == nullptr) {
                        return false;
                    }
                    config = {};
                    model.aggregateAndApplyControlledProperties(config, *control);
                    const auto devices = config.firstControlledProperty.targetSettings.find_value(std::string_view("available_devices"));
                    return devices.has_value() && std::ranges::contains(DigitizerUi::stringList(*devices), std::string("Test card [test_card]"));
                }));
                expect(config.firstControlledProperty.targetSettings.contains("measured_frame_rate") && config.firstControlledProperty.targetSettings.contains("available_controls"));
                expect(eq(opendigitizer::video::deviceSelector("Test card [test_card]"), std::string("@id:test_card")));
            };

            "picking a device and a priority in the toolbar control sets them on the camera"_test = [ctx] {
                auto&                            session = g_state.dashboard->session;
                const DigitizerUi::UiGraphBlock* control = session.graphModel.recursiveFindBlockByName("cameraControl").block;
                expect(control != nullptr) << fatal;
                const auto cameraSetting = [&session](const char* key) { return opendigitizer::test::setting(session, "camera", key).value_or(std::string{}); };
                ctx->ItemClick(opendigitizer::test::controlItemId(ctx, "//Test Window/##Toolbar", control->blockUniqueName, "##device"));
                ctx->ItemClick("//$FOCUSED/Test card [test_card]");
                expect(g_state.waitUntil(ctx, "the picked device reaches the camera", [&] { return cameraSetting("device") == "@id:test_card"; }));
                ctx->ItemClick(opendigitizer::test::controlItemId(ctx, "//Test Window/##Toolbar", control->blockUniqueName, "...##cameraSettings"));
                for (const char* priority : {"rate", "requested"}) {
                    ctx->ItemClick("//$FOCUSED/priority");
                    ctx->ItemClick(std::format("//$FOCUSED/{}", priority).c_str());
                    expect(g_state.waitUntil(ctx, std::format("priority '{}' reaches the camera", priority), [&] { return cameraSetting("priority") == priority; }));
                }
                ctx->PopupCloseAll();
            };

            // built, not started: no camera or microphone needed; a .grc key that is no setting is dropped silently at load
            const auto keysResolve = [](std::string_view source, std::size_t microphones) {
                gr::PluginLoader loader(gr::globalBlockRegistry(), gr::globalSchedulerRegistry(), {});
                const auto       graph = gr::loadGrc(loader, source);
                expect(graph.has_value()) << fatal << (graph ? std::string{} : graph.error().message);
                const auto blocks = gr::pmt::yaml::deserialize(source).value_or(gr::property_map{}).value_or<gr::Tensor<gr::pmt::Value>>("blocks", {});
                expect(gt(blocks.size(), 0UZ));
                for (const gr::pmt::Value& definition : blocks) {
                    const auto parameters = definition.value_or(gr::property_map{}).value_or<gr::property_map>("parameters", {});
                    const auto name       = parameters.value_or<std::string>("name", "");
                    const auto block      = std::ranges::find_if((*graph)->blocks(), [&name](const auto& b) { return b->name() == name; });
                    expect(block != (*graph)->blocks().end()) << fatal << name;
                    const gr::property_map settings = (*block)->settings().get();
                    for (const auto& [key, _] : parameters) {
                        expect(key == "name" || settings.contains(key)) << std::format("{}: '{}' is no setting", name, std::string_view(key));
                    }
                    if (name == "acousticImage") {
                        const auto positions = settings.value_or<gr::Tensor<double>>("element_positions", gr::Tensor<double>{});
                        expect(positions.rank() == 2UZ && positions.extent(0UZ) == microphones && positions.extent(1UZ) == 3UZ) << std::format("element_positions {} for {} microphones", positions.extents(), microphones);
                    }
                }
            };

            "the camera dashboards' blocks, settings and connections resolve"_test = [&keysResolve] {
                for (const auto& file : {cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/XtraCameraDemo.grc"), cmrc::ui_test_assets::get_filesystem().open("examples/qa_image_chart.grc")}) {
                    keysResolve(std::string_view(file.begin(), file.end()), 0UZ);
                }
            };

            "the spectrum range demo's blocks, settings and connections resolve"_test = [&keysResolve] {
                const auto file = cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/SpectrumRangeDemo.grc");
                keysResolve(std::string_view(file.begin(), file.end()), 0UZ);
            };

#if defined(OD_ACOUSTIC_CAMERA)
            "the acoustic-camera dashboards' blocks, settings and connections resolve (native and browser)"_test = [&keysResolve] {
                const auto native = cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/XtraAcousticCamera.grc");
                keysResolve(std::string_view(native.begin(), native.end()), 16UZ);
                const auto browser = cmrc::ui_test_assets::get_filesystem().open("dashboards/browser/XtraAcousticCamera.grc");
                keysResolve(std::string_view(browser.begin(), browser.end()), 4UZ);
            };

#if defined(OD_TRIGGER_BLOCKS)
            "the localisation dashboards' blocks, settings and connections resolve (per array native and browser, and the switchable one)"_test = [&keysResolve] {
                for (const std::string_view array : {"Square", "Tetrahedron", "Line", "Uma16", "Stereo"}) {
                    for (const auto& file : {cmrc::sample_dashboards::get_filesystem().open(std::format("assets/sampleDashboards/XtraLocalisation{}.grc", array)), cmrc::ui_test_assets::get_filesystem().open(std::format("dashboards/browser/XtraLocalisation{}.grc", array))}) {
                        keysResolve(std::string_view(file.begin(), file.end()), 0UZ);
                    }
                }
                for (const auto& switchable : {cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/XtraLocalisation.grc"), cmrc::ui_test_assets::get_filesystem().open("dashboards/browser/XtraLocalisation.grc")}) {
                    keysResolve(std::string_view(switchable.begin(), switchable.end()), 0UZ);
                }
            };
            "the loudspeaker echo dashboard's blocks, settings and connections resolve"_test = [&keysResolve] {
                const auto file = cmrc::sample_dashboards::get_filesystem().open("assets/sampleDashboards/XtraLocalisationActive.grc");
                keysResolve(std::string_view(file.begin(), file.end()), 0UZ);
            };
#endif
#endif

            "a device setting switches the running camera, its status follows"_test = [ctx] {
                auto&                      session = g_state.dashboard->session;
                DigitizerUi::UiGraphBlock* camera  = session.graphModel.recursiveFindBlockByName("camera").block;
                expect(camera != nullptr) << fatal;
                camera->setSetting("device", gr::pmt::Value(std::string("no such camera")));
                expect(g_state.waitUntil(ctx, "the camera falls back to the test card", [&] { return !opendigitizer::test::setting(session, "camera", "fallback_reason").value_or(std::string{}).empty(); }));
                camera->setSetting("device", gr::pmt::Value(std::string("@id:test_card")));
                expect(g_state.waitUntil(ctx, "the requested test card replaces the fallback", [&] { return opendigitizer::test::setting(session, "camera", "fallback_reason").value_or(std::string{"?"}).empty(); }));
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "image_chart";

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    std::ignore = gr::registerBlock<opendigitizer::TestSpectrumGenerator, float>(registry); // the spectrum range demo's, as in the application
#if defined(OD_ACOUSTIC_CAMERA)                                                             // the localisation dashboards' blocks, as in the application
    gr::blocklib::initGrAudioBlocks(registry);
    gr::blocklib::initGrFilterBlocks(registry);
    gr::blocklib::initGrLocalisationBlocks(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::BrowserMicrophonePairs>(registry); // the browser variant's, built not started
#endif
    gr::PluginLoader pluginLoader(registry, gr::globalSchedulerRegistry(), {});

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);
    app.initImGui();

    auto loader = DigitizerUi::test::ImGuiTestApp::createPluginLoader();

    auto result = app.runTests();
    g_state.dashboardPage.reset();
    g_state.dashboard.reset(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
