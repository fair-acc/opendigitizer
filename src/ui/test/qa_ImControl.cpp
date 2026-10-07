#include "ImGuiTestApp.hpp"
#include "StandaloneUiControl.hpp"
#include "TestDashboardRunner.hpp"
#include "TestingBlockInspectionUtils.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <ToolbarView.hpp>
#include <blocks/ImControlText.hpp>
#include <blocks/ImControlToggle.hpp>
#include <blocks/ImControlTrigger.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace opendigitizer::test;

namespace {
constexpr ImVec2 kDashboardWindowPos{40.f, 80.f};
constexpr ImVec2 kUiControlWindowPos{40.f, 240.f};
constexpr ImVec2 kWindowSize{900.f, 120.f};

struct TestState : TestDashboardRunner {
    DigitizerUi::ToolbarView                                          view;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlToggle>>  toggle;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlTrigger>> trigger;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlText>>    text;
    bool                                                              showLabels = true;
};

TestState g_state;

TESTING_BLOCK_INSPECTION_UTILS_MAKE_ALL_GLOBAL_STATE_AWARE_OVERLOADS(g_state.dashboard->session)

template<typename T>
std::size_t countMatchingSettingsMessages(const std::vector<gr::Message>& messages, std::string_view target, const std::string& key, const T& expected) {
    return static_cast<std::size_t>(std::ranges::count_if(messages, [&](const gr::Message& message) {
        if (message.cmd != gr::message::Command::Set || message.serviceName != target || message.endpoint != gr::block::property::kSetting || !message.data) {
            return false;
        }
        const gr::pmt::Value value = message.data->find_value(key).value_or(gr::pmt::Value{});
        if constexpr (std::is_same_v<T, std::string>) {
            return value.value_or(std::string{}) == expected;
        } else {
            const T* typed = value.get_if<T>();
            return typed != nullptr && *typed == expected;
        }
    }));
}

template<typename T>
bool containsMatchingMessage(const std::vector<gr::Message>& messages, std::string_view target, const std::string& key, const T& expected) {
    return countMatchingSettingsMessages<T>(messages, target, key, expected) > 0UZ;
}

void sendSetTargetMapMessage(const std::string& control, const std::string& targetMap) {
    gr::Message message;
    message.cmd         = gr::message::Command::Set;
    message.serviceName = control;
    message.endpoint    = gr::block::property::kSetting;
    message.data        = gr::property_map{{"target_map", targetMap}};
    g_state.dashboard->session.sendMessage(std::move(message));
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        static constexpr auto commonGuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kDashboardWindowPos);
            ImGui::SetNextWindowSize(kWindowSize);
            {
                IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                // draw the toolbar for the dashboard, if the dashboard is active
                if (g_state.dashboard) {
                    g_state.dashboard->handleMessages();
                    if (g_state.dashboard->isInitialised) {
                        g_state.view.draw(g_state.dashboard->session, false);
                    }
                }

                // draw the standalone controls, which are not used in all tests but always drawn so as to only have one gui function
                ImGui::SetNextWindowPos(kUiControlWindowPos);
                ImGui::SetNextWindowSize(kWindowSize);
                {
                    IMW::Window            uiControlPanel("uiControlPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
                    const gr::property_map config{{"show_label", g_state.showLabels}};
                    std::ignore = g_state.toggle->block.draw(config);
                    ImGui::SameLine();
                    std::ignore = g_state.trigger->block.draw(config);
                    ImGui::SameLine();
                    std::ignore = g_state.text->block.draw(config);
                }
            }
        };

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "imcontrol_conflicting_init_values", "controls whose init values disagree with their targets adopt the target's value instead of overwriting it");

            t->GuiFunc = commonGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_imcontrol_conflicting_init_values.grc", "conflicting_init_values");
                g_state.waitForScheduler(ctx);

                "the control blocks get their init values from blocks and do not modify the blocks"_test = [&] {
                    using namespace std::chrono_literals;
                    // As updates are delivered via messages, time to delivery is not deterministic, so we can spin and make sure the asserts continue to be true
                    for (const auto start = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - start < 2s;) {
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase1DefaultTrueBlock", "visible", true)) << "block keeps its default despite state of control";
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase1DefaultFalseBlock", "visible", false)) << "block keeps its default despite state of control";
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase2DefaultTrueBlock", "visible", true)) << "block keeps its default despite state of control";
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase2DefaultFalseBlock", "visible", false)) << "block keeps its default despite state of control";
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase3DefaultTrueBlock", "visible", true)) << "block keeps its default despite state of control";
                        expect(blockHasBasicTypeKeyValuePair<bool>("testCase3DefaultFalseBlock", "visible", false)) << "block keeps its default despite state of control";
                        ctx->Yield();
                    }

                    // at any point during the above wait, the opposite should have occurred, and the controls should have received info about their initial values / defaults
                    expect(blockHasBasicTypeKeyValuePair<bool>("testCase1ToggleTrueToTrue", "value", true)) << "control value matches attached block's value";
                    expect(blockHasBasicTypeKeyValuePair<bool>("testCase1ToggleFalseToFalse", "value", false)) << "control value matches attached block's value";
                    expect(blockHasBasicTypeKeyValuePair<bool>("testCase3ToggleTrueToFalse", "value", true)) << "control value matches attached block's value";
                    expect(blockHasBasicTypeKeyValuePair<bool>("testCase3ToggleFalseToTrue", "value", false)) << "control value matches attached block's value";
                };
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "imcontrol", "test ImControl* widgets which should affect the state of blocks in the flowgraph");

            t->GuiFunc = commonGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                g_state.reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_imcontrol.grc", "controls");
                g_state.waitForScheduler(ctx);
                g_state.waitUntil(ctx, "flowgraph appears to be initialized and running", [] { return blockHasStringKeyValuePair("plotSink1", "signal_name", "first"); });

                "some controls are drawn, at least"_test = [&] {
                    for (const char* item : {"**/##Visible", "**/Show", "**/##Name"}) {
                        expect(ctx->ItemExists(item)) << item;
                    }
                    captureScreenshot(*ctx, ImRect(kDashboardWindowPos, kDashboardWindowPos + kWindowSize));
                };

                "checkbox loads as checked, based on the state of the block. then test basic functionality"_test = [&] {
                    expect(blockHasBasicTypeKeyValuePair<bool>("plotSink1", "visible", true)) << fatal << "the target starts true";
                    g_state.waitUntil(ctx, "checkbox is checked, informed by target block", [] { return blockHasBasicTypeKeyValuePair<bool>("controlToggle", "value", true); });
                    ctx->ItemClick("**/##Visible");
                    g_state.waitUntil(ctx, "checkbox is unchecked", [] { return blockHasBasicTypeKeyValuePair<bool>("plotSink1", "visible", false) && blockHasBasicTypeKeyValuePair<bool>("controlToggle", "value", false); });
                    ctx->ItemClick("**/##Visible");
                    g_state.waitUntil(ctx, "checkbox is re-checked", [] { return blockHasBasicTypeKeyValuePair<bool>("plotSink1", "visible", true) && blockHasBasicTypeKeyValuePair<bool>("controlToggle", "value", true); });
                };

                "trigger basic functionality"_test = [&] {
                    expect(blockHasBasicTypeKeyValuePair<bool>("plotSink2", "visible", false)) << fatal << "the target starts false";
                    ctx->ItemClick("**/Show");
                    g_state.waitUntil(ctx, "the click sends a set:true message", [] { return blockHasBasicTypeKeyValuePair<bool>("plotSink2", "visible", true); });
                };

                "the text input sets the string on each named block"_test = [&] {
                    expect(blockHasStringKeyValuePair("plotSink1", "signal_name", "first")) << fatal << "signal name starts at \"first\", as is written in the .grc";
                    expect(blockHasStringKeyValuePair("plotSink2", "signal_name", "second")) << fatal << "signal name starts at \"second\", as is written in the .grc";
                    // not sure whether it is really an important contract that the first block listed in the target_map is the one that determines the default value for the control, but for now that is the behavior
                    g_state.waitUntil(ctx, "text input should inherit the initial value from its first target", [] { return blockHasStringKeyValuePair("controlText", "value", "first"); });
                    ctx->ItemInputValue("**/##Name", "foo bar"); // ui control block with name "controlText" has label "Name"
                    g_state.waitUntil(ctx, "text input got propagated to all target blocks", [] { return blockHasStringKeyValuePair("plotSink1", "signal_name", "foo bar") && blockHasStringKeyValuePair("plotSink2", "signal_name", "foo bar"); });
                };

                "changing the target of a control"_test = [&] {
                    expect(blockHasBasicTypeKeyValuePair<bool>("plotSink1", "visible", true)) << fatal << "visible targets of toggle start out visible";
                    expect(blockHasBasicTypeKeyValuePair<bool>("plotSink2", "visible", true)) << fatal << "visible targets of toggle start out visible";
                    sendSetTargetMapMessage("controlToggle", "plotSink2:visible");
                    g_state.waitUntil(ctx, "the new target_map appears in the block settings", [] { return setting("controlToggle", "target_map").value_or(std::string{}) == "plotSink2:visible"; });
                    ctx->ItemClick("**/##Visible");
                    g_state.waitUntil(ctx, "the new target is set", [] { return blockHasBasicTypeKeyValuePair<bool>("plotSink2", "visible", false); });

                    // state of old block seems to continue to be good, though this is not a complete test
                    using namespace std::chrono_literals;
                    for (const auto start = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - start < 2s;) {
                        expect(blockHasBasicTypeKeyValuePair<bool>("plotSink1", "visible", true)) << "no longer targeted block is not affected by control";
                        ctx->Yield();
                    }
                };
            };
        }

        {
            ImGuiTest* t = IM_REGISTER_TEST(engine(), "imcontrol_standalone", "test ImControl blocks which are outside of a flowgraph, verify that the messages they send look sane");

            t->GuiFunc = commonGuiFunc;

            t->TestFunc = [](ImGuiTestContext* ctx) {
                "a toggle sends a bool to each named block, checked and unchecked"_test = [&] {
                    std::ignore = g_state.toggle->takeSent();
                    ctx->ItemClick("**/##Enabled");
                    const auto messagesAfterCheck = g_state.toggle->takeSent();
                    expect(containsMatchingMessage(messagesAfterCheck, "TargetA", "enabled", true) && containsMatchingMessage(messagesAfterCheck, "TargetB", "enabled", true)) << "sends true to TargetA and TargetB";
                    ctx->ItemClick("**/##Enabled");
                    const auto messagesAfterUncheck = g_state.toggle->takeSent();
                    expect(containsMatchingMessage(messagesAfterUncheck, "TargetA", "enabled", false) && containsMatchingMessage(messagesAfterUncheck, "TargetB", "enabled", false)) << "sends false to TargetA and TargetB";
                };

                "trigger sending a message per click"_test = [&] {
                    std::ignore = g_state.trigger->takeSent();
                    for (int click = 0; click < 3; ++click) {
                        ctx->ItemClick("**/Fire");
                    }
                    const auto sent = g_state.trigger->takeSent();
                    expect(eq(countMatchingSettingsMessages(sent, "TargetA", "fire", true), 3UZ)) << std::format("three clicks, {} messages", sent.size());
                    expect(sent.size() == 3UZ) << "Excess messages sent by ui control, its only job is to send set messages";
                };

                "text input control only sends a message when the user presses enter or the control is unfocused"_test = [&] {
                    std::ignore = g_state.text->takeSent();
                    expect(g_state.text->block.value != std::string("ramp")) << fatal << "test's input string must be different than initial string";
                    ctx->ItemInput("**/##Title");
                    ctx->KeyCharsReplace("ramp");
                    g_state.waitUntil(ctx, "text input stores the edited value", [] { return g_state.text->block.value == std::string("ramp"); });
                    expect(g_state.text->takeSent().empty()) << "nothing sent because text is still focused";
                    ctx->KeyPress(ImGuiKey_Enter);
                    // text block should synchronously output the set text message, no need to yield or wait
                    expect(containsMatchingMessage<std::string>(g_state.text->takeSent(), "TargetA", "caption", std::string("ramp"))) << "Enter sends the text";
                };

                "ImControl derivative widgets responsively render with or without labels depending on available space"_test = [&] {
                    const float                  labelWidth      = ImGui::CalcTextSize("Enabled").x + ImGui::GetStyle().ItemInnerSpacing.x;
                    const float                  labelledWidth   = ctx->ItemInfo("**/##Enabled").RectFull.Min.x;
                    Digitizer::utils::scope_exit resetShowLabels = [old = g_state.showLabels] { g_state.showLabels = old; };
                    g_state.showLabels                           = false;
                    ctx->Yield(); // redraw with new config that looks at showLabels
                    const float unlabelledWidth = ctx->ItemInfo("**/##Enabled").RectFull.Min.x;
                    expect(std::abs(labelledWidth - unlabelledWidth - labelWidth) < 0.5f) << std::format("the checkbox moved left by the label's {} px: {} -> {}", labelWidth, labelledWidth, unlabelledWidth);
                    captureScreenshot(*ctx, ImRect(kUiControlWindowPos, kUiControlWindowPos + kWindowSize));
                };
            };
        }
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "imcontrol";
    TestApp app(options);

    app.initImGui();

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    g_state.toggle  = std::make_unique<StandaloneControl<DigitizerUi::ImControlToggle>>(gr::property_map{{"name", "standalone_toggle"}, {"label", "Enabled"}, {"value", false}, {"target_map", "TargetA,TargetB:enabled"}});
    g_state.trigger = std::make_unique<StandaloneControl<DigitizerUi::ImControlTrigger>>(gr::property_map{{"name", "standalone_trigger"}, {"label", "Fire"}, {"target_map", "TargetA:fire"}});
    g_state.text    = std::make_unique<StandaloneControl<DigitizerUi::ImControlText>>(gr::property_map{{"name", "standalone_text"}, {"label", "Title"}, {"value", "start"}, {"target_map", "TargetA:caption"}});

    const bool result = app.runTests();
    g_state.unloadDashboard(); // ensure scheduler cleanup before global teardown
    return result ? 0 : 1;
}
