#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include <FlowgraphPage.hpp>
#include <GraphModel.hpp>
#include <LogHistory.hpp>
#include <Scheduler.hpp>
#include <StatusBarView.hpp>
#include <ToolbarView.hpp>

#include <chrono>
#include <format>
#include <string>
#include <vector>

using namespace boost;
using namespace boost::ut;
using gr::lifecycle::State;

namespace {
constexpr int kMaxFrames = 600;

// a host's own flowgraph: no Dashboard, no chart layout
constexpr std::string_view kHostGrc = R"(blocks:
  - id: gr::basic::ClockSource
    parameters:
      name: ClockSource1
      n_samples_max: 0
      sample_rate: 4096
  - id: gr::basic::SignalGenerator<float32>
    parameters:
      name: SignalGenerator1
      sample_rate: 4096
  - id: DigitizerUi::ToolbarButton
    parameters:
      name: host_button
      label: Amplify
      target_block: SignalGenerator1
      payload:
        amplitude: 7.0
  - id: DigitizerUi::SchedulerStateIndicator
    parameters:
      name: host_indicator
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: HostSink
connections:
  - [ClockSource1, 0, SignalGenerator1, 0]
  - [SignalGenerator1, 0, HostSink, 0]
)";

// what a host runs instead of a Dashboard: a scheduler and the graph model that mirrors it, wired together
struct HostGraph {
    DigitizerUi::Scheduler    scheduler;
    DigitizerUi::UiGraphModel graphModel;

    HostGraph() {
        graphModel.sendMessage_ = [this](gr::Message message, std::source_location location) { scheduler.sendMessage(std::move(message), location); };
        auto graph              = gr::loadGrc(gr::globalPluginLoader(), kHostGrc);
        expect(graph.has_value()) << fatal << "the host's .grc loads";
        scheduler.emplaceGraph(std::move(**graph));
    }
};

struct HostTestState {
    std::unique_ptr<HostGraph>                  host;
    DigitizerUi::ToolbarView                    toolbar;
    std::unique_ptr<DigitizerUi::StatusBarView> statusBar;
    std::unique_ptr<DigitizerUi::FlowgraphPage> editor;
};

HostTestState* g_state = nullptr;
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "hostcomposition", "toolbar, status bar and editor without a dashboard");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *g_state;
            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(ImVec2(1200.f, 800.f));
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            state.host->scheduler.handleMessages(state.host->graphModel); // the host pumps the messages each frame
            state.toolbar.draw(state.host->scheduler, state.host->graphModel, true);
            {
                IMW::Child editorArea("##editor", ImVec2(0.f, -DigitizerUi::StatusBarView::height()), false, ImGuiWindowFlags_NoScrollbar);
                state.editor->draw();
            }
            state.statusBar->draw(&state.host->scheduler, &state.host->graphModel);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&      state = *g_state;
            const auto reach = [&](State target) {
                // bounded by time, not frames: test frames run much faster than a start dispatched to the IO pool
                for (const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10); state.host->scheduler->state() != target && std::chrono::steady_clock::now() < deadline;) {
                    ctx->Yield();
                }
                ctx->Yield(2);
                return state.host->scheduler->state() == target;
            };

            "the toolbar draws the graph's toolbar block and the scheduler controls, which drive the scheduler"_test = [&] {
                expect(reach(State::RUNNING)) << fatal;
                expect(eq(state.toolbar.blocks().size(), 1UZ)) << fatal;
                expect(std::string(state.toolbar.blocks().front()->name()) == "host_button");
                expect(ctx->ItemExists("**/Amplify")) << "the toolbar block is drawn";
                ctx->ItemClick("**/###schedulerPause");
                expect(reach(State::PAUSED)) << "pause";
                ctx->ItemClick("**/###schedulerPlay");
                expect(reach(State::RUNNING)) << "play";
            };

            "the status bar draws the graph's status-bar block"_test = [&] {
                expect(eq(state.statusBar->blocks().size(), 1UZ));
                expect(ctx->ItemInfo("**/###schedulerState").DebugLabel == std::string("RUNNING###schedulerState")) << ctx->ItemInfo("**/###schedulerState").DebugLabel;
            };

            "a settings change reaches the host's graph model and keeps the setting's meta information"_test = [&] {
                const auto generator = [&] { return state.host->graphModel.recursiveFindBlockByName("SignalGenerator1").block; };
                const auto amplitude = [&] {
                    auto* block = generator();
                    if (block == nullptr) {
                        return 0.f;
                    }
                    const auto value = block->blockSettings.find_value(std::string("amplitude"), std::pmr::get_default_resource());
                    return value ? value->value_or(0.f) : 0.f;
                };
                for (int frame = 0; frame < kMaxFrames && (generator() == nullptr || generator()->blockSettingsMetaInformation.empty()); ++frame) {
                    ctx->Yield();
                }
                expect(generator() != nullptr && generator()->blockSettingsMetaInformation.contains("amplitude")) << fatal << "meta information known before the change";
                ctx->ItemClick("**/Amplify");
                for (int frame = 0; frame < kMaxFrames && amplitude() != 7.f; ++frame) {
                    ctx->Yield();
                }
                expect(eq(amplitude(), 7.f)) << "the new value reached the graph model";
                expect(generator()->blockSettingsMetaInformation.contains("amplitude")) << "the meta information is still there";
            };

            "the flowgraph editor edits the host's graph model"_test = [&] {
                for (int frame = 0; frame < kMaxFrames && state.editor->editorCount() == 0UZ; ++frame) {
                    ctx->Yield();
                }
                expect(eq(state.editor->editorCount(), 1UZ)) << "an editor opens once the graph model knows the graph";
                expect(static_cast<bool>(state.host->graphModel.recursiveFindBlockByName("host_indicator"))) << "and it holds the host's blocks";
            };
        };
    }
};

int main(int argc, char* argv[]) {
    HostTestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "host_composition";
    TestApp app(options);
    app.initImGui(); // DigitizerUi::initialise(): registers the toolbar and status-bar blocks
    gr::blocklib::initGrBasicBlocks(gr::globalBlockRegistry());

    state.host      = std::make_unique<HostGraph>();
    state.statusBar = std::make_unique<DigitizerUi::StatusBarView>(DigitizerUi::logHistory());
    state.editor    = std::make_unique<DigitizerUi::FlowgraphPage>(); // no RestClient: a host without opencmw objects
    state.editor->setGraphModel(std::addressof(state.host->graphModel));

    const bool result = app.runTests();
    state.editor.reset();
    state.host.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
