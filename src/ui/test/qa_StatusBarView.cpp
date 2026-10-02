#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <LogHistory.hpp>
#include <StatusBarView.hpp>
#include <components/ImGuiNotify.hpp>
#include <components/ViewModeBlocker.hpp>

#include <cmrc/cmrc.hpp>

#include <format>
#include <memory>
#include <string>
#include <thread>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct ForeignStatusBlock : gr::Block<ForeignStatusBlock, gr::Drawable<gr::UICategory::StatusBar, "Qt">> {
    using Description = gr::Doc<"status-bar block of another toolkit, which the ImGui status bar must not draw">;

    inline static std::size_t drawCount = 0UZ;

    GR_MAKE_REFLECTABLE(ForeignStatusBlock);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        ++drawCount;
        return gr::work::Status::OK;
    }
};

struct TestStatusText : gr::Block<TestStatusText, gr::Drawable<gr::UICategory::StatusBar, "Dear ImGui">> {
    using Description = gr::Doc<"test-only status-bar block that draws its name">;

    GR_MAKE_REFLECTABLE(TestStatusText);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        ImGui::TextUnformatted(name.value.c_str());
        return gr::work::Status::OK;
    }
};

namespace {
constexpr ImVec2 kHostPos{100.f, 80.f};
constexpr ImVec2 kHostSize{900.f, 500.f};
constexpr int    kMaxFrames = 600;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::StatusBarView              view{DigitizerUi::logHistory()};
    bool                                    viewModeLocked = false; // the App's input blocker covers everything above the bar
};

TestState* g_state = nullptr;

// ImGuiTestItemInfo::DebugLabel keeps 32 characters: the messages in these tests are short enough to stay visible in it
std::string barLabel(ImGuiTestContext* ctx) { return ctx->ItemInfo("**/###statusLine").DebugLabel; }

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

std::vector<std::string> statusBlockNames() {
    std::vector<std::string> names;
    for (const auto& block : g_state->view.blocks()) {
        names.emplace_back(block->name());
    }
    return names;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "statusbarview", "log, notifications and status-bar blocks");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *g_state;
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            {
                IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                state.dashboard->handleMessages();
                ImGui::Dummy(ImVec2(0.f, ImGui::GetContentRegionAvail().y - DigitizerUi::StatusBarView::height()));
                state.view.draw(state.dashboard->isInitialised ? state.dashboard.get() : nullptr);
            }
            if (state.viewModeLocked) {
                std::ignore = DigitizerUi::components::drawViewModeBlocker(ImRect(kHostPos, kHostPos + kHostSize - ImVec2(0.f, DigitizerUi::StatusBarView::height())));
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *g_state;
            for (int frame = 0; frame < kMaxFrames && !(state.dashboard->isInitialised && state.dashboard->graphModel.topologyGeneration > 0UZ); ++frame) {
                ctx->Yield();
            }
            ctx->Yield(3);

            "a warning logged from a worker thread shows in the bar with its count"_test = [&] {
                DigitizerUi::logHistory().clear();
                std::thread([] { gr::log::warning("pump overheated"); }).join();
                ctx->Yield(2);
                const std::string label = barLabel(ctx);
                expect(contains(label, "pump overheated")) << label;
                expect(contains(label, "W 1")) << label;
            };

            "a notification shows in the bar, also when a host takes the notifications"_test = [&] {
                DigitizerUi::logHistory().clear();
                DigitizerUi::components::Notification::error("bad yaml");
                ctx->Yield(2);
                expect(contains(barLabel(ctx), "bad yaml") && contains(barLabel(ctx), "E 1")) << barLabel(ctx);

                std::string sunk;
                DigitizerUi::components::Notification::sink = [&sunk](ImGuiToastType, std::string_view text) { sunk = text; };
                DigitizerUi::components::Notification::warning("disk full");
                DigitizerUi::components::Notification::sink = nullptr;
                ctx->Yield(2);
                expect(sunk == "disk full") << "the host's sink still receives it";
                expect(contains(barLabel(ctx), "disk full") && contains(barLabel(ctx), "W 1")) << barLabel(ctx);
            };

            "in locked View mode the bar opens the log, and clear empties it"_test = [&] {
                state.viewModeLocked = true;
                DigitizerUi::logHistory().clear();
                gr::log::warning("first");
                gr::log::error("second");
                ctx->Yield(2);
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow); // as a real click: whatever window is on top receives it
                ctx->Yield(2);
                expect(ctx->ItemExists("**/Clear")) << fatal << "the log popup is open";
                ctx->ItemClick("**/Clear", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->Yield(2);
                expect(DigitizerUi::logHistory().snapshot().empty()) << "history cleared";
                expect(contains(barLabel(ctx), "E 0  W 0")) << barLabel(ctx);
                ctx->KeyPress(ImGuiKey_Escape);
                state.viewModeLocked = false;
            };

            "the flowgraph's ImGui status-bar blocks are drawn in graph order, those of other toolkits are not"_test = [&] {
                expect(statusBlockNames() == std::vector<std::string>{"status_b_indicator", "status_a_text"}) << std::format("{}", statusBlockNames());
                expect(eq(ForeignStatusBlock::drawCount, 0UZ));
                expect(ctx->ItemExists("**/###schedulerState")) << "the indicator is drawn";
            };

            "the indicator shows the scheduler's state"_test = [&] {
                for (int frame = 0; frame < kMaxFrames && state.dashboard->scheduler->state() != gr::lifecycle::State::RUNNING; ++frame) {
                    ctx->Yield();
                }
                ctx->Yield(2);
                expect(ctx->ItemInfo("**/###schedulerState").DebugLabel == std::string("RUNNING###schedulerState")) << ctx->ItemInfo("**/###schedulerState").DebugLabel;

                expect(state.dashboard->scheduler->stop().has_value());
                for (int frame = 0; frame < kMaxFrames && state.dashboard->scheduler->state() != gr::lifecycle::State::STOPPED; ++frame) {
                    ctx->Yield();
                }
                std::string label;
                for (int frame = 0; frame < kMaxFrames && label != "STOPPED###schedulerState"; ++frame) { // the blocks reach STOPPED with their scheduler
                    ctx->Yield();
                    label = ctx->ItemInfo("**/###schedulerState").DebugLabel;
                }
                expect(label == std::string("STOPPED###schedulerState")) << label;
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "status_bar_view";
    TestApp app(options);
    auto    restClient = std::make_shared<opencmw::client::RestClient>();

    app.initImGui(); // also runs DigitizerUi::initialise(): log capture and notification recording

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    std::ignore = gr::registerBlock<ForeignStatusBlock>(registry);
    std::ignore = gr::registerBlock<TestStatusText>(registry);

    auto grcFile    = cmrc::ui_test_assets::get_filesystem().open("examples/qa_statusbar.grc");
    state.dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("status bar"));
    state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { state.dashboard->emplaceGraph(std::move(graph)); });

    const bool result = app.runTests();
    state.dashboard.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
