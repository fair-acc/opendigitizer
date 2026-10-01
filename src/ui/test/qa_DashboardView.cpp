#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardView.hpp>

#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>

#include <format>
#include <memory>

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
    Mode                                        mode      = Mode::View;
    LegendPosition                              legend    = LegendPosition::Bottom;
    bool                                        drawEmpty = false;
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
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            auto&       state = *g_state;
            if (!state.view) {
                return;
            }
            DigitizerUi::DashboardView::Options options;
            options.legend = state.legend;
            std::ignore    = state.view->draw(state.drawEmpty ? *state.emptyDashboard : *state.dashboard, state.mode, options);
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

            "every chart window of an embedded dashboard is docked inside the host rectangle"_test = [&] {
                state.view = std::make_unique<DigitizerUi::DashboardView>();
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
            state.view.reset();
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
    state.view.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
