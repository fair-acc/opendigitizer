#include "App.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <OpenDashboardPage.hpp>
#include <components/AppHeader.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <string_view>

using namespace gr::od;

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

namespace {
using Mode = DashboardView::Mode;

struct TestState : test::TestDashboardRunner {
    std::unique_ptr<DashboardPage>     page;
    std::unique_ptr<OpenDashboardPage> selection;
    components::AppHeader              header;
    bool                               showSelection = false;
};

TestState* gState = nullptr;

const ImGuiWindow* activeWindowNamed(std::string_view part) {
    for (const ImGuiWindow* window : GImGui->Windows) {
        if (window->WasActive && std::string_view(window->Name).find(part) != std::string_view::npos) {
            return window;
        }
    }
    return nullptr;
}

std::vector<const ImGuiWindow*> chartWindows() { return test::chartWindows(*gState->dashboard); }

ImRect viewportRect() { return ImRect(ImVec2{}, ImGui::GetIO().DisplaySize); }

bool expectedCompact() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    return std::min(size.x, size.y) < LookAndFeel::instance().compactEnterShortSide;
}

bool expectedPortrait() { return ImGui::GetIO().DisplaySize.y > ImGui::GetIO().DisplaySize.x; }
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "compactlayout", "phone-sized window");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto&                state    = *gState;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            imw::Window window("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            state.header.draw(state.dashboard->description->name, LookAndFeel::instance().fontLarge(), LookAndFeel::instance().style);
            imw::Child pageArea("##pageArea", ImVec2{}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
            if (state.showSelection) {
                state.selection->draw(state.dashboard.get(), state.page.get());
            } else {
                state.page->draw(Mode::Interaction);
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&       state       = *gState;
            const auto& lookAndFeel = LookAndFeel::instance();
            const auto& appState    = gAppState;

            "the compact layout follows the shorter viewport side, with hysteresis"_test = [&] {
                auto& mutableLookAndFeel = LookAndFeel::mutableInstance();
                mutableLookAndFeel.updateFormFactor({1600.f, 1000.f});
                expect(!appState.compact && eq(ImGui::GetStyle().FontScaleMain, 1.f));
                mutableLookAndFeel.updateFormFactor({412.f, 915.f});
                expect(appState.compact && appState.portrait && eq(ImGui::GetStyle().FontScaleMain, lookAndFeel.compactFontScale));
                mutableLookAndFeel.updateFormFactor({915.f, 412.f});
                expect(appState.compact && !appState.portrait);
                mutableLookAndFeel.updateFormFactor({620.f, 900.f});
                expect(appState.compact) << "stays compact until the leave threshold";
                mutableLookAndFeel.updateFormFactor({700.f, 900.f});
                expect(!appState.compact);
                mutableLookAndFeel.updateFormFactor({620.f, 900.f});
                expect(!appState.compact) << "stays desktop until the enter threshold";
                mutableLookAndFeel.updateFormFactor({412.f, 915.f});
                mutableLookAndFeel.updateFormFactor({0.f, 0.f});
                expect(appState.compact) << "a minimised window keeps its layout";
                ctx->Yield();
                expect(eq(appState.compact, expectedCompact())) << "the next frame follows the window again";
                expect(eq(ImGui::GetStyle().FontScaleMain, expectedCompact() ? lookAndFeel.compactFontScale : 1.f));
            };

            "charts stack in a column in portrait and in a grid in landscape, and the stored layout is saved"_test = [&] {
                while (state.dashboard->session.graphModel.recursiveGatherPlotSinks().size() < 6UZ) {
                    ctx->Yield();
                }
                test::waitUntilAllSamplesDrawn(ctx, *state.dashboard);
                test::waitForStableCharts(ctx, *state.dashboard);

                std::set<float> lefts;
                std::set<float> tops;
                for (const ImGuiWindow* window : chartWindows()) {
                    expect(window != nullptr) << fatal;
                    expect(viewportRect().Contains(ImRect(window->Pos, window->Pos + window->Size))) << std::format("chart '{}' on screen", window->Name);
                    lefts.insert(window->Pos.x);
                    tops.insert(window->Pos.y);
                }
                if (expectedCompact() && expectedPortrait()) {
                    expect(eq(lefts.size(), 1UZ)) << "one column";
                    expect(eq(tops.size(), chartWindows().size())) << "one chart per row";
                } else if (expectedCompact()) {
                    expect(gt(lefts.size(), 1UZ) && gt(tops.size(), 1UZ)) << "a grid";
                }
                expect(state.page->saveLayoutConfiguration().first == DockingLayoutType::Free) << "the .grc's Free layout is what gets saved";
                captureScreenshot(*ctx, viewportRect());
            };

            "the selection page fits the window, with icon buttons, and the filters behind a toggle in the compact layout"_test = [&] {
                state.showSelection = true;
                while (!ctx->ItemExists("**/###Open a new Digitizer Window")) {
                    ctx->Yield();
                }
                const auto openNewWindowButton = ctx->ItemInfo("**/###Open a new Digitizer Window");
                expect(openNewWindowButton.ID != 0) << fatal;
                expect(viewportRect().Contains(openNewWindowButton.RectFull)) << "the bottom buttons on screen";
                for (const char* button : {"**/###Save", "**/###Save as", "**/###Close", "**/###Load", "**/###Open empty dashboard"}) {
                    const auto info = ctx->ItemInfo(button);
                    expect(info.ID != 0 && viewportRect().Contains(info.RectFull)) << std::format("'{}' on screen", button);
                }
                ctx->MouseMove("**/###Open empty dashboard");
                while (activeWindowNamed("##Tooltip") == nullptr) {
                    ctx->Yield();
                }
                ctx->MouseMove("**/##dashboardSearch");

                if (!expectedCompact()) {
                    expect(activeWindowNamed("##dashboardSidebar") != nullptr) << "the sidebar beside the table";
                    captureScreenshot(*ctx, viewportRect());
                    return;
                }
                expect(activeWindowNamed("##dashboardSidebar") == nullptr) << "no sidebar";
                expect(activeWindowNamed("##dashboardFilters") == nullptr) << "filters collapsed";
                captureScreenshot(*ctx, viewportRect());

                ctx->ItemClick("**/###Filter and view options");
                while (activeWindowNamed("##dashboardFilters") == nullptr) {
                    ctx->Yield();
                }
                const ImGuiWindow* filters = activeWindowNamed("##dashboardFilters");
                expect(filters != nullptr) << fatal << "filters shown after the toggle";
                expect(viewportRect().Contains(ImRect(filters->Pos, filters->Pos + filters->Size))) << "filters on screen";
                captureScreenshot(*ctx, viewportRect());
                ctx->ItemClick("**/###Filter and view options");
                while (activeWindowNamed("##dashboardFilters") != nullptr) {
                    ctx->Yield();
                }
            };
            state.showSelection = false;
        };
    }
};

int main(int argc, char* argv[]) {
    auto              options = test::TestOptions::fromArgs(argc, argv);
    const std::string prefix  = std::format("compact_layout_{}x{}", static_cast<int>(options.windowSize.x), static_cast<int>(options.windowSize.y));
    options.screenshotPrefix  = prefix.c_str();
    TestApp app(options);

    app.initImGui();
    TestState state;
    gState = std::addressof(state);
    state.header.loadAssets();

    auto& registry = gr::globalBlockRegistry();
    registerAppBlocks(registry);

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_layout.grc", "CompactLayoutDashboard");
    state.page = std::make_unique<DashboardPage>();
    state.page->setDashboard(*state.dashboard);

    state.selection                        = std::make_unique<OpenDashboardPage>(state.restClient);
    state.selection->requestLoadDashboard  = [](const std::shared_ptr<const DashboardDescription>&) {};
    state.selection->requestCloseDashboard = [] {};
    state.selection->addDashboard("example://builtin-samples");

    const bool result = app.runTests();
    return result ? 0 : 1;
}
