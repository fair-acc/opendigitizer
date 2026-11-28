#include "App.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

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
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using namespace gr::od;

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

struct TestState : test::TestDashboardRunner {
    StatusBarView view;
    bool          viewModeLocked = false;
};

TestState* gState = nullptr;

// DebugLabel keeps 32 characters: keep messages short
std::string barLabel(ImGuiTestContext* ctx) { return ctx->ItemInfo("**/###statusLine").DebugLabel; }

std::string testClipboard;

std::uint64_t nowNanos() { return gr::settings::convertTimePointToUint64Ns(std::chrono::system_clock::now()); }

std::uint64_t countOf(gr::log::Level level) { return logHistory().counts()[static_cast<std::size_t>(level)]; }

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream) {
        return {};
    }

    std::ostringstream ss;
    ss << stream.rdbuf();
    return ss.str();
}

} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "statusbarview", "log, notifications and status-bar blocks");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *gState;
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            {
                imw::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                state.dashboard->session.receiveMessages();
                ImGui::Dummy(ImVec2(0.f, ImGui::GetContentRegionAvail().y - StatusBarView::height()));
                const bool withGraph = state.dashboard->isInitialised;
                state.view.draw(withGraph ? &state.dashboard->session : nullptr);
            }
            if (state.viewModeLocked) {
                std::ignore = components::drawViewModeBlocker(ImRect(kHostPos, kHostPos + kHostSize - ImVec2(0.f, StatusBarView::height())));
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&      state       = *gState;
            const auto graphLoaded = [&] {
                const auto* scheduler = std::get_if<UiGraphBlock::SchedulerBlockInfo>(&state.dashboard->session.graphModel.rootBlock.blockCategoryInfo);
                return state.dashboard->isInitialised && scheduler && scheduler->childrenLoaded;
            };
            state.waitUntil(ctx, "the graph is loaded and its status-bar blocks are drawn", [&] { return graphLoaded() && ctx->ItemExists("**/status_a_text"); });

            "loading a valid dashboard logs no warning or error, except for the Qt block it cannot show"_test = [&] {
                for (const gr::log::LogRecord& record : logHistory().snapshot()) {
                    const std::string_view text(record.text, record.textLength);
                    expect(record.level > gr::log::Level::warning || text.contains("ForeignStatusBlock")) << std::format("{}: {}", gr::meta::enumName(record.level).value_or("?"), text);
                }
            };

            "the info dot is lit for five seconds after the latest info or debug record"_test = [] {
                constexpr std::uint64_t kT0 = 1'000'000'000'000UZ;
                expect(StatusBarView::isInfoDotLit(kT0, kT0) && StatusBarView::isInfoDotLit(kT0, kT0 + 4'999'999'999UZ));
                expect(!StatusBarView::isInfoDotLit(kT0, kT0 + 5'000'000'000UZ));
                expect(!StatusBarView::isInfoDotLit(0U, kT0)) << "nothing logged";
            };

            "the history keeps the time of the latest info or debug record until cleared"_test = [] {
                logHistory().clear();
                gr::log::warning("not info");
                expect(eq(logHistory().latestInfoOrDebugNanos(), 0UZ));
                const std::uint64_t before = nowNanos();
                std::ignore                = logHistory().publish(gr::log::LogRecord{.level = gr::log::Level::info, .timestampNanos = nowNanos()});
                expect(ge(logHistory().latestInfoOrDebugNanos(), before));
                logHistory().clear();
                expect(eq(logHistory().latestInfoOrDebugNanos(), 0UZ));
            };

            "a warning logged from a worker thread shows in the bar with its count"_test = [&] {
                logHistory().clear();
                std::thread([] { gr::log::warning("pump overheated"); }).join();
                state.waitUntil(ctx, "the bar shows the warning", [ctx] { return barLabel(ctx).contains("pump overheated"); });
                const std::string label = barLabel(ctx);
                expect(label.contains("pump overheated")) << label;
                expect(eq(countOf(gr::log::Level::warning), 1UZ));
                expect(ctx->ItemExists("**/##errorDot") && ctx->ItemExists("**/##warningDot") && ctx->ItemExists("**/##infoDot"));
            };

            "a multi-line log message stays on one status-bar line"_test = [&] {
                logHistory().clear();
                gr::log::warning("first line\nsecond line");
                state.waitUntil(ctx, "the bar shows the record", [ctx] { return barLabel(ctx).contains("first line"); });
                const ImGuiTestItemInfo line = ctx->ItemInfo("**/###statusLine");
                expect(!std::string_view(line.DebugLabel).contains("\n")) << line.DebugLabel;
                expect(std::string_view(line.DebugLabel).contains("first line second line")) << line.DebugLabel;
                const float lineHeight = line.RectFull.Max.y - line.RectFull.Min.y;
                expect(lineHeight <= ImGui::GetFrameHeight() + 1.f) << std::format("status line height {}, frame height {}", lineHeight, ImGui::GetFrameHeight());
            };

            "a notification shows in the bar and as a toast"_test = [&] {
                logHistory().clear();
                const auto toastsBefore = ImGui::notifications.size();
                components::Notification::error("bad yaml");
                state.waitUntil(ctx, "the bar shows the notification", [ctx] { return barLabel(ctx).contains("bad yaml"); });
                expect(barLabel(ctx).contains("bad yaml") && eq(countOf(gr::log::Level::error), 1UZ)) << barLabel(ctx);
                expect(eq(ImGui::notifications.size(), toastsBefore + 1UZ)) << "a toast is queued too";
            };

            "in locked View mode the bar opens the log, and clear empties it"_test = [&] {
                state.viewModeLocked = true;
                logHistory().clear();
                gr::log::warning("first");
                gr::log::error("second");
                state.waitUntil(ctx, "the bar shows the error", [ctx] { return barLabel(ctx).contains("second"); });
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the log popup is open", [ctx] { return ctx->ItemExists("**/clearLogs"); });
                expect(ctx->ItemExists("**/clearLogs")) << fatal << "the log popup is open";
                const ImGuiWindow* logWindow = ctx->ItemInfo("**/clearLogs").Window;
                const ImGuiWindow* barWindow = ctx->ItemInfo("**/###statusLine").Window;
                expect(std::abs(logWindow->Size.x - barWindow->Size.x) < 1.f && std::abs(logWindow->Pos.x - barWindow->Pos.x) < 1.f) << std::format("the log spans the bar: {} px at {}, bar {} px at {}", logWindow->Size.x, logWindow->Pos.x, barWindow->Size.x, barWindow->Pos.x);
                ctx->ItemClick("**/clearLogs", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the history is cleared", [] { return logHistory().snapshot().empty(); });
                expect(logHistory().snapshot().empty()) << "history cleared";
                expect(eq(countOf(gr::log::Level::error) + countOf(gr::log::Level::warning), 0UZ));
                ctx->PopupCloseAll();
                state.viewModeLocked = false;
            };

            "the log copies all records, or the selected ones, to the clipboard"_test = [&] {
                ImGuiPlatformIO& platform      = ImGui::GetPlatformIO();
                const auto       desktopGetter = std::exchange(platform.Platform_GetClipboardTextFn, [](ImGuiContext*) -> const char* { return testClipboard.c_str(); });
                const auto       desktopSetter = std::exchange(platform.Platform_SetClipboardTextFn, [](ImGuiContext*, const char* text) { testClipboard = text; });
                state.viewModeLocked           = true;
                logHistory().clear();
                gr::log::warning("alpha");
                gr::log::warning("beta");
                gr::log::warning("gamma");
                state.waitUntil(ctx, "the bar shows the newest warning", [ctx] { return barLabel(ctx).contains("gamma"); });
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the log popup is open", [ctx] { return ctx->ItemExists("**/###copyLog"); });
                expect(ctx->ItemExists("**/###copyLog")) << fatal << "the log popup is open";
                ctx->ItemClick("**/###copyLog", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                const std::string all = ImGui::GetClipboardText();
                expect(all.contains("alpha") && all.contains("beta") && all.contains("gamma") && all.contains("warning")) << all;
                ctx->ItemClick("**/###record0", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyDown(ImGuiMod_Ctrl);
                ctx->ItemClick("**/###record2", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyUp(ImGuiMod_Ctrl);
                ctx->ItemClick("**/###copyLog", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                const std::string selected = ImGui::GetClipboardText();
                expect(selected.contains("alpha") && !selected.contains("beta") && selected.contains("gamma")) << selected;
                ctx->ItemClick("**/###record1", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
                const std::string shortcut = ImGui::GetClipboardText();
                expect(!shortcut.contains("alpha") && shortcut.contains("beta") && !shortcut.contains("gamma")) << "Ctrl+C: " << shortcut;
                ctx->ItemInputValue("**/##logTextFilter", "beta");
                ctx->ItemClick("**/###copyLog", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                const std::string filtered = ImGui::GetClipboardText();
                expect(!filtered.contains("alpha") && filtered.contains("beta") && !filtered.contains("gamma")) << "Filter: " << filtered;
                ctx->ItemInputValue("**/##logTextFilter", "");
                ctx->PopupCloseAll();
                platform.Platform_GetClipboardTextFn = desktopGetter;
                platform.Platform_SetClipboardTextFn = desktopSetter;
                state.viewModeLocked                 = false;
            };

            "save to file keeps the payload after a failed start, then overwrite confirmation replaces the file"_test = [&] {
                state.viewModeLocked                      = true;
                const std::filesystem::path savePath      = std::filesystem::temp_directory_path() / "qa_statusbar_view_log.txt";
                const std::filesystem::path blockedParent = std::filesystem::temp_directory_path() / "qa_statusbar_view_blocked_parent";
                const std::filesystem::path invalidPath   = blockedParent / "log.txt";
                std::filesystem::remove(savePath);
                std::error_code cleanupError;
                std::filesystem::permissions(blockedParent, std::filesystem::perms::owner_all, cleanupError);
                cleanupError.clear();
                std::filesystem::remove_all(blockedParent, cleanupError);
                std::filesystem::create_directory(blockedParent);
                std::filesystem::permissions(blockedParent, std::filesystem::perms::none);

                logHistory().clear();
                gr::log::warning("alpha");
                gr::log::warning("beta");
                gr::log::warning("gamma");
                state.waitUntil(ctx, "the bar shows the newest warning", [ctx] { return barLabel(ctx).contains("gamma"); });
                ctx->ItemClick("**/###statusLine", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the log popup is open", [ctx] { return ctx->ItemExists("**/##logTextFilter"); });
                ctx->ItemInputValue("**/##logTextFilter", "");
                expect(ctx->ItemExists("**/###saveLogToFile")) << fatal << "the save button is shown";
                ctx->ItemClick("**/###saveLogToFile", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the save popup is open", [ctx] { return ctx->ItemExists("**/##saveLogPath"); });
                expect(ctx->ItemExists("**/##saveLogPath")) << fatal << "the save popup is open";
                ctx->ItemInputValue("**/##saveLogPath", invalidPath.string().c_str());
                ctx->Yield();
                expect(ctx->ItemExists("**/##saveLogPath")) << fatal << "the save popup stays open after a failed start";
                ctx->ItemInputValue("**/##saveLogPath", savePath.string().c_str());
                state.waitUntil(ctx, "the log is saved", [&] { return std::filesystem::exists(savePath); });
                const std::string savedAll = readFile(savePath);
                expect(savedAll.contains("alpha") && savedAll.contains("beta") && savedAll.contains("gamma") && savedAll.contains("warning")) << savedAll;

                ctx->ItemClick("**/###record1", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                ctx->ItemClick("**/###saveLogToFile", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the save popup is open", [ctx] { return ctx->ItemExists("**/##saveLogPath"); });
                ctx->ItemInputValue("**/##saveLogPath", savePath.string().c_str());
                state.waitUntil(ctx, "the overwrite confirmation is shown", [ctx] { return ctx->ItemExists("**/###saveLogOverwrite"); });
                expect(ctx->ItemExists("**/###saveLogOverwrite")) << fatal << "overwrite confirmation is shown";
                ctx->ItemClick("**/###saveLogOverwrite", ImGuiMouseButton_Left, ImGuiTestOpFlags_NoFocusWindow);
                state.waitUntil(ctx, "the log is overwritten", [&] { return readFile(savePath) != savedAll; });
                const std::string savedSelection = readFile(savePath);
                expect(!savedSelection.contains("alpha") && savedSelection.contains("beta") && !savedSelection.contains("gamma")) << savedSelection;

                std::filesystem::remove(savePath);
                cleanupError.clear();
                std::filesystem::permissions(blockedParent, std::filesystem::perms::owner_all, cleanupError);
                cleanupError.clear();
                std::filesystem::remove_all(blockedParent, cleanupError);
                ctx->PopupCloseAll();
                state.viewModeLocked = false;
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
                state.waitUntil(ctx, "the indicator shows RUNNING", [ctx] { return ctx->ItemInfo("**/###schedulerState").DebugLabel == std::string("RUNNING###schedulerState"); });
                expect(ctx->ItemInfo("**/###schedulerState").DebugLabel == std::string("RUNNING###schedulerState")) << ctx->ItemInfo("**/###schedulerState").DebugLabel;

                state.dashboard->session.stop();
                std::string label;
                state.waitUntil(ctx, "the indicator shows STOPPED", [&] {
                    label = ctx->ItemInfo("**/###schedulerState").DebugLabel;
                    return label == "STOPPED###schedulerState";
                });
                expect(label == std::string("STOPPED###schedulerState")) << label;
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "status_bar_view";
    TestApp app(options);

    app.initImGui();
    TestState state;
    gState = std::addressof(state);

    auto& registry = gr::globalBlockRegistry();
    registerAppBlocks(registry);
    std::ignore = gr::registerBlock<ForeignStatusBlock>(registry);
    std::ignore = gr::registerBlock<TestStatusText>(registry);

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_statusbar.grc", "status bar");

    const bool result = app.runTests();
    return result ? 0 : 1;
}
