#include "ImGuiTestApp.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <ToolbarView.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <format>
#include <memory>

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
constexpr int    kMaxFrames = 600;

struct TestState {
    std::shared_ptr<DigitizerUi::Dashboard> dashboard;
    DigitizerUi::ToolbarView                view;
};

TestState* g_state = nullptr;

std::shared_ptr<gr::BlockModel> schedulerBlock(std::string_view name) {
    for (const auto& block : g_state->dashboard->scheduler->graph().blocks()) {
        if (block->name() == name) {
            return block;
        }
    }
    return nullptr;
}

std::vector<std::string> toolbarBlockNames() {
    std::vector<std::string> names;
    std::ranges::transform(g_state->view.blocks(), std::back_inserter(names), [](const auto& block) { return std::string(block->name()); });
    return names;
}

template<typename T>
bool waitForSetting(ImGuiTestContext* ctx, std::string_view blockName, const std::string& key, T expected) {
    for (int frame = 0; frame < kMaxFrames; ++frame) {
        if (const auto value = schedulerBlock(blockName)->settings().get(key); value && value->template value_or<T>(T{}) == expected) {
            return true;
        }
        ctx->Yield();
    }
    return false;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "toolbarview", "toolbar blocks of the flowgraph");

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(kHostSize);
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            auto&       state = *g_state;
            state.dashboard->handleMessages();
            if (state.dashboard->isInitialised) {
                state.view.draw(*state.dashboard);
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *g_state;
            for (int frame = 0; frame < kMaxFrames && !(state.dashboard->isInitialised && state.dashboard->graphModel.topologyGeneration > 0UZ); ++frame) {
                ctx->Yield();
            }
            ctx->Yield(3);

            "the ImGui toolbar blocks are drawn in graph order, those of other toolkits are not"_test = [&] {
                expect(toolbarBlockNames() == std::vector<std::string>{"toolbar_b_hold", "toolbar_a_amplify"}) << std::format("graph order, not by name or type: {}", toolbarBlockNames());
                expect(eq(ForeignToolbarBlock::drawCount, 0UZ)) << "the Qt block is never drawn";
                expect(schedulerBlock("toolbar_c_foreign") != nullptr) << "the Qt block is in the flowgraph";

                const ImGuiTestItemInfo checkbox = ctx->ItemInfo("**/Hold");
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
                ctx->ItemClick("**/Hold");
                expect(waitForSetting<bool>(ctx, "ClockSource1", "do_zero_order_hold", true)) << "checked";
                ctx->ItemClick("**/Hold");
                expect(waitForSetting<bool>(ctx, "ClockSource1", "do_zero_order_hold", false)) << "unchecked again";
            };

            "a toolbar block added to the running flowgraph appears at the end of the toolbar"_test = [&] {
                gr::Message message;
                message.cmd         = gr::message::Command::Set;
                message.endpoint    = gr::scheduler::property::kEmplaceBlock;
                message.serviceName = state.dashboard->graphModel.rootBlock.ownerSchedulerUniqueName();
                message.data        = gr::property_map{{"type", std::string("DigitizerUi::ToolbarButton")}, {"properties", gr::property_map{{"name", std::string("toolbar_d_extra")}, {"label", std::string("Extra")}}}};
                state.dashboard->graphModel.sendMessage(std::move(message));

                for (int frame = 0; frame < kMaxFrames && state.view.blocks().size() < 3UZ; ++frame) {
                    ctx->Yield();
                }
                expect(toolbarBlockNames() == std::vector<std::string>{"toolbar_b_hold", "toolbar_a_amplify", "toolbar_d_extra"}) << std::format("{}", toolbarBlockNames());
                ctx->Yield();
                expect(ctx->ItemInfo("**/Extra").ID != 0) << "the new button is drawn";
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "toolbar_view";
    TestApp app(options);
    auto    restClient = std::make_shared<opencmw::client::RestClient>();

    app.initImGui(); // also runs DigitizerUi::initialise()

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    std::ignore = gr::registerBlock<ForeignToolbarBlock>(registry);

    auto grcFile    = cmrc::ui_test_assets::get_filesystem().open("examples/qa_toolbar.grc");
    state.dashboard = DigitizerUi::Dashboard::create(restClient, DigitizerUi::DashboardDescription::createEmpty("toolbar"));
    state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [&](gr::Graph&& graph) { state.dashboard->emplaceGraph(std::move(graph)); });

    const bool result = app.runTests();
    state.view        = {};
    state.dashboard.reset();
    g_state = nullptr;
    return result ? 0 : 1;
}
