#include "App.hpp"
#include "ImGuiTestApp.hpp"
#include "TestDashboardRunner.hpp"
#include "TestPropertySetterButton.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#include <Dashboard.hpp>
#include <ToolbarView.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <vector>

using namespace gr::od;

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct ForeignToolbarBlock : gr::Block<ForeignToolbarBlock, gr::Drawable<gr::UICategory::Toolbar, "Qt">> {
    using Description = gr::Doc<"toolbar block of another toolkit, which the ImGui toolbar must not draw">;

    inline static std::size_t drawCount = 0UZ;

    GR_MAKE_REFLECTABLE(ForeignToolbarBlock);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        ++drawCount;
        return gr::work::Status::OK;
    }
};

namespace {
constexpr ImVec2 kHostPos{100.f, 80.f};
constexpr ImVec2 kHostSize{800.f, 200.f};

struct TestState : test::TestDashboardRunner {
    std::string grc;
    ToolbarView view;
};

TestState* gState = nullptr;

template<typename T>
bool waitForSetting(ImGuiTestContext* ctx, std::string_view blockName, std::string_view key, T expected) {
    return gState->waitUntil(ctx, std::format("{}.{} is set", blockName, key), [=] { return gState->blockHasBasicTypeKeyValuePair<T>(blockName, key, expected); });
}
} // namespace

const boost::ut::suite<"planToolbar"> planTests = [] {
    constexpr toolbar::ItemWidths          kSlider{.natural = 200.f, .minimum = 100.f, .label = 30.f};
    const std::vector<toolbar::ItemWidths> sliders(3UZ, kSlider);
    const auto                             slotIs = [](const ToolbarSlot& slot, std::size_t row, float width, bool label) { return slot.row == row && std::abs(slot.width - width) < 1e-3f && slot.showLabel == label; };

    "a row wide enough keeps the natural widths"_test = [&] { // 3·200 + 2·10 = 620 ≤ 700
        const auto slots = planToolbar(sliders, 700.f, 10.f);
        expect(std::ranges::all_of(slots, [&](const ToolbarSlot& slot) { return slotIs(slot, 0UZ, 200.f, true); }));
    };

    "a narrower row shrinks the items evenly"_test = [&] { // free 500 - 20 - 300 = 180 of 300: 100 + 0.6·100
        const auto slots = planToolbar(sliders, 500.f, 10.f);
        expect(std::ranges::all_of(slots, [&](const ToolbarSlot& slot) { return slotIs(slot, 0UZ, 160.f, true); }));
    };

    "below the minimum row the items wrap, each row growing back"_test = [&] { // 100+10+100 = 210 ≤ 250 < 320
        const auto slots = planToolbar(sliders, 250.f, 10.f);
        expect(slotIs(slots[0], 0UZ, 120.f, true) && slotIs(slots[1], 0UZ, 120.f, true)) << "row 1: 40 free of 200";
        expect(slotIs(slots[2], 1UZ, 200.f, true)) << "row 2: room for the natural width";
    };

    "when two rows do not hold them, the labels are dropped"_test = [&] { // without labels: minimum 70, natural 170
        const auto slots = planToolbar(sliders, 150.f, 10.f);
        expect(slotIs(slots[0], 0UZ, 70.f, false) && slotIs(slots[1], 0UZ, 70.f, false)) << "70+10+70 = 150";
        expect(slotIs(slots[2], 1UZ, 150.f, false)) << "row 2: 80 free of 100";
    };

    "what two rows without labels cannot hold is clipped at the end of the second"_test = [&] {
        const auto slots = planToolbar(sliders, 60.f, 10.f);
        expect(slotIs(slots[0], 0UZ, 70.f, false) && slotIs(slots[1], 1UZ, 70.f, false) && slotIs(slots[2], 1UZ, 70.f, false));
    };

    "a fixed item keeps its width while the others shrink"_test = [&] { // free 500 - 30 - 340 = 130 of 300
        std::vector<toolbar::ItemWidths> items{{.natural = 40.f, .minimum = 40.f, .label = 0.f}};
        items.insert(items.end(), sliders.begin(), sliders.end());
        const auto slots = planToolbar(items, 500.f, 10.f);
        expect(slotIs(slots[0], 0UZ, 40.f, true) && slotIs(slots[1], 0UZ, 100.f + 130.f / 3.f, true));
    };
};

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "toolbarview", "toolbar blocks of the flowgraph");

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            imw::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            auto&       state = *gState;
            state.dashboard->session.receiveMessages();
            if (state.dashboard->isInitialised) {
                state.view.draw(state.dashboard->session, state.dashboard->schedulerUi);
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *gState;
            state.waitUntil(ctx, "the toolbar blocks are drawn", [&] { return state.dashboard->isInitialised && state.dashboard->session.graphModel.topologyGeneration > 0UZ && ctx->ItemExists("**/Amplify") && ctx->ItemExists("**/##Hold"); });

            "the ImGui toolbar blocks are drawn in graph order, those of other toolkits are not"_test = [&] {
                expect(eq(ForeignToolbarBlock::drawCount, 0UZ)) << "the Qt block is never drawn";
                expect(state.findBlockInRootGraph("toolbar_c_foreign") != nullptr) << "the Qt block is in the flowgraph";

                const ImGuiTestItemInfo checkbox = ctx->ItemInfo("**/##Hold");
                const ImGuiTestItemInfo button   = ctx->ItemInfo("**/Amplify");
                expect(checkbox.ID != 0 && button.ID != 0) << fatal << "both items are drawn";
                expect(checkbox.RectFull.Max.x <= button.RectFull.Min.x) << std::format("checkbox right edge {} left of the button at {}", checkbox.RectFull.Max.x, button.RectFull.Min.x);
                const ImRect host(kHostPos, kHostPos + kHostSize);
                expect(host.Contains(checkbox.RectFull) && host.Contains(button.RectFull)) << "drawn inside the host";
            };

            "pressing the button sets its payload on the target block in the flowgraph"_test = [&] {
                expect(waitForSetting<float>(ctx, "SignalGenerator1", "amplitude", 5.f)) << fatal << "initial amplitude from the .grc";
                ctx->ItemClick("**/Amplify");
                expect(waitForSetting<float>(ctx, "SignalGenerator1", "amplitude", 7.f)) << "amplitude applied by the signal generator";
            };

            "checking the checkbox sets the boolean setting of the target block in the flowgraph"_test = [&] {
                expect(waitForSetting<bool>(ctx, "ClockSource1", "do_zero_order_hold", false)) << fatal << "initial value from the .grc";
                ctx->ItemClick("**/##Hold");
                expect(waitForSetting<bool>(ctx, "ClockSource1", "do_zero_order_hold", true)) << "checked";
                ctx->ItemClick("**/##Hold");
                expect(waitForSetting<bool>(ctx, "ClockSource1", "do_zero_order_hold", false)) << "unchecked again";
            };

            "without scheduler_ui the toolbar has no scheduler controls"_test = [&] {
                expect(!state.dashboard->schedulerUi) << "the .grc does not ask for them";
                expect(!ctx->ItemExists("**/###schedulerPlay"));
            };

            "with scheduler_ui, play, pause and stop drive the scheduler and follow its state"_test = [&] {
                using enum gr::lifecycle::State;
                const auto enabled = [ctx](const char* ref) { return (ctx->ItemInfo(ref).ItemFlags & ImGuiItemFlags_Disabled) == 0; };
                const auto reach   = [&](gr::lifecycle::State target) {
                    state.waitUntil(ctx, std::format("the scheduler is {}", gr::meta::enumName(target).value_or("?")), [&] { return state.dashboard->session.state() == target; });
                    ctx->Yield(); // drawn once in that state
                    return state.dashboard->session.state() == target;
                };
                state.dashboard->schedulerUi = true;
                expect(reach(RUNNING)) << fatal;
                expect(!enabled("**/###schedulerPlay") && enabled("**/###schedulerPause") && enabled("**/###schedulerStop")) << "running";

                ctx->ItemClick("**/###schedulerPause");
                expect(reach(PAUSED)) << fatal << "pause";
                expect(enabled("**/###schedulerPlay") && !enabled("**/###schedulerPause") && enabled("**/###schedulerStop")) << "paused";

                ctx->ItemClick("**/###schedulerPlay");
                expect(reach(RUNNING)) << fatal << "play resumes";

                ctx->ItemClick("**/###schedulerStop");
                expect(reach(STOPPED)) << fatal << "stop";
                expect(enabled("**/###schedulerPlay") && !enabled("**/###schedulerPause") && !enabled("**/###schedulerStop")) << "stopped";

                ctx->ItemClick("**/###schedulerPlay");
                expect(reach(RUNNING)) << fatal << "play starts again";
                state.dashboard->schedulerUi = false;
            };

            "scheduler_ui is read from the dashboard section and saved only when set"_test = [&] {
                const auto savedHas = [](const gr::property_map& graphYaml) {
                    const auto section = graphYaml.find_value(std::string("dashboard"), std::pmr::get_default_resource()); // kept alive: get_if refers into it
                    if (!section) {
                        return false;
                    }
                    const auto dashboard = section->get_if<gr::property_map>();
                    return dashboard && dashboard->contains("scheduler_ui");
                };
                expect(!savedHas(state.dashboard->serialise().second)) << "a dashboard without it saves without it";
                state.dashboard->schedulerUi = true;
                expect(savedHas(state.dashboard->serialise().second)) << "a dashboard with it saves it";
                state.dashboard->schedulerUi = false;

                std::string withControls = state.grc;
                withControls.replace(withControls.find("  layout: Free"), std::string_view("  layout: Free").size(), "  layout: Free\n  scheduler_ui: true");
                auto loaded = std::make_unique<Dashboard>(state.restClient, DashboardDescription::createEmpty("with controls"));
                loaded->loadAndThen(withControls, [](gr::Graph&&) {});
                expect(loaded->schedulerUi) << "read from the .grc";
            };

            "a toolbar block added to the running flowgraph appears at the end of the toolbar"_test = [&] {
                state.dashboard->session.graphModel.send(gr::message::Command::Set, state.dashboard->session.graphModel.rootBlock.ownerSchedulerUniqueName(), std::string(gr::scheduler::property::kEmplaceBlock), {{"type", std::string("opendigitizer::test::TestPropertySetterButton")}, {"properties", gr::property_map{{"name", std::string("toolbar_d_extra")}, {"label", std::string("Extra")}}}});

                state.waitUntil(ctx, "the new button is drawn", [ctx] { return ctx->ItemExists("**/Extra"); });
                const ImGuiTestItemInfo extra   = ctx->ItemInfo("**/Extra");
                const ImGuiTestItemInfo amplify = ctx->ItemInfo("**/Amplify");
                expect(extra.ID != 0) << fatal << "the new button is drawn";
                expect(amplify.RectFull.Max.x <= extra.RectFull.Min.x) << std::format("after the last button: Amplify ends at {}, Extra starts at {}", amplify.RectFull.Max.x, extra.RectFull.Min.x);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "toolbar_view";
    TestApp app(options);

    app.initImGui();
    TestState state;
    gState = std::addressof(state);

    auto& registry = gr::globalBlockRegistry();
    registerAppBlocks(registry);
    std::ignore = gr::registerBlock<ForeignToolbarBlock>(registry);
    registerWithIdPrefix<test::TestPropertySetterButton>(registry, kOpendigitizerIdPrefix);

    auto grcFile = cmrc::ui_test_assets::get_filesystem().open("examples/qa_toolbar.grc");
    state.grc    = std::string(grcFile.begin(), grcFile.end());
    state.reloadFromYamlString(state.grc, "toolbar");

    const bool result = app.runTests();
    return result ? 0 : 1;
}
