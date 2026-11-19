#include "ImGuiTestApp.hpp"
#include "TestPropertySetterButton.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include <FlowgraphPage.hpp>
#include <GraphModel.hpp>
#include <GraphSession.hpp>
#include <LogHistory.hpp>
#include <StatusBarView.hpp>
#include <ToolbarView.hpp>
#include <cmrc/cmrc.hpp>
#include <common/LookAndFeel.hpp>
#include <gnuradio-4.0/meta/utils.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <vector>

using namespace gr::od;

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using gr::lifecycle::State;

namespace {
struct HostGraph {
    GraphSession session;

    HostGraph() {
        const auto grc   = cmrc::ui_test_assets::get_filesystem().open("examples/qa_host_composition.grc");
        auto       graph = gr::loadGrc(gr::globalPluginLoader(), std::string(grc.begin(), grc.end()));
        expect(graph.has_value()) << fatal << "the host's .grc loads";
        session.emplaceGraph(std::move(**graph));
    }
};

struct HostTestState {
    std::unique_ptr<HostGraph>     host;
    ToolbarView                    toolbar;
    std::unique_ptr<StatusBarView> statusBar;
    std::unique_ptr<FlowgraphPage> editor;
    std::optional<ImVec4>          hostBackground;
    ImRect                         editorRect;
};

bool isMagenta(unsigned int rgba) { return (rgba & 0xFFFFFFU) == 0xFF00FFU; } // exact: a faint grid line still changes a pixel

struct CanvasPixels {
    std::size_t interiorMagenta = 0UZ;
    std::size_t interiorArea    = 0UZ;
    std::size_t edgeNotMagenta  = 0UZ;
};

HostTestState* gState = nullptr;
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "hostcomposition", "toolbar, status bar and editor without a dashboard");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *gState;
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(ImVec2(1200.f, 800.f));
            const int nHostColours = state.hostBackground ? 2 : 0;
            if (state.hostBackground) {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, *state.hostBackground);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, *state.hostBackground);
            }
            gr::on_scope_exit popHostColours = [nHostColours] { ImGui::PopStyleColor(nHostColours); };
            imw::Window       window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            state.host->session.receiveMessages();
            state.toolbar.draw(state.host->session, true);
            {
                imw::Child editorArea("##editor", ImVec2(0.f, -StatusBarView::height()), false, ImGuiWindowFlags_NoScrollbar);
                state.editorRect = ImRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize());
                state.editor->draw();
            }
            state.statusBar->draw(&state.host->session);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&      state = *gState;
            const auto reach = [&](State target) {
                test::waitUntil(ctx, std::format("the scheduler is {}", gr::meta::enumName(target).value_or("?")), [&] { return state.host->session.state() == target; });
                ctx->Yield(); // drawn once in that state
                return state.host->session.state() == target;
            };

            "the toolbar draws the graph's toolbar block and the scheduler controls, which drive the scheduler"_test = [&] {
                expect(reach(State::RUNNING)) << fatal;
                expect(ctx->ItemExists("**/Amplify")) << "the toolbar block is drawn";
                ctx->ItemClick("**/###schedulerPause");
                expect(reach(State::PAUSED)) << "pause";
                ctx->ItemClick("**/###schedulerPlay");
                expect(reach(State::RUNNING)) << "play";
                ctx->ItemClick("**/###schedulerStop");
                expect(reach(State::STOPPED)) << "stop";
                ctx->ItemClick("**/###schedulerPlay");
                expect(reach(State::RUNNING)) << "running again for the next scenarios";
            };

            "the status bar draws the graph's status-bar block"_test = [&] {
                const auto label = [&] { return std::string(ctx->ItemInfo("**/###schedulerState").DebugLabel); };
                // the indicator shows its block's state, which follows the scheduler's restart by a few frames
                test::waitUntil(ctx, "the indicator shows RUNNING", [&] { return label() == "RUNNING###schedulerState"; });
                expect(label() == "RUNNING###schedulerState") << label();
            };

            "a settings change reaches the host's graph model and keeps the setting's meta information"_test = [&] {
                const auto generator = [&] { return state.host->session.graphModel.recursiveFindBlockByName("SignalGenerator1").block; };
                const auto amplitude = [&] {
                    auto* block = generator();
                    if (block == nullptr) {
                        return 0.f;
                    }
                    const auto value = block->blockSettings.find_value(std::string("amplitude"), std::pmr::get_default_resource());
                    return value ? value->value_or(0.f) : 0.f;
                };
                test::waitUntil(ctx, "the meta information is known", [&] { return generator() != nullptr && !generator()->blockSettingsMetaInformation.empty(); });
                expect(generator() != nullptr && generator()->blockSettingsMetaInformation.contains("amplitude")) << fatal << "meta information known before the change";
                ctx->ItemClick("**/Amplify");
                test::waitUntil(ctx, "the new value reached the graph model", [&] { return amplitude() == 7.f; });
                expect(eq(amplitude(), 7.f)) << "the new value reached the graph model";
                expect(generator()->blockSettingsMetaInformation.contains("amplitude")) << "the meta information is still there";
            };

            "the graph model tells the toolbar's control blocks apart, so an editor can hide them"_test = [&] {
                const auto isUiControl = [&](std::string_view name) {
                    const auto found = state.host->session.graphModel.recursiveFindBlockByName(name);
                    expect(static_cast<bool>(found)) << fatal << name;
                    return found.block->isUiControl();
                };
                expect(isUiControl("host_button")) << "Drawable<Toolbar>";
                expect(!isUiControl("host_indicator")) << "Drawable<StatusBar>";
                expect(!isUiControl("HostSink")) << "Drawable<Content>";
                expect(!isUiControl("ClockSource1")) << "not drawable";
            };

            "the flowgraph editor edits the host's graph model"_test = [&] {
                test::waitUntil(ctx, "an editor opens", [&] { return state.editor->editorCount() > 0UZ; });
                expect(eq(state.editor->editorCount(), 1UZ)) << "an editor opens once the graph model knows the graph";
                expect(static_cast<bool>(state.host->session.graphModel.recursiveFindBlockByName("host_indicator"))) << "and it holds the host's blocks";
            };

            "a host turns off the editor canvas's background, grid and border and sees its own background"_test = [&] {
                struct Layers {
                    bool background = true;
                    bool grid       = true;
                    bool border     = true;
                };
                state.hostBackground     = ImVec4(1.f, 0.f, 1.f, 1.f);
                const auto captureCanvas = [&](Layers layers) {
                    auto& flowgraph            = LookAndFeel::mutableInstance().flowgraph;
                    flowgraph.canvasBackground = layers.background;
                    flowgraph.canvasGrid       = layers.grid;
                    flowgraph.canvasBorder     = layers.border;
                    state.editor->updateStyle();
                    const CapturedPixels image  = captureSettledPixels(*ctx, state.editorRect);
                    constexpr int        kInset = 40;
                    CanvasPixels         pixels;
                    for (int y = 0; y < image.height; ++y) {
                        for (int x = 0; x < image.width; ++x) {
                            const bool magenta = isMagenta(image.rgba[static_cast<std::size_t>(y * image.width + x)]);
                            if (x == 0 || y == 0 || x == image.width - 1 || y == image.height - 1) {
                                pixels.edgeNotMagenta += magenta ? 0UZ : 1UZ;
                            } else if (x >= kInset && y >= kInset && x < image.width - kInset && y < image.height - kInset) {
                                pixels.interiorMagenta += magenta ? 1UZ : 0UZ;
                                ++pixels.interiorArea;
                            }
                        }
                    }
                    return pixels;
                };
                const CanvasPixels all        = captureCanvas(Layers{});
                const CanvasPixels none       = captureCanvas(Layers{.background = false, .grid = false, .border = false});
                const CanvasPixels gridOnly   = captureCanvas(Layers{.background = false, .grid = true, .border = false});
                const CanvasPixels borderOnly = captureCanvas(Layers{.background = false, .grid = false, .border = true});
                const Layers       defaults;
                auto&              flowgraph = LookAndFeel::mutableInstance().flowgraph;
                flowgraph.canvasBackground   = defaults.background;
                flowgraph.canvasGrid         = defaults.grid;
                flowgraph.canvasBorder       = defaults.border;
                state.editor->updateStyle();
                state.hostBackground.reset();

                expect(eq(all.interiorMagenta, 0UZ)) << "the default canvas background hides the host's";
                expect(none.interiorMagenta * 2UZ > none.interiorArea) << std::format("without the canvas layers most of the canvas shows the host: {} of {}", none.interiorMagenta, none.interiorArea);
                expect(gridOnly.interiorMagenta + none.interiorArea / 50UZ < none.interiorMagenta) << std::format("grid lines cover the host's background: {} magenta pixels with the grid, {} without", gridOnly.interiorMagenta, none.interiorMagenta);
                expect(borderOnly.edgeNotMagenta > none.edgeNotMagenta + 1000UZ) << std::format("the border lines the canvas edge: {} edge pixels not magenta with the border, {} without", borderOnly.edgeNotMagenta, none.edgeNotMagenta);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "host_composition";
    TestApp app(options);
    app.initImGui();
    HostTestState state;
    gState         = std::addressof(state);
    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    registerWithIdPrefix<test::TestPropertySetterButton>(registry, kOpendigitizerIdPrefix);

    state.host      = std::make_unique<HostGraph>();
    state.statusBar = std::make_unique<StatusBarView>();
    state.editor    = std::make_unique<FlowgraphPage>();
    state.editor->setGraphModel(std::addressof(state.host->session.graphModel));

    const bool result = app.runTests();
    return result ? 0 : 1;
}
