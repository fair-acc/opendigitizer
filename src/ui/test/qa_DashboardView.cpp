#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include "../common/LookAndFeel.hpp"
#include <Dashboard.hpp>
#include <DashboardPage.hpp>
#include <DashboardView.hpp>
#include <charts/XYChart.hpp>
#include <components/AutoHide.hpp>
#include <components/SignalSelector.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

constexpr ImVec2 kHostPos{200.f, 150.f};
constexpr ImVec2 kHostSize{800.f, 600.f};

struct TestState : opendigitizer::test::TestDashboardRunner {
    std::shared_ptr<DigitizerUi::Dashboard>     emptyDashboard;
    std::unique_ptr<DigitizerUi::DashboardView> view;
    std::unique_ptr<DigitizerUi::DashboardPage> page;
    Mode                                        mode             = Mode::View;
    LegendPosition                              legend           = LegendPosition::Bottom;
    bool                                        drawEmpty        = false;
    bool                                        documentHost     = false;
    int                                         regionId         = 0;
    bool                                        hideView         = false;
    bool                                        background       = true;
    bool                                        frameProbe       = false;
    float                                       probedFrameAlpha = -1.f;
    std::optional<ImVec4>                       hostBackground;
};

TestState* gState = nullptr;

const ImGuiWindow* hostWindow() { return ImGui::FindWindowByName("Host"); }

std::vector<const ImGuiWindow*> chartWindows() { return opendigitizer::test::chartWindows(*gState->dashboard); }

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
            auto& state = *gState;
            if (state.documentHost && state.view && state.hideView) {
                return;
            }
            if (state.documentHost && state.view) {
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(viewport->Pos);
                ImGui::SetNextWindowSize(viewport->Size);
                IMW::Window document("##document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
                ImGui::SetCursorScreenPos(kHostPos);
                IMW::ChangeId regionScope(state.regionId);
                IMW::Child    region("##region", kHostSize, ImGuiChildFlags_None, ImGuiWindowFlags_None);
                DigitizerUi::LookAndFeel::mutableInstance().dashboardStyle.legend = LegendPosition::None;
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
                ImGui::PopStyleColor();
            }
            if (!state.view) {
                return;
            }
            auto& dashboardStyle      = DigitizerUi::LookAndFeel::mutableInstance().dashboardStyle;
            dashboardStyle.legend     = state.legend;
            dashboardStyle.background = state.background;
            DigitizerUi::DashboardView::Options options;
            if (state.frameProbe) {
                options.addDockWindows = [](DigitizerUi::DockSpace::Windows& windows) {
                    static const auto probe = std::make_shared<DigitizerUi::DockSpace::Window>("FrameProbe");
                    probe->renderFunc       = [] { gState->probedFrameAlpha = ImPlot::GetStyleColorVec4(ImPlotCol_FrameBg).w; };
                    windows.push_back(probe);
                };
            }
            std::ignore = state.view->draw(state.drawEmpty ? *state.emptyDashboard : *state.dashboard, state.mode, options);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *gState;

            "a dashboard that has not loaded is drawn as a placeholder, without chart windows"_test = [&] {
                state.view      = std::make_unique<DigitizerUi::DashboardView>();
                state.drawEmpty = true;
                ctx->Yield(); // drawn once
                expect(!state.emptyDashboard->isInitialised.load());
                expect(state.emptyDashboard->uiWindows.empty()) << "no chart windows";
                state.view.reset();
                state.drawEmpty = false;
            };

            "bar and menu controls slide out of sight once the pointer stayed away, and back when it returns"_test = [] {
                DigitizerUi::components::AutoHide hide;
                const auto&                       look = DigitizerUi::LookAndFeel::instance();
                expect(eq(hide.advance(false, look.autoHideDelaySeconds - 0.1f), 0.f)) << "in place until the delay";
                expect(lt(std::abs(hide.advance(false, 0.2f) - 0.2f / look.autoHideSlideSeconds), 1e-6f)) << "then sliding by dt / autoHideSlideSeconds";
                expect(eq(hide.advance(false, look.autoHideSlideSeconds), 1.f)) << "out of sight";
                expect(lt(std::abs(hide.advance(true, 0.5f * look.autoHideSlideSeconds) - 0.5f), 1e-6f)) << "half way back once the pointer is near";
                expect(eq(hide.advance(true, look.autoHideSlideSeconds), 0.f));
            };

            "the .grc's dashboard editable sets the starting state (default true) and is saved, first"_test = [&] {
                const auto restClient = std::make_shared<opencmw::client::RestClient>();
                for (const std::optional<bool> editable : {std::optional{false}, std::optional{true}, std::optional<bool>{}}) {
                    auto dashboard = std::make_unique<DigitizerUi::Dashboard>(restClient, DigitizerUi::DashboardDescription::createEmpty("editable"));
                    dashboard->loadAndThen(std::format("blocks: []\nconnections: []\ndashboard:\n{}  layout: Free\n  sources: []\n  plots: []\n", editable ? std::format("  editable: {}\n", *editable) : std::string{}), [](gr::Graph&&) {}); // parsed, not run
                    expect(eq(dashboard->editable, editable.value_or(true)));
                }
                state.dashboard->editable = false;
                const auto saved          = state.dashboard->serialise().second.value_or<gr::property_map>("dashboard", {});
                state.dashboard->editable = true;
                expect(!saved.value_or<bool>("editable", true)) << "the current state is saved";
                expect(!saved.empty() && std::string_view(saved.begin()->first) == "editable") << "written first";
            };

            "a dashboard drawn in a child of an input-less full-screen window docks its charts inside the child, also under another ID stack"_test = [&] {
                state.view         = std::make_unique<DigitizerUi::DashboardView>();
                state.mode         = Mode::View;
                state.documentHost = true;
                state.waitUntil(ctx, "the six plot sinks are known", [] { return gState->dashboard->session.graphModel.recursiveGatherPlotSinks().size() >= 6UZ; });
                opendigitizer::test::waitUntilAllSamplesDrawn(ctx, *state.dashboard);
                const ImRect regionRect(kHostPos, kHostPos + kHostSize);
                const auto   expectDockedInRegion = [&](std::string_view when) {
                    opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
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
                state.regionId              = 1;
                expectDockedInRegion("under another ID");
                // split ratios of node sizes round by a pixel or two; the fallback grid layout differs by a hundred
                constexpr float kRoundingPx     = 4.f;
                const auto      near            = [](ImVec2 a, ImVec2 b) { return std::abs(a.x - b.x) <= kRoundingPx && std::abs(a.y - b.y) <= kRoundingPx; };
                const ImRect    firstChartAfter = firstChartRect();
                expect(near(firstChartAfter.Min, firstChart.Min) && near(firstChartAfter.Max, firstChart.Max)) << std::format("'{}' keeps its place: ({}, {}) size ({}, {}), was ({}, {}) size ({}, {})", chartWindows().front()->Name, firstChartAfter.Min.x, firstChartAfter.Min.y, firstChartAfter.GetWidth(), firstChartAfter.GetHeight(), firstChart.Min.x, firstChart.Min.y, firstChart.GetWidth(), firstChart.GetHeight());

                for (const int region : {0, 1, 0}) {
                    state.hideView = true;
                    state.waitUntil(ctx, "the chart windows are no longer drawn", [] { return std::ranges::none_of(chartWindows(), [](const ImGuiWindow* window) { return window != nullptr && window->Active; }); });
                    state.hideView = false;
                    state.regionId = region;
                    expectDockedInRegion(std::format("back under ID {} after frames without the view", region));
                    const ImRect firstChartBack = firstChartRect();
                    expect(near(firstChartBack.Min, firstChart.Min) && near(firstChartBack.Max, firstChart.Max)) << std::format("ID {}: '{}' keeps its place: ({}, {}) size ({}, {}), was ({}, {}) size ({}, {})", region, chartWindows().front()->Name, firstChartBack.Min.x, firstChartBack.Min.y, firstChartBack.GetWidth(), firstChartBack.GetHeight(), firstChart.Min.x, firstChart.Min.y, firstChart.GetWidth(), firstChart.GetHeight());
                }
                state.documentHost = false;
            };

            "every chart window of an embedded dashboard is docked inside the host rectangle"_test = [&] {
                state.view = std::make_unique<DigitizerUi::DashboardView>();
                state.waitUntil(ctx, "the six plot sinks are known", [] { return gState->dashboard->session.graphModel.recursiveGatherPlotSinks().size() >= 6UZ; });
                for (const Mode mode : {Mode::View, Mode::Interaction}) {
                    state.mode = mode;
                    opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
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

            "without a background the host shows through the chart windows and frames, the plots area and the bar"_test = [&] {
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);
                const auto   magentaShare = [&](bool background) {
                    state.background = background;
                    opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                    const CapturedPixels pixels  = captureSettledPixels(*ctx, chartsBounds());
                    const auto           magenta = std::ranges::count_if(pixels.rgba, [](unsigned int c) { return (c & 0xFFU) > 240U && ((c >> 8U) & 0xFFU) < 15U && ((c >> 16U) & 0xFFU) > 240U; });
                    return static_cast<double>(magenta) / static_cast<double>(pixels.rgba.size());
                };
                state.mode                                                            = Mode::View;
                state.hostBackground                                                  = ImVec4(1.f, 0.f, 1.f, 1.f);
                DigitizerUi::LookAndFeel::mutableInstance().chartStyle.plotBackground = ImVec4(0.f, 0.f, 0.f, 0.f);
                const ImVec4 frameBackground                                          = std::exchange(ImPlot::GetStyle().Colors[ImPlotCol_FrameBg], ImVec4(.3f, .3f, .4f, 1.f));
                const double withBackground                                           = magentaShare(true);
                const double withoutBackground                                        = magentaShare(false);
                captureScreenshot(*ctx, hostRect);
                DigitizerUi::LookAndFeel::mutableInstance().chartStyle = {};
                ImPlot::GetStyle().Colors[ImPlotCol_FrameBg]           = frameBackground;
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
                    opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
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
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                const auto width = [](const char* name) {
                    const ImGuiWindow* window = ImGui::FindWindowByName(name);
                    return window ? window->Size.x : 0.f;
                };
                const float widthRatio        = width("Plot1") / std::max(1.f, width("Plot2"));
                state.dashboard->windowLayout = {};
                expect(widthRatio > .8f && widthRatio < 1.25f) << std::format("Plot1 is {:.2f} times as wide as Plot2: the saved layout's halves, not the cells' 3", widthRatio);
            };

            "without a background the charts' frame is transparent while they draw, e.g. a SpectrumView's subplots"_test = [&] {
                const ImVec4 hostFrame = std::exchange(ImPlot::GetStyle().Colors[ImPlotCol_FrameBg], ImVec4(.3f, .3f, .4f, 1.f));
                state.frameProbe       = true;
                const auto frameAlpha  = [&](bool background) {
                    state.background       = background;
                    state.probedFrameAlpha = -1.f;
                    state.waitUntil(ctx, "a frame probed the frame colour", [&] { return state.probedFrameAlpha >= 0.f; });
                    return state.probedFrameAlpha;
                };
                const float withBackground                   = frameAlpha(true);
                const float withoutBackground                = frameAlpha(false);
                state.frameProbe                             = false;
                state.background                             = true;
                ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = hostFrame;
                expect(eq(withBackground, 1.f)) << "the host's frame colour";
                expect(eq(withoutBackground, 0.f)) << "transparent";
            };

            "a legend on top moves the charts below it"_test = [&] {
                state.mode   = Mode::View;
                state.legend = LegendPosition::Bottom;
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                const float topWithLegendBelow = topOfCharts();
                state.legend                   = LegendPosition::Top;
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                expect(topOfCharts() > topWithLegendBelow + 10.f) << std::format("top of charts {} with the legend on top, {} with it below", topOfCharts(), topWithLegendBelow);
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + kHostSize));
                state.legend = LegendPosition::Bottom;
            };

            "a legend on the left or right narrows the charts from that side, inside the host"_test = [&] {
                state.mode   = Mode::View;
                state.legend = LegendPosition::Bottom;
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                const ImRect withLegendBelow = chartsBounds();
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);

                state.legend = LegendPosition::Left;
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
                const ImRect withLegendLeft = chartsBounds();
                expect(withLegendLeft.Min.x > withLegendBelow.Min.x + 30.f) << std::format("left edge {} with the legend on the left, {} with it below", withLegendLeft.Min.x, withLegendBelow.Min.x);
                expect(hostRect.Contains(withLegendLeft));
                captureScreenshot(*ctx, hostRect);

                state.legend = LegendPosition::Right;
                opendigitizer::test::waitForStableCharts(ctx, *state.dashboard);
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
            auto&       state = *gState;
            if (!state.page) {
                state.page = std::make_unique<DigitizerUi::DashboardPage>();
                state.page->setDashboard(*state.dashboard);
            }
            state.page->draw(Mode::Interaction);
        };

        pageTest->TestFunc = [](ImGuiTestContext* ctx) {
            "the edit pane opened from the legend lies inside the host"_test = [ctx] {
                gState->waitUntil(ctx, "the six plot sinks are known", [] { return gState->dashboard->session.graphModel.recursiveGatherPlotSinks().size() >= 6UZ; });
                opendigitizer::test::waitForStableCharts(ctx, *gState->dashboard);
                ctx->ItemClick("**/PlotSink1", ImGuiMouseButton_Right);
                gState->waitUntil(ctx, "the edit pane has opened", [] { return gState->page->isEditPaneShown(); });

                const ImGuiWindow* panel = ImGui::FindWindowByName("BlockControlsPanel");
                expect(panel != nullptr && panel->Active) << fatal << "the edit pane is open";
                const ImRect hostRect(kHostPos, kHostPos + kHostSize);
                const ImRect panelRect(panel->Pos, panel->Pos + panel->Size);
                expect(hostRect.Contains(panelRect)) << std::format("edit pane at ({}, {}) size ({}, {}) inside the host at ({}, {})", panelRect.Min.x, panelRect.Min.y, panelRect.GetWidth(), panelRect.GetHeight(), hostRect.Min.x, hostRect.Min.y);
                captureScreenshot(*ctx, hostRect);
            };
            gState->page.reset();
        };

        ImGuiTest* remoteTest = IM_REGISTER_TEST(engine(), "dashboardview", "adding a remote signal adds its source, sink, edge and chart");
        remoteTest->GuiFunc   = pageTest->GuiFunc;
        remoteTest->TestFunc  = [](ImGuiTestContext* ctx) {
            "a remote signal gets a source, a sink fed by it, and a chart showing the sink"_test = [ctx] {
                auto& graphModel = gState->dashboard->session.graphModel;
                while (graphModel.recursiveGatherPlotSinks().size() < 6UZ) {
                    ctx->Yield();
                }
                DigitizerUi::SignalData signal{};
                signal.signalName  = "qa_remote_signal";
                signal.unit        = "V";
                signal.hostname    = "localhost";
                signal.protocol    = "http";
                signal.serviceName = "/GnuRadio/Acquisition";
                signal.port        = 1;
                gState->page->addSelectedRemoteSignal(signal);

                const auto findBlock = [&graphModel](auto&& predicate) {
                    DigitizerUi::UiGraphBlock* found = nullptr;
                    graphModel.recursiveForEachBlock([&](const DigitizerUi::UiGraphModel::FindBlockResult& result) {
                        if (result.block && predicate(*result.block)) {
                            found = result.block;
                        }
                        return DigitizerUi::UiGraphModel::VisitorResult::Recurse;
                    });
                    return found;
                };
                const auto chartFor = [](std::string_view sinkName) {
                    return std::ranges::any_of(gState->dashboard->uiWindows, [sinkName](const auto& w) { //
                        return w.block && w.block->typeName() == "opendigitizer::charts::XYChart" && std::ranges::contains(static_cast<const opendigitizer::charts::XYChart*>(w.block->raw())->data_sinks.value, sinkName);
                    });
                };
                DigitizerUi::UiGraphBlock* source = nullptr;
                DigitizerUi::UiGraphBlock* sink   = nullptr;
                while (!source || !sink || !chartFor(sink->blockName) || std::ranges::none_of(graphModel.rootBlock.childBlocks[0]->childEdges, [&](const DigitizerUi::UiGraphEdge& edge) { return edge.edgeSourcePort && edge.edgeDestinationPort && edge.edgeSourcePort->ownerBlock == source && edge.edgeDestinationPort->ownerBlock == sink; })) {
                    ctx->Yield();
                    source = findBlock([](const DigitizerUi::UiGraphBlock& block) { return block.blockTypeName.starts_with("opendigitizer::RemoteStreamSource"); });
                    sink   = source ? findBlock([source](const DigitizerUi::UiGraphBlock& block) { return block.isPlotSink() && block.blockName == source->blockUniqueName + "_sink"; }) : nullptr;
                }
                expect(sink->blockTypeName == "opendigitizer::ImPlotSink<float32>") << sink->blockTypeName;
            };
            gState->page.reset();
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboard_view";
    TestApp app(options);

    app.initImGui();
    TestState state;
    gState = std::addressof(state);

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_layout.grc", "embedded");
    state.emptyDashboard = std::make_unique<DigitizerUi::Dashboard>(state.restClient, DigitizerUi::DashboardDescription::createEmpty("never loaded"));

    const bool result = app.runTests();
    return result ? 0 : 1;
}
