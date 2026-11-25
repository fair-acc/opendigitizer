#include "App.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <DashboardView.hpp>
#include <common/LookAndFeel.hpp>
#include <components/GlobalSignalLegend.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace gr::od;

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

namespace {
using Mode = DashboardPage::Mode;

constexpr std::array kPhoneScreenSizes{
    ImVec2{320.f, 568.f},
    ImVec2{360.f, 640.f},
    ImVec2{390.f, 844.f},
    ImVec2{430.f, 932.f},
    ImVec2{568.f, 320.f},
    ImVec2{844.f, 390.f},
};
constexpr std::size_t kNumSignalsInManySignalsGrc = 30UZ;

struct TestState : test::TestDashboardRunner {
    std::unique_ptr<DashboardPage> page;
    Mode                           mode = Mode::Interaction;
};

TestState* gState = nullptr;

ImRect screenRect() { return ImRect(ImVec2{}, ImGui::GetMainViewport()->Size); }

std::string formatImRect(const ImRect& rect) { return std::format("({}, {})-({}, {})", rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y); }

struct NamedRect {
    ImGuiID     id;
    std::string name;
    ImRect      rect;
};

std::vector<NamedRect> chartRects() {
    std::vector<NamedRect> rects;
    for (const auto& uiWindow : gState->dashboard->uiWindows) {
        if (!uiWindow.window || !uiWindow.block) {
            continue;
        }
        const ImGuiWindow* window = ImGui::FindWindowByName(uiWindow.window->name.c_str());
        if (window != nullptr && window->Active) {
            rects.push_back({window->ID, uiWindow.window->name, ImRect(window->Pos, window->Pos + window->Size)});
        }
    }
    return rects;
}

const ImGuiWindow* dashboardViewWindow() {
    const ImGuiWindow* host = ImGui::FindWindowByName("Host");
    for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        if (window->ParentWindow == host && window->Active && std::string_view(window->Name).find("##plots") != std::string_view::npos) {
            return window;
        }
    }
    return nullptr;
}

std::vector<NamedRect> gatherItemRects(ImGuiTestContext* ctx, ImGuiID idScope) {
    std::vector<NamedRect> rects;
    ImGuiTestItemList      items;
    ctx->GatherItems(&items, idScope);
    for (const ImGuiTestItemInfo& item : items) {
        rects.push_back({item.ID, item.DebugLabel, item.RectFull});
    }
    return rects;
}

ImGuiID barIdScope(ImGuiTestContext* ctx) {
    const ImGuiWindow* view = dashboardViewWindow();
    expect(view != nullptr) << fatal << "the dashboard view is drawn";
    return ctx->GetID(DashboardView::kBarIdScope, view->ID);
}

void showScreen(ImGuiTestContext* ctx, ImVec2 screenSize, Mode mode) {
    gState->mode = mode;
    test::resizeViewport(ctx, screenSize);
    test::waitForStableCharts(ctx, *gState->dashboard);
}
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboard_page_layout", "phone screens");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *gState;
            ImGui::SetNextWindowPos(ImVec2{});
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
            imw::Window window("Host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            state.page->draw(state.mode);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *gState;

            // force disable autohide so that we can test the shown locations of everything
            gr::on_scope_exit restore = [autoHideDelaySeconds = std::exchange(LookAndFeel::mutableInstance().autoHideDelaySeconds, std::numeric_limits<float>::infinity())] { //
                LookAndFeel::mutableInstance().autoHideDelaySeconds = autoHideDelaySeconds;
            };

            state.waitUntil(ctx, "signals are all loaded", [&] { return state.dashboard->session.graphModel.recursiveGatherPlotSinks().size() >= kNumSignalsInManySignalsGrc; });

            "the signal legend items do not overlap or go off the screen"_test = [&] {
                for (const Mode mode : {Mode::Interaction, Mode::View}) {
                    for (const ImVec2 screenSize : kPhoneScreenSizes) {
                        showScreen(ctx, screenSize, mode);
                        const std::string context = std::format("{} {}x{}", magic_enum::enum_name(mode), screenSize.x, screenSize.y);
                        captureScreenshot(*ctx, screenRect());
                        const std::vector<NamedRect> charts = chartRects();
                        for (const NamedRect& chart : charts) {
                            expect(chart.rect.GetArea() > 0.f && screenRect().Contains(chart.rect)) << std::format("{}: the chart {} at {} lies inside the screen", context, chart.name, formatImRect(chart.rect));
                        }

                        const ImGuiID          barId       = barIdScope(ctx);
                        std::vector<NamedRect> legendItems = gatherItemRects(ctx, ctx->GetID(GlobalSignalLegend::kIdScope, barId));
                        std::vector<NamedRect> otherBarItems;
                        for (NamedRect& item : gatherItemRects(ctx, barId)) {
                            const bool isLegendItem = std::ranges::any_of(legendItems, [&item](const NamedRect& legendItem) { return legendItem.id == item.id; });
                            if (!isLegendItem && item.rect.GetArea() > 0.f) {
                                otherBarItems.push_back(std::move(item));
                            }
                        }
                        expect(legendItems.size() >= kNumSignalsInManySignalsGrc) << fatal << context << ": all signals are present in the signal legend";
                        for (const std::vector<NamedRect>* items : {&legendItems, &otherBarItems}) {
                            for (const NamedRect& item : *items) {
                                expect(screenRect().Contains(item.rect)) << std::format("{}: the bar item '{}' at {} is inside the screen", context, item.name, formatImRect(item.rect));
                                for (const NamedRect& chart : charts) {
                                    expect(!item.rect.Overlaps(chart.rect)) << std::format("{}: the bar item '{}' at {} does not overlap the chart {} at {}", context, item.name, formatImRect(item.rect), chart.name, formatImRect(chart.rect));
                                }
                            }
                        }
                        for (const NamedRect& item : otherBarItems) {
                            for (const NamedRect& legendItem : legendItems) {
                                expect(!item.rect.Overlaps(legendItem.rect)) << std::format("{}: the bar item '{}' at {} does not overlap the signal legend item '{}' at {}", context, item.name, formatImRect(item.rect), legendItem.name, formatImRect(legendItem.rect));
                            }
                        }
                    }
                }
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboard_page_layout";
    TestApp app(options);
    app.initImGui();

    TestState state;
    gState = std::addressof(state);

    registerAppBlocks(gr::globalBlockRegistry());

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_many_signals.grc", "many signals");
    state.page = std::make_unique<DashboardPage>();
    state.page->setDashboard(*state.dashboard);

    const bool result = app.runTests();
    state.page.reset();
    gState = nullptr;
    return result ? 0 : 1;
}
