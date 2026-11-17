#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

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

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

namespace {
using Mode = DigitizerUi::DashboardView::Mode;

struct TestState : opendigitizer::test::TestDashboardRunner {
    std::unique_ptr<DigitizerUi::DashboardPage>     page;
    std::unique_ptr<DigitizerUi::OpenDashboardPage> selection;
    DigitizerUi::components::AppHeader              header;
    bool                                            showSelection = false;
};

TestState* g_state = nullptr;

bool expectedCompact() {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    return std::min(size.x, size.y) < DigitizerUi::LookAndFeel::instance().compactEnterShortSide;
}

} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "compactlayout", "phone-sized window");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto&                state    = *g_state;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            IMW::Window window("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            state.header.draw(state.dashboard->description->name, DigitizerUi::LookAndFeel::instance().fontLarge(), DigitizerUi::LookAndFeel::instance().style);
            IMW::Child pageArea("##pageArea", ImVec2{}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
            if (state.showSelection) {
                state.selection->draw(state.dashboard.get(), state.page.get());
            } else {
                state.page->draw(Mode::Interaction);
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto&       state       = *g_state;
            const auto& lookAndFeel = DigitizerUi::LookAndFeel::instance();

            "the compact layout follows the shorter viewport side, with hysteresis"_test = [&] {
                auto& mutableLookAndFeel = DigitizerUi::LookAndFeel::mutableInstance();
                mutableLookAndFeel.updateFormFactor({1600.f, 1000.f});
                expect(!lookAndFeel.compact && eq(ImGui::GetStyle().FontScaleMain, 1.f));
                mutableLookAndFeel.updateFormFactor({412.f, 915.f});
                expect(lookAndFeel.compact && lookAndFeel.portrait && eq(ImGui::GetStyle().FontScaleMain, lookAndFeel.compactFontScale));
                mutableLookAndFeel.updateFormFactor({915.f, 412.f});
                expect(lookAndFeel.compact && !lookAndFeel.portrait);
                mutableLookAndFeel.updateFormFactor({620.f, 900.f});
                expect(lookAndFeel.compact) << "stays compact until the leave threshold";
                mutableLookAndFeel.updateFormFactor({700.f, 900.f});
                expect(!lookAndFeel.compact);
                mutableLookAndFeel.updateFormFactor({620.f, 900.f});
                expect(!lookAndFeel.compact) << "stays desktop until the enter threshold";
                mutableLookAndFeel.updateFormFactor({412.f, 915.f});
                mutableLookAndFeel.updateFormFactor({0.f, 0.f});
                expect(lookAndFeel.compact) << "a minimised window keeps its layout";
                ctx->Yield();
                expect(eq(lookAndFeel.compact, expectedCompact())) << "the next frame follows the window again";
                expect(eq(ImGui::GetStyle().FontScaleMain, expectedCompact() ? lookAndFeel.compactFontScale : 1.f));
            };

            state.showSelection = false;
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto              options = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    const std::string prefix  = std::format("compact_layout_{}x{}", static_cast<int>(options.windowSize.x), static_cast<int>(options.windowSize.y));
    options.screenshotPrefix  = prefix.c_str();
    TestApp app(options);

    app.initImGui();
    state.header.loadAssets();

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);

    state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_layout.grc", "CompactLayoutDashboard");
    state.page = std::make_unique<DigitizerUi::DashboardPage>();
    state.page->setDashboard(*state.dashboard);

    state.selection                        = std::make_unique<DigitizerUi::OpenDashboardPage>(state.restClient);
    state.selection->requestLoadDashboard  = [](const std::shared_ptr<const DigitizerUi::DashboardDescription>&) {};
    state.selection->requestCloseDashboard = [] {};
    state.selection->addDashboard("example://builtin-samples");

    const bool result = app.runTests();
    state.selection.reset();
    state.page.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
