#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/SineSource.hpp"

#include <cmrc/cmrc.hpp>

#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

// Regression baseline for the dashboard refactoring: the screen rectangles of every chart window, for each layout type
// in View and Interaction mode, as the App's DashboardPage lays them out. The expected values were recorded from the
// implementation before the refactoring (test/baselines/dashboard_layout.txt); OD_WRITE_BASELINE=<file> records them anew.

namespace {
using Mode = DigitizerUi::DashboardPage::Mode;
using DigitizerUi::DockingLayoutType;

constexpr ImVec2 kHostSize{1024.f, 768.f};
constexpr float  kTolerancePixels = 1.f;

struct Scenario {
    DockingLayoutType layout;
    Mode              mode;
};

// Free first: the .grc rects are consumed by the first free layout
constexpr std::array kScenarios{Scenario{DockingLayoutType::Free, Mode::View}, Scenario{DockingLayoutType::Free, Mode::Interaction}, //
    Scenario{DockingLayoutType::Row, Mode::View}, Scenario{DockingLayoutType::Row, Mode::Interaction},                               //
    Scenario{DockingLayoutType::Column, Mode::View}, Scenario{DockingLayoutType::Column, Mode::Interaction},                         //
    Scenario{DockingLayoutType::Grid, Mode::View}, Scenario{DockingLayoutType::Grid, Mode::Interaction}};

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard>     dashboard;
    std::unique_ptr<DigitizerUi::DashboardPage> page;
    Scenario                                    scenario      = kScenarios.front();
    bool                                        layoutChanged = true;
};

TestState* g_state = nullptr;

std::string scenarioName(const Scenario& scenario) { return std::format("{}/{}", magic_enum::enum_name(scenario.layout), magic_enum::enum_name(scenario.mode)); }

// one line per chart window: "<scenario> <window name> <x> <y> <width> <height>", relative to the host window
std::vector<std::string> chartRectangles(const Scenario& scenario) {
    const ImGuiWindow* host = ImGui::FindWindowByName("Test Window");
    expect(host != nullptr) << fatal;
    std::vector<std::string> lines;
    for (const auto& uiWindow : g_state->dashboard->uiWindows) {
        if (!uiWindow.isChart()) {
            continue;
        }
        const ImGuiWindow* window = ImGui::FindWindowByName(uiWindow.window->name.c_str());
        expect(window != nullptr) << fatal << std::format("chart window '{}' exists", uiWindow.window->name);
        lines.push_back(std::format("{} {} {} {} {} {}", scenarioName(scenario), uiWindow.window->name, std::lround(window->Pos.x - host->Pos.x), std::lround(window->Pos.y - host->Pos.y), std::lround(window->Size.x), std::lround(window->Size.y)));
    }
    return lines;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream       stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

bool sameWithinTolerance(const std::string& expected, const std::string& actual) {
    std::istringstream expectedStream(expected);
    std::istringstream actualStream(actual);
    std::string        expectedScenario, expectedName, actualScenario, actualName;
    float              e[4]{}, a[4]{};
    expectedStream >> expectedScenario >> expectedName >> e[0] >> e[1] >> e[2] >> e[3];
    actualStream >> actualScenario >> actualName >> a[0] >> a[1] >> a[2] >> a[3];
    if (expectedScenario != actualScenario || expectedName != actualName) {
        return false;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(e[i] - a[i]) > kTolerancePixels) {
            return false;
        }
    }
    return true;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "dashboardbaseline", "chart rectangles per layout and mode");

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(kHostSize);

            auto& state = *g_state;
            state.dashboard->handleMessages();
            if (std::exchange(state.layoutChanged, false)) {
                state.dashboard->layoutType = state.scenario.layout; // the page follows the dashboard's layout type
            }
            if (!state.page && state.dashboard->isInitialised) { // as App::processAndRender does
                state.page = std::make_unique<DigitizerUi::DashboardPage>();
                state.page->setDashboard(*state.dashboard);
            }
            if (!state.page) {
                return;
            }
            state.page->draw(state.scenario.mode);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "chart rectangles equal the pre-refactoring baseline"_test = [ctx] {
                auto& state = *g_state;
                while (!state.page) {
                    ctx->Yield();
                }

                std::vector<std::string> actual;
                for (const auto& scenario : kScenarios) {
                    state.scenario      = scenario;
                    state.layoutChanged = true;
                    std::vector<std::string> previous;
                    std::size_t              stableFrames = 0UZ;
                    for (std::size_t frame = 0UZ; frame < 300UZ && stableFrames < 5UZ; ++frame) {
                        ctx->Yield();
                        auto current = chartRectangles(scenario);
                        stableFrames = current == previous ? stableFrames + 1UZ : 0UZ;
                        previous     = std::move(current);
                    }
                    expect(stableFrames >= 5UZ) << fatal << std::format("{}: chart windows settle", scenarioName(scenario));
                    expect(!previous.empty()) << fatal << std::format("{}: charts are drawn", scenarioName(scenario));
                    actual.insert(actual.end(), previous.begin(), previous.end());
                }

                if (const char* writeTo = std::getenv("OD_WRITE_BASELINE")) {
                    std::ofstream out(writeTo);
                    for (const auto& line : actual) {
                        out << line << '\n';
                    }
                    std::println("baseline written to {} ({} lines)", writeTo, actual.size());
                    return;
                }

                const auto file     = cmrc::ui_test_assets::get_filesystem().open("baselines/dashboard_layout.txt");
                const auto expected = splitLines(std::string(file.begin(), file.end()));
                expect(eq(actual.size(), expected.size())) << fatal << "same number of chart windows over all scenarios";
                for (std::size_t i = 0UZ; i < expected.size(); ++i) {
                    expect(sameWithinTolerance(expected[i], actual[i])) << std::format("expected '{}', got '{}'", expected[i], actual[i]);
                }
            };

            "a destroyed dashboard page leaves no chart request handler behind"_test = [] {
                auto& state = *g_state;
                expect(opendigitizer::charts::g_chartRequests != nullptr) << fatal << "the drawing page receives chart requests";
                state.page.reset();
                expect(opendigitizer::charts::g_chartRequests == nullptr) << "no handler pointing into a destroyed page";
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "dashboard_baseline";
    TestApp app(options);
    auto    restClient = std::make_shared<opencmw::client::RestClient>();

    app.initImGui(); // Dashboard construction touches the ImGui style

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);

    auto grcFile    = cmrc::ui_test_assets::get_filesystem().open("examples/qa_layout.grc");
    state.dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("empty"));
    state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { state.dashboard->emplaceGraph(std::move(graph)); });

    const bool result = app.runTests();
    state.page.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
