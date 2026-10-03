#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include "../common/LookAndFeel.hpp"
#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <DashboardView.hpp>

#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <span>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

namespace {
using Mode           = DigitizerUi::DashboardView::Mode;
using LegendPosition = DigitizerUi::DashboardView::LegendPosition;

// an embedding host draws the dashboard away from the screen origin
constexpr ImVec2 kHostPos{200.f, 150.f};
constexpr ImVec2 kHostSize{800.f, 600.f};

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard>     dashboard;      // loads qa_layout.grc
    std::shared_ptr<DigitizerUi::Dashboard>     emptyDashboard; // never loads a graph
    std::unique_ptr<DigitizerUi::DashboardView> view;
    std::unique_ptr<DigitizerUi::DashboardPage> page; // the App's page, drawn at the same offset
    Mode                                        mode         = Mode::View;
    LegendPosition                              legend       = LegendPosition::Bottom;
    bool                                        drawEmpty    = false;
    bool                                        documentHost = false; // gr4-present: an input-less full-screen window, a child per region
    int                                         regionId     = 0;     // the host's PushID around the region
    bool                                        background   = true;
    std::optional<ImVec4>                       hostBackground;
};

TestState* g_state = nullptr;

const ImGuiWindow* hostWindow() { return ImGui::FindWindowByName("Host"); }

std::vector<const ImGuiWindow*> chartWindows() {
    std::vector<const ImGuiWindow*> windows;
    for (const auto& uiWindow : g_state->dashboard->uiWindows) {
        if (uiWindow.isChart()) {
            windows.push_back(ImGui::FindWindowByName(uiWindow.window->name.c_str()));
        }
    }
    return windows;
}

void waitForStableCharts(ImGuiTestContext* ctx) {
    std::vector<ImRect> previous;
    std::size_t         stableFrames = 0UZ;
    for (std::size_t frame = 0UZ; frame < 300UZ && stableFrames < 5UZ; ++frame) {
        ctx->Yield();
        std::vector<ImRect> current;
        for (const auto* window : chartWindows()) {
            current.push_back(window ? ImRect(window->Pos, window->Pos + window->Size) : ImRect());
        }
        const bool same = current.size() == previous.size() && std::ranges::equal(current, previous, [](const ImRect& a, const ImRect& b) { return a.Min == b.Min && a.Max == b.Max; });
        stableFrames    = (same && !current.empty()) ? stableFrames + 1UZ : 0UZ;
        previous        = std::move(current);
    }
    expect(stableFrames >= 5UZ) << fatal << "chart windows settle";
}

ImRect chartsBounds() {
    ImRect bounds(ImVec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), ImVec2(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()));
    for (const auto* window : chartWindows()) {
        bounds.Add(ImRect(window->Pos, window->Pos + window->Size));
    }
    return bounds;
}

float topOfCharts() {
    float top = std::numeric_limits<float>::max();
    for (const auto* window : chartWindows()) {
        top = std::min(top, window->Pos.y);
    }
    return top;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboardview", "embedded dashboard");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *g_state;
            if (state.documentHost && state.view) {
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                IMW::Window document("##document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetCursorScreenPos(kHostPos);
                IMW::ChangeId regionScope(state.regionId);
                IMW::Child    region("##region", kHostSize, ImGuiChildFlags_None, ImGuiWindowFlags_None);
                DigitizerUi::LookAndFeel::mutableInstance().dashboardStyle.legend = LegendPosition::None; // as gr4-present draws a region
                std::ignore                                                       = state.view->draw(*state.dashboard, state.mode);
                return;
            }
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            if (state.hostBackground) {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, *state.hostBackground);
            }
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            if (state.hostBackground) {
                ImGui::PopStyleColor(); // only the host's own window
            }
            if (!state.view) {
                return;
            }
            auto& dashboardStyle      = DigitizerUi::LookAndFeel::mutableInstance().dashboardStyle;
            dashboardStyle.legend     = state.legend;
            dashboardStyle.background = state.background;
            std::ignore               = state.view->draw(state.drawEmpty ? *state.emptyDashboard : *state.dashboard, state.mode);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *g_state;

            "a dashboard that has not loaded is drawn as a placeholder, without chart windows"_test = [&] {
                state.view      = std::make_unique<DigitizerUi::DashboardView>();
                state.drawEmpty = true;
                ctx->Yield(10);
                expect(!state.emptyDashboard->isInitialised.load());
                expect(state.emptyDashboard->uiWindows.empty()) << "no chart windows";
                state.view.reset();
                state.drawEmpty = false;
            };

            "a dashboard drawn in a child of an input-less full-screen window docks its charts inside the child, also under another ID stack"_test = [&] {
                state.view         = std::make_unique<DigitizerUi::DashboardView>();
                state.mode         = Mode::View;
                state.documentHost = true;
                while (state.dashboard->graphModel.recursiveGatherPlotSinks().size() < 6UZ) {
                    ctx->Yield();
                }
                const ImRect regionRect(kHostPos, kHostPos + kHostSize);
                const auto   expectDockedInRegion = [&](std::string_view when) {
                    waitForStableCharts(ctx);
                    for (const ImGuiWindow* window : chartWindows()) {
                        expect(window != nullptr) << fatal;
                        const ImRect rect(window->Pos, window->Pos + window->Size);
                        expect(window->DockIsActive) << std::format("{}: '{}' is docked", when, window->Name);
                        expect(regionRect.Contains(rect)) << std::format("{}: '{}' at ({}, {}) size ({}, {}) inside the region", when, window->Name, rect.Min.x, rect.Min.y, rect.GetWidth(), rect.GetHeight());
                    }
                };
                expectDockedInRegion("first draw");
                captureScreenshot(*ctx, regionRect);
                const auto   firstChartRect = [] { return ImRect(chartWindows().front()->Pos, chartWindows().front()->Pos + chartWindows().front()->Size); };
                const ImRect firstChart     = firstChartRect();
                state.regionId              = 1; // the same view under another ID stack, hence another dockspace ID
                expectDockedInRegion("under another ID");
                // the layout moves as split ratios of node sizes, separators included: a pixel or two of rounding; the
                // grid layout a lost arrangement falls back to differs by a hundred
                constexpr float kRoundingPx     = 4.f;
                const auto      near            = [](ImVec2 a, ImVec2 b) { return std::abs(a.x - b.x) <= kRoundingPx && std::abs(a.y - b.y) <= kRoundingPx; };
                const ImRect    firstChartAfter = firstChartRect();
                expect(near(firstChartAfter.Min, firstChart.Min) && near(firstChartAfter.Max, firstChart.Max)) << std::format("'{}' keeps its place: ({}, {}) size ({}, {}), was ({}, {}) size ({}, {})", chartWindows().front()->Name, firstChartAfter.Min.x, firstChartAfter.Min.y, firstChartAfter.GetWidth(), firstChartAfter.GetHeight(), firstChart.Min.x, firstChart.Min.y, firstChart.GetWidth(), firstChart.GetHeight());
                state.documentHost = false;
            };

            "every chart window of an embedded dashboard is docked inside the host rectangle"_test = [&] {
                state.view = std::make_unique<DigitizerUi::DashboardView>(); // a second view of the dashboard: no free layout of its own yet
                // the legend lists the sinks once the graph model has received them from the scheduler
                while (state.dashboard->graphModel.recursiveGatherPlotSinks().size() < 6UZ) {
                    ctx->Yield();
                }
                for (const Mode mode : {Mode::View, Mode::Interaction}) {
                    state.mode = mode;
                    waitForStableCharts(ctx);
                    const ImGuiWindow* host = hostWindow();
                    expect(host != nullptr) << fatal;
                    const ImRect hostRect(host->Pos, host->Pos + host->Size);
                    const auto   windows = chartWindows();
                    expect(!windows.empty()) << fatal;
                    for (const ImGuiWindow* window : windows) {
                        expect(window != nullptr) << fatal;
                        const ImRect rect(window->Pos, window->Pos + window->Size);
                        expect(window->DockIsActive) << std::format("{} '{}' is docked", magic_enum::enum_name(mode), window->Name);
                        expect(hostRect.Contains(rect)) << std::format("{} '{}' at ({}, {}) size ({}, {}) inside the host at ({}, {}) size ({}, {})", magic_enum::enum_name(mode), window->Name, rect.Min.x, rect.Min.y, rect.GetWidth(), rect.GetHeight(), hostRect.Min.x, hostRect.Min.y, hostRect.GetWidth(), hostRect.GetHeight());
                    }
                }
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + kHostSize));
            };

            "without a background the host shows through the chart windows, the plots area and the bar"_test = [&] {
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);
                const auto   magentaShare = [&](bool background) {
                    state.background = background;
                    waitForStableCharts(ctx);
                    ctx->Yield(3);
                    const CapturedPixels pixels  = capturePixels(*ctx, chartsBounds()); // the gaps between windows show the host either way
                    const auto           magenta = std::ranges::count_if(pixels.rgba, [](unsigned int c) { return (c & 0xFFU) > 240U && ((c >> 8U) & 0xFFU) < 15U && ((c >> 16U) & 0xFFU) > 240U; });
                    return static_cast<double>(magenta) / static_cast<double>(pixels.rgba.size());
                };
                state.mode                                                            = Mode::View;
                state.hostBackground                                                  = ImVec4(1.f, 0.f, 1.f, 1.f);
                DigitizerUi::LookAndFeel::mutableInstance().chartStyle.plotBackground = ImVec4(0.f, 0.f, 0.f, 0.f);
                const double withBackground                                           = magentaShare(true);
                const double withoutBackground                                        = magentaShare(false);
                captureScreenshot(*ctx, hostRect);
                DigitizerUi::LookAndFeel::mutableInstance().chartStyle = {};
                state.hostBackground.reset();
                state.background = true;
                expect(withBackground < 0.01) << std::format("with backgrounds the chart windows cover the host: {:.3f} of their area magenta", withBackground);
                expect(withoutBackground > 0.5) << std::format("without them the host shows through: {:.3f} of the chart windows' area magenta", withoutBackground);
            };

            "every new view of the dashboard lays out its charts by the .grc's free-layout cells"_test = [&] {
                // qa_layout.grc, 4x4 cells: Plot1 3 wide, Plot2 1 wide (same row); Plot3 1 high, Plot4 2 high
                const auto size = [](const char* name) {
                    const ImGuiWindow* window = ImGui::FindWindowByName(name);
                    return window ? window->Size : ImVec2{};
                };
                const auto expectCellRatios = [&](std::string_view view) {
                    waitForStableCharts(ctx);
                    const float widthRatio  = size("Plot1").x / std::max(1.f, size("Plot2").x);
                    const float heightRatio = size("Plot4").y / std::max(1.f, size("Plot3").y);
                    expect(widthRatio > 2.5f && widthRatio < 3.5f) << std::format("{}: Plot1 is {:.2f} times as wide as Plot2, the cells 3", view, widthRatio);
                    expect(heightRatio > 1.7f && heightRatio < 2.3f) << std::format("{}: Plot4 is {:.2f} times as high as Plot3, the cells 2", view, heightRatio);
                };
                state.mode = Mode::View;
                expectCellRatios("this view");
                state.view = std::make_unique<DigitizerUi::DashboardView>();
                expectCellRatios("a second view");
            };

            "a saved free layout wins over the .grc cells in a new view"_test = [&] {
                // Plot1 on the left half, Plot2..Plot6 stacked on the right half: Plot1 and Plot2 equally wide (cells: 3:1)
                const auto stacked = [](this const auto& self, std::span<const std::string> names) -> gr::pmt::Value {
                    if (names.size() == 1UZ) {
                        return names.front();
                    }
                    return gr::property_map{{"vsplit", gr::property_map{{"ratio", .5f}, {"first", names.front()}, {"second", self(names.subspan(1UZ))}}}};
                };
                const std::array<std::string, 5> right{"Plot2", "Plot3", "Plot4", "Plot5", "Plot6"};
                const gr::property_map           saved{{"dockSpace", gr::property_map{{"hsplit", gr::property_map{{"ratio", .5f}, {"first", std::string("Plot1")}, {"second", stacked(right)}}}}}, {"floatingWindows", gr::property_map{}}};
                state.dashboard->windowLayout = saved;
                state.mode                    = Mode::View;
                state.view                    = std::make_unique<DigitizerUi::DashboardView>();
                waitForStableCharts(ctx);
                const auto width = [](const char* name) {
                    const ImGuiWindow* window = ImGui::FindWindowByName(name);
                    return window ? window->Size.x : 0.f;
                };
                const float widthRatio        = width("Plot1") / std::max(1.f, width("Plot2"));
                state.dashboard->windowLayout = {};
                expect(widthRatio > .8f && widthRatio < 1.25f) << std::format("Plot1 is {:.2f} times as wide as Plot2: the saved layout's halves, not the cells' 3", widthRatio);
            };

            "a legend on top moves the charts below it"_test = [&] {
                state.mode   = Mode::View;
                state.legend = LegendPosition::Bottom;
                waitForStableCharts(ctx);
                const float topWithLegendBelow = topOfCharts();
                state.legend                   = LegendPosition::Top;
                waitForStableCharts(ctx);
                expect(topOfCharts() > topWithLegendBelow + 10.f) << std::format("top of charts {} with the legend on top, {} with it below", topOfCharts(), topWithLegendBelow);
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + kHostSize));
                state.legend = LegendPosition::Bottom;
            };

            "a legend on the left or right narrows the charts from that side, inside the host"_test = [&] {
                state.mode   = Mode::View;
                state.legend = LegendPosition::Bottom;
                waitForStableCharts(ctx);
                const ImRect withLegendBelow = chartsBounds();
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);

                state.legend = LegendPosition::Left;
                waitForStableCharts(ctx);
                const ImRect withLegendLeft = chartsBounds();
                expect(withLegendLeft.Min.x > withLegendBelow.Min.x + 30.f) << std::format("left edge {} with the legend on the left, {} with it below", withLegendLeft.Min.x, withLegendBelow.Min.x);
                expect(hostRect.Contains(withLegendLeft));
                captureScreenshot(*ctx, hostRect);

                state.legend = LegendPosition::Right;
                waitForStableCharts(ctx);
                const ImRect withLegendRight = chartsBounds();
                expect(withLegendRight.Max.x < withLegendBelow.Max.x - 30.f) << std::format("right edge {} with the legend on the right, {} with it below", withLegendRight.Max.x, withLegendBelow.Max.x);
                expect(hostRect.Contains(withLegendRight));
                captureScreenshot(*ctx, hostRect);
                state.legend = LegendPosition::Bottom;
            };
            state.view.reset();
        };

        ImGuiTest* pageTest = IM_REGISTER_TEST(engine(), "dashboardview", "App page away from the screen origin");

        pageTest->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            auto&       state = *g_state;
            if (!state.page) {
                state.page = std::make_unique<DigitizerUi::DashboardPage>();
                state.page->setDashboard(*state.dashboard);
            }
            state.page->draw(Mode::Interaction);
        };

        pageTest->TestFunc = [](ImGuiTestContext* ctx) {
            "the edit pane opened from the legend lies inside the host"_test = [ctx] {
                while (g_state->dashboard->graphModel.recursiveGatherPlotSinks().size() < 6UZ) {
                    ctx->Yield();
                }
                ctx->Yield(5);
                ctx->ItemClick("**/PlotSink1", ImGuiMouseButton_Right);
                ctx->Yield(60); // the splitter animates the edit pane open

                const ImGuiWindow* panel = ImGui::FindWindowByName("BlockControlsPanel");
                expect(panel != nullptr && panel->Active) << fatal << "the edit pane is open";
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);
                const ImRect panelRect(panel->Pos, panel->Pos + panel->Size);
                expect(hostRect.Contains(panelRect)) << std::format("edit pane at ({}, {}) size ({}, {}) inside the host at ({}, {})", panelRect.Min.x, panelRect.Min.y, panelRect.GetWidth(), panelRect.GetHeight(), hostRect.Min.x, hostRect.Min.y);
                captureScreenshot(*ctx, hostRect);
            };
            g_state->page.reset();
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboard_view";
    TestApp app(options);
    auto    restClient = std::make_shared<opencmw::client::RestClient>();

    app.initImGui(); // also runs DigitizerUi::initialise()

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);

    auto grcFile         = cmrc::ui_test_assets::get_filesystem().open("examples/qa_layout.grc");
    state.dashboard      = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("embedded"));
    state.emptyDashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("never loaded"));
    state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { state.dashboard->emplaceGraph(std::move(graph)); });

    const bool result = app.runTests();
    state.page.reset();
    state.view.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
