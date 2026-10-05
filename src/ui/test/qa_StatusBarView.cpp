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

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>

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

struct TestStatusText : gr::Block<TestStatusText, gr::Drawable<gr::UICategory::StatusBar, "ImGui">> {
    using Description = gr::Doc<"test-only status-bar block that draws its name as a button">;

    GR_MAKE_REFLECTABLE(TestStatusText);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        ImGui::SmallButton(name.value.c_str());
        return gr::work::Status::OK;
    }
};

namespace {
constexpr ImVec2 kHostPos{100.f, 80.f};
constexpr ImVec2 kHostSize{900.f, 500.f};
constexpr int    kMaxFrames = 600;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::StatusBarView              view;
    bool                                    viewModeLocked = false;
};

TestState* g_state = nullptr;

// DebugLabel keeps 32 characters: keep messages short
std::string barLabel(ImGuiTestContext* ctx) { return ctx->ItemInfo("**/###statusLine").DebugLabel; }

std::string testClipboard;

std::uint64_t nowNanos() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }

std::uint64_t countOf(gr::log::Level level) { return DigitizerUi::logHistory().counts()[static_cast<std::size_t>(level)]; }

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

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
                const bool withGraph = state.dashboard->isInitialised;
                state.view.draw(withGraph ? &state.dashboard->session : nullptr);
            }
            if (state.viewModeLocked) {
                std::ignore = DigitizerUi::components::drawViewModeBlocker(ImRect(kHostPos, kHostPos + kHostSize - ImVec2(0.f, DigitizerUi::StatusBarView::height())));
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&      state       = *g_state;
            const auto graphLoaded = [&] {
                const auto* scheduler = std::get_if<DigitizerUi::UiGraphBlock::SchedulerBlockInfo>(&state.dashboard->session.graphModel.rootBlock.blockCategoryInfo);
                return state.dashboard->isInitialised && scheduler && scheduler->childrenLoaded;
            };
            for (int frame = 0; frame < kMaxFrames && !graphLoaded(); ++frame) {
                ctx->Yield();
            }
            ctx->Yield(3);

            "loading a valid dashboard logs no warning or error, except for the Qt block it cannot show"_test = [&] {
                for (const gr::log::LogRecord& record : DigitizerUi::logHistory().snapshot()) {
                    const std::string_view text(record.text, record.textLength);
                    expect(record.level > gr::log::Level::warning || text.contains("ForeignStatusBlock")) << std::format("{}: {}", gr::meta::enumName(record.level).value_or("?"), text);
                }
            };

            "the info dot is lit for five seconds after the latest info or debug record"_test = [] {
                constexpr std::uint64_t kT0 = 1'000'000'000'000UZ;
                expect(DigitizerUi::StatusBarView::isInfoDotLit(kT0, kT0) && DigitizerUi::StatusBarView::isInfoDotLit(kT0, kT0 + 4'999'999'999UZ));
                expect(!DigitizerUi::StatusBarView::isInfoDotLit(kT0, kT0 + 5'000'000'000UZ));
                expect(!DigitizerUi::StatusBarView::isInfoDotLit(0U, kT0)) << "nothing logged";
            };

            "the history keeps the time of the latest info or debug record until cleared"_test = [] {
                DigitizerUi::logHistory().clear();
                gr::log::warning("not info");
                expect(eq(DigitizerUi::logHistory().latestInfoOrDebugNanos(), 0UZ));
                const std::uint64_t before = nowNanos();
                std::ignore                = DigitizerUi::logHistory().publish(gr::log::LogRecord{.level = gr::log::Level::info, .timestampNanos = nowNanos()});
                expect(ge(DigitizerUi::logHistory().latestInfoOrDebugNanos(), before));
                DigitizerUi::logHistory().clear();
                expect(eq(DigitizerUi::logHistory().latestInfoOrDebugNanos(), 0UZ));
            };

            "a warning logged from a worker thread shows in the bar with its count"_test = [&] {
                DigitizerUi::logHistory().clear();
                std::thread([] { gr::log::warning("pump overheated"); }).join();
                ctx->Yield(2);
                const std::string label = barLabel(ctx);
                expect(contains(label, "pump overheated")) << label;
                expect(eq(countOf(gr::log::Level::warning), 1UZ));
                expect(ctx->ItemExists("**/##errorDot") && ctx->ItemExists("**/##warningDot") && ctx->ItemExists("**/##infoDot"));
            };

            "a notification shows in the bar and as a toast"_test = [&] {
                DigitizerUi::logHistory().clear();
                const auto toastsBefore = ImGui::notifications.size();
                DigitizerUi::components::Notification::error("bad yaml");
                ctx->Yield(2);
                expect(contains(barLabel(ctx), "bad yaml") && eq(countOf(gr::log::Level::error), 1UZ)) << barLabel(ctx);
                expect(eq(ImGui::notifications.size(), toastsBefore + 1UZ)) << "a toast is queued too";
            };

            "in locked View mode the bar opens the log, and clear empties it"_test = [&] {
                state.viewModeLocked = true;
                DigitizerUi::logHistory().clear();
                gr::log::warning("first");
                gr::log::error("second");
                ctx->Yield(2);
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->Yield(2);
                expect(ctx->ItemExists("**/Clear")) << fatal << "the log popup is open";
                const ImGuiWindow* logWindow = ctx->ItemInfo("**/Clear").Window;
                const ImGuiWindow* barWindow = ctx->ItemInfo("**/###statusLine").Window;
                expect(std::abs(logWindow->Size.x - barWindow->Size.x) < 1.f && std::abs(logWindow->Pos.x - barWindow->Pos.x) < 1.f) << std::format("the log spans the bar: {} px at {}, bar {} px at {}", logWindow->Size.x, logWindow->Pos.x, barWindow->Size.x, barWindow->Pos.x);
                ctx->ItemClick("**/Clear", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->Yield(2);
                expect(DigitizerUi::logHistory().snapshot().empty()) << "history cleared";
                expect(eq(countOf(gr::log::Level::error) + countOf(gr::log::Level::warning), 0UZ));
                ctx->PopupCloseAll();
                state.viewModeLocked = false;
            };

            "the log copies all records, or the selected ones, to the clipboard"_test = [&] {
                ImGuiPlatformIO& platform      = ImGui::GetPlatformIO();
                const auto       desktopGetter = std::exchange(platform.Platform_GetClipboardTextFn, [](ImGuiContext*) -> const char* { return testClipboard.c_str(); });
                const auto       desktopSetter = std::exchange(platform.Platform_SetClipboardTextFn, [](ImGuiContext*, const char* text) { testClipboard = text; });
                state.viewModeLocked           = true;
                DigitizerUi::logHistory().clear();
                gr::log::warning("alpha");
                gr::log::warning("beta");
                gr::log::warning("gamma");
                ctx->Yield(2);
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->Yield(2);
                expect(ctx->ItemExists("**/###copyLog")) << fatal << "the log popup is open";
                ctx->ItemClick("**/###copyLog", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                const std::string all = ImGui::GetClipboardText();
                expect(contains(all, "alpha") && contains(all, "beta") && contains(all, "gamma") && contains(all, "warning")) << all;
                ctx->ItemClick("**/###record0", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyDown(ImGuiMod_Ctrl);
                ctx->ItemClick("**/###record2", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyUp(ImGuiMod_Ctrl);
                ctx->ItemClick("**/###copyLog", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                const std::string selected = ImGui::GetClipboardText();
                expect(contains(selected, "alpha") && !contains(selected, "beta") && contains(selected, "gamma")) << selected;
                ctx->ItemClick("**/###record1", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
                const std::string shortcut = ImGui::GetClipboardText();
                expect(!contains(shortcut, "alpha") && contains(shortcut, "beta") && !contains(shortcut, "gamma")) << "Ctrl+C: " << shortcut;
                ctx->PopupCloseAll();
                platform.Platform_GetClipboardTextFn = desktopGetter;
                platform.Platform_SetClipboardTextFn = desktopSetter;
                state.viewModeLocked                 = false;
            };

            "the flowgraph's ImGui status-bar blocks are drawn in graph order, those of other toolkits are not"_test = [&] {
                const ImGuiTestItemInfo indicator = ctx->ItemInfo("**/###schedulerState");
                const ImGuiTestItemInfo text      = ctx->ItemInfo("**/status_a_text");
                expect(indicator.ID != 0 && text.ID != 0) << fatal << "both ImGui blocks are drawn";
                expect(indicator.RectFull.Max.x <= text.RectFull.Min.x) << std::format("graph order, not by name: the indicator ends at {}, the text starts at {}", indicator.RectFull.Max.x, text.RectFull.Min.x);
                expect(eq(ForeignStatusBlock::drawCount, 0UZ)) << "the Qt block is never drawn";
            };

            "the log and blocks take nine tenths of the bar, the version the rest"_test = [&] {
                const ImGuiTestItemInfo logLine = ctx->ItemInfo("**/###statusLine");
                const ImGuiTestItemInfo text    = ctx->ItemInfo("**/status_a_text");
                expect(logLine.ID != 0 && text.ID != 0) << fatal;
                const float contentLeft  = logLine.Window->Pos.x + ImGui::GetStyle().WindowPadding.x;
                const float contentRight = logLine.Window->Pos.x + logLine.Window->Size.x - ImGui::GetStyle().WindowPadding.x;
                const float nineTenths   = contentLeft + .9f * (contentRight - contentLeft);
                expect(std::abs(text.RectFull.Max.x - nineTenths) <= ImGui::GetStyle().ItemSpacing.x + 1.f) << std::format("the blocks end at {}, nine tenths at {}", text.RectFull.Max.x, nineTenths);
            };

            "the indicator shows the scheduler's state"_test = [&] {
                for (int frame = 0; frame < kMaxFrames && state.dashboard->session.state() != gr::lifecycle::State::RUNNING; ++frame) {
                    ctx->Yield();
                }
                ctx->Yield(2);
                expect(ctx->ItemInfo("**/###schedulerState").DebugLabel == std::string("RUNNING###schedulerState")) << ctx->ItemInfo("**/###schedulerState").DebugLabel;

                state.dashboard->session.stop();
                for (int frame = 0; frame < kMaxFrames && state.dashboard->session.state() != gr::lifecycle::State::STOPPED; ++frame) {
                    ctx->Yield();
                }
                std::string label;
                for (int frame = 0; frame < kMaxFrames && label != "STOPPED###schedulerState"; ++frame) {
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

    app.initImGui();

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    std::ignore = gr::registerBlock<ForeignStatusBlock>(registry);
    std::ignore = gr::registerBlock<TestStatusText>(registry);

    auto grcFile    = cmrc::ui_test_assets::get_filesystem().open("examples/qa_statusbar.grc");
    state.dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("status bar"));
    state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { state.dashboard->session.emplaceGraph(std::move(graph)); });

    const bool result = app.runTests();
    state.dashboard.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
