#include "ImGuiTestApp.hpp"
#include "StandaloneUiControl.hpp"
#include "TestDashboardRunner.hpp"
#include "TestingBlockInspectionUtils.hpp"

#include <ClientCommon.hpp>
#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>

#include <Dashboard.hpp>
#include <ToolbarView.hpp>
#include <blocks/ImControlNumber.hpp>
#include <common/LookAndFeel.hpp>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace opendigitizer::test;

namespace {
constexpr ImVec2 kHostPos{40.f, 80.f};
constexpr ImVec2 kHostSize{900.f, 120.f};
constexpr ImVec2 kStandalonePos{40.f, 240.f};

constexpr std::uint64_t kValueBeforeExceedingDoublePrecision   = 1ULL << std::numeric_limits<double>::digits;
constexpr std::uint64_t kUint64ValueThatExceedsDoublePrecision = kValueBeforeExceedingDoublePrecision + 1ULL;

struct TestState : opendigitizer::test::TestDashboardRunner {
    DigitizerUi::ToolbarView                                         view;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> steps;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> ratio;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> byte;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> fraction;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> coarseControl;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> rateControl;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> toggleMinAndMaxControl;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> doubleConversionTestControl;
    bool                                                             isMinMaxEnabledForToggleMinMaxControl = false;
    float                                                            hostWidth                             = kHostSize.x;
};

TestState g_state;

TESTING_BLOCK_INSPECTION_UTILS_MAKE_ALL_GLOBAL_STATE_AWARE_OVERLOADS(g_state.dashboard->session)

template<typename T>
gr::property_map configWithMinMaxHints(T value, std::optional<double> min = std::nullopt, std::optional<double> max = std::nullopt) {
    return DigitizerUi::DrawableBlockConfig{.firstControlledProperty = {.value = value, .hints = {.min = min, .max = max}}, .aggregateControlledPropertyHints = {.min = min, .max = max}}.toMap().value_or(gr::property_map{});
}

template<typename T>
bool sentExactly(const std::vector<gr::Message>& messages, std::string_view target, const std::string& key, T expected) {
    return std::ranges::any_of(messages, [&](const gr::Message& message) {
        if (message.cmd != gr::message::Command::Set || message.serviceName != target || message.endpoint != gr::block::property::kSetting || !message.data) {
            return false;
        }
        const gr::pmt::Value value = message.data->find_value(key).value_or(gr::pmt::Value{});
        const T*             typed = value.get_if<T>();
        return typed != nullptr && *typed == expected;
    });
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "imcontrolnumber", "number controls in the toolbar");

        t->GuiFunc = [](ImGuiTestContext*) { // NOSONAR test lambda length
            ImGui::SetNextWindowPos(kHostPos);
            ImGui::SetNextWindowSize(ImVec2(g_state.hostWidth, kHostSize.y));
            IMW::Window window("Host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            g_state.dashboard->handleMessages();
            if (g_state.dashboard->isInitialised) {
                g_state.view.draw(g_state.dashboard->session, false);
            }
            ImGui::SetNextWindowPos(kStandalonePos);
            ImGui::SetNextWindowSize(kHostSize);
            IMW::Window standalone("Standalone", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            std::ignore = g_state.steps->block.draw(configWithMinMaxHints<std::int16_t>(0, -10., 10.));
            ImGui::SameLine();
            std::ignore = g_state.ratio->block.draw(configWithMinMaxHints<double>(0.));
            ImGui::SameLine();
            std::ignore = g_state.byte->block.draw(configWithMinMaxHints<std::uint8_t>(0, -5., 1000.));
            std::ignore = g_state.fraction->block.draw(configWithMinMaxHints<double>(0., 0., 1.));
            ImGui::SameLine();
            std::ignore = g_state.coarseControl->block.draw(configWithMinMaxHints<float>(0.f, 0., 10.));
            ImGui::SameLine();
            std::ignore = g_state.rateControl->block.draw(configWithMinMaxHints<double>(0.));
            ImGui::SameLine();
            std::ignore = g_state.toggleMinAndMaxControl->block.draw(g_state.isMinMaxEnabledForToggleMinMaxControl ? configWithMinMaxHints<double>(0., 0., 10.) : configWithMinMaxHints<double>(0.));
            std::ignore = g_state.doubleConversionTestControl->block.draw(configWithMinMaxHints<std::uint64_t>(kUint64ValueThatExceedsDoublePrecision));
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            expect(g_state.waitUntil(ctx, "dashboard is initialized", [&] { return g_state.dashboard->isInitialised && g_state.dashboard->session.graphModel.topologyGeneration > 0UZ && blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "amplitude", 5.f); })) << fatal << "the flowgraph runs";
            ctx->Yield(10);

            "loading the dashboard writes nothing to the targets"_test = [&] {
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "amplitude", 5.f)) << "not the field's 1";
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "amplitude", 3.f)) << "not the field's 1";
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "frequency", 1.f)) << "not the spin box's 1";
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "frequency", 2.f)) << "not the slider's 2";
                expect(g_state.steps->takeSent().empty() && g_state.ratio->takeSent().empty()) << "the standalone controls sent nothing";
            };

            "each control is drawn in the toolbar"_test = [&] {
                for (const char* item : {"**/##Amplitude", "**/##Frequency", "**/##Level", "**/##Gain", "**/##Gain/+"}) {
                    expect(ctx->ItemExists(item)) << item;
                }
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + kHostSize));
            };

            "typing into the field sets the setting of both named blocks as float32"_test = [&] {
                ctx->ItemInputValue("**/##Amplitude", 2.5f);
                expect(g_state.waitUntil(ctx, "amplitude reaches inputted value", [] { return blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "amplitude", 2.5f) && blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "amplitude", 2.5f); }));
            };

            "the spin box's step button raises the target by the step"_test = [&] {
                ctx->ItemClick("**/##Frequency/+");
                expect(g_state.waitUntil(ctx, "frequency appears to have incremented", [] { return blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "frequency", 2.f); })) << "1 + step 1";
            };

            "the slider sets the frequency of its target"_test = [&] {
                ctx->ItemInputValue("**/##Level", 4.f);
                expect(g_state.waitUntil(ctx, "frequency reaches what was inputted by slider", [] { return blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "frequency", 4.f); }));
            };

            "the slider spinner's '*' entry is not dispatched yet"_test = [&] {
                ctx->ItemInputValue("**/##Gain", "80");
                ctx->Yield(10);
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "amplitude", 2.5f) && blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "amplitude", 2.5f)) << "'*:amplitude' sets nothing";
            };

            "without a step the spinner steps by the last significant digit, as the editors do"_test = [&] {
                const auto rateIs = [](double expected) { return std::abs(g_state.rateControl->block.value - expected) < 1e-9; };
                ctx->ItemInputValue("**/##Rate", "80");
                expect(g_state.waitUntil(ctx, "the float64 spin box has the expected value", [&] { return rateIs(80.); })) << fatal;
                ctx->ItemClick("**/##Rate/+");
                expect(g_state.waitUntil(ctx, "the spin box button increments the value by 1.0", [&] { return rateIs(81.); })) << "80.0000 -> step 1";
            };

            "the step amount of a spin box is equal to the last significant digit of the last typed value"_test = [&] {
                const auto rateIs = [](double expected) { return std::abs(g_state.rateControl->block.value - expected) < 1e-9; };
                for (const auto& [typed, afterPlus] : std::array{std::pair{"50.1", 50.2}, std::pair{"50", 51.}, std::pair{"50.01", 50.02}}) {
                    ctx->ItemInputValue("**/##Rate", typed);
                    ctx->ItemClick("**/##Rate/+");
                    expect(g_state.waitUntil(ctx, "the control's value increments to the expected value", [&, afterPlus] { return rateIs(afterPlus); })) << std::format("typed {}, then '+'", typed);
                }
                ctx->ItemClick("**/##Rate/+");
                ctx->ItemClick("**/##Rate/-");
                ctx->ItemClick("**/##Rate/-");
                expect(g_state.waitUntil(ctx, "the control's value to update after pressing +/-", [&] { return rateIs(50.01); })) << "the step buttons still use 0.01";
            };

            "dragging the slider moves the value in steps of the last typed value's significant digit"_test = [&] {
                ctx->ItemInputValue("**/##Coarse", "4");
                expect(g_state.waitUntil(ctx, "the 0..10 float32 slider has the expected value", [] { return g_state.coarseControl->block.value == 4.; })) << fatal;
                ctx->ItemDragWithDelta("**/##Coarse", ImVec2(37.f, 0.f));
                expect(g_state.waitUntil(ctx, "the drag moved the value", [] { return g_state.coarseControl->block.value != 4.; })) << fatal;
                const double dragged = g_state.coarseControl->block.value;
                expect(eq(dragged, std::round(dragged))) << std::format("dragged to {}: whole numbers", dragged);
            };

            "the toolbar fits its host: natural row, shrunk row, two rows, then without labels"_test = [&] {
                const ImGuiStyle&                  style      = ImGui::GetStyle();
                const DigitizerUi::DashboardStyle& toolbar    = DigitizerUi::LookAndFeel::instance().dashboardStyle;
                const auto                         labelWidth = [&](const char* label) { return ImGui::CalcTextSize(label).x + style.ItemInnerSpacing.x; };
                const std::array<const char*, 4UZ> labels{"Amplitude", "Frequency", "Level", "Gain"};
                const float                        spinner = 2.f * (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x);
                const float                        input   = toolbar.toolbarControlInputWidth;
                const float                        slider  = toolbar.toolbarControlSliderWidth;
                const float                        least   = toolbar.toolbarControlMinimumWidth;
                const std::array<float, 4UZ>       naturalWidget{input, input + spinner, slider, slider + spinner};
                const std::array<float, 4UZ>       minimumWidget{least, least + spinner, least, least + spinner};
                std::array<float, 4UZ>             minimum{};
                float                              naturalRow = 3.f * toolbar.toolbarItemSpacing;
                float                              minimumRow = 3.f * toolbar.toolbarItemSpacing;
                for (std::size_t i = 0UZ; i < labels.size(); ++i) {
                    minimum[i] = minimumWidget[i] + labelWidth(labels[i]);
                    naturalRow += naturalWidget[i] + labelWidth(labels[i]);
                    minimumRow += minimum[i];
                }
                const float frame    = toolbar.toolbarLeftPadding + 2.f * style.WindowPadding.x;
                const auto  layoutAt = [&](float rowWidth) {
                    g_state.hostWidth = rowWidth + frame;
                    ctx->Yield(4);
                };
                const auto visibleFrames = [&] {
                    std::vector<ImRect> frames;
                    for (const char* label : labels) {
                        if (const std::string ref = std::format("**/##{}", label); ctx->ItemExists(ref.c_str())) {
                            frames.push_back(ctx->ItemInfo(ref.c_str()).RectFull);
                        }
                    }
                    return frames;
                };
                const auto rowsOf = [](const std::vector<ImRect>& frames) {
                    std::vector<int> tops;
                    std::ranges::transform(frames, std::back_inserter(tops), [](const ImRect& rect) { return static_cast<int>(std::round(rect.Min.y)); });
                    std::ranges::sort(tops);
                    return static_cast<std::size_t>(std::ranges::distance(tops.begin(), std::ranges::unique(tops).begin()));
                };
                const auto insideHost = [&](const std::vector<ImRect>& frames) {
                    const ImRect host(kHostPos, kHostPos + ImVec2(g_state.hostWidth, kHostSize.y));
                    return std::ranges::all_of(frames, [&](const ImRect& rect) { return host.Contains(rect); });
                };
                const auto  sliderWidth    = [&] { return ctx->ItemInfo("**/##Level").RectFull.GetWidth(); };
                const float rowStart       = kHostPos.x + style.WindowPadding.x + toolbar.toolbarLeftPadding;
                const auto  firstFrameLeft = [&] { return ctx->ItemInfo("**/##Amplitude").RectFull.Min.x; };

                layoutAt(naturalRow + 40.f);
                auto frames = visibleFrames();
                expect(eq(frames.size(), 4UZ) && eq(rowsOf(frames), 1UZ) && insideHost(frames)) << "one row";
                expect(std::abs(sliderWidth() - slider) < 1.f) << std::format("the slider at its natural {} px: {}", slider, sliderWidth());
                expect(std::abs(firstFrameLeft() - rowStart - labelWidth("Amplitude")) < 1.f) << "the label comes first, then its field";

                layoutAt(0.5f * (naturalRow + minimumRow));
                frames = visibleFrames();
                expect(eq(frames.size(), 4UZ) && eq(rowsOf(frames), 1UZ) && insideHost(frames)) << "still one row";
                expect(sliderWidth() > least && sliderWidth() < slider) << std::format("the slider shrunk to {} px", sliderWidth());
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + ImVec2(g_state.hostWidth, kHostSize.y)));

                layoutAt(std::max(minimum[0] + minimum[1], minimum[2] + minimum[3]) + toolbar.toolbarItemSpacing + 1.f);
                frames = visibleFrames();
                expect(eq(frames.size(), 4UZ) && eq(rowsOf(frames), 2UZ) && insideHost(frames)) << "two rows, labels kept";
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + ImVec2(g_state.hostWidth, kHostSize.y)));

                layoutAt(*std::ranges::min_element(minimum) - 1.f);
                expect(std::abs(firstFrameLeft() - rowStart) < 1.f) << "labels dropped: the first field starts the row";
                expect(le(rowsOf(visibleFrames()), 2UZ)) << "at most two rows";
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + ImVec2(g_state.hostWidth, kHostSize.y)));

                layoutAt(kHostSize.x - frame);
            };

            "a settings message changes the targets of a running control"_test = [&] {
                gr::Message message;
                message.cmd         = gr::message::Command::Set;
                message.serviceName = "control_a_field";
                message.endpoint    = gr::block::property::kSetting;
                message.data        = gr::property_map{{"target_map", std::string("SignalGenerator2:amplitude")}};
                g_state.dashboard->session.sendMessage(std::move(message));
                expect(g_state.waitUntil(ctx, "control's target_map field to update according to message", [] { return setting("control_a_field", "target_map").value_or(std::string{}) == "SignalGenerator2:amplitude"; })) << fatal << "the control took the new target_map";
                ctx->ItemInputValue("**/##Amplitude", 6.f);
                expect(g_state.waitUntil(ctx, "signal generator's input value to update after it was just selected with a new target_map", [] { return blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "amplitude", 6.f); })) << "the new target is set";
                ctx->Yield(10);
                expect(blockHasBasicTypeKeyValuePair<float>("SignalGenerator1", "amplitude", 2.5f)) << "the old target is not";
            };

            "the value sent by a control has the type of its first controlled property"_test = [&] {
                ctx->ItemInputValue("**/##Steps", 7);
                const auto steps = g_state.steps->takeSent();
                expect(sentExactly<std::int16_t>(steps, "TargetA", "received", 7) && sentExactly<std::int16_t>(steps, "TargetB", "received", 7)) << "an int16 7 to TargetA and TargetB";
                ctx->ItemInputValue("**/##Ratio", "80.5");
                expect(sentExactly<double>(g_state.ratio->takeSent(), "TargetA", "ratio", 80.5)) << "a float64 80.5";
            };

            "dragging a slider sends at most one message per frame"_test = [&] {
                std::ignore          = g_state.steps->takeSent();
                const int firstFrame = ImGui::GetFrameCount();
                ctx->ItemDragWithDelta("**/##Steps", ImVec2(60.f, 0.f));
                const auto sent          = g_state.steps->takeSent();
                const int  framesDragged = ImGui::GetFrameCount() - firstFrame;
                expect(!sent.empty()) << "the drag changed the value";
                expect(le(sent.size(), 2UZ * static_cast<std::size_t>(framesDragged))) << std::format("{} messages (two targets each) over {} frames", sent.size(), framesDragged);
                const auto finalValue = static_cast<std::int16_t>(g_state.steps->block.value);
                expect(!sent.empty() && sentExactly<std::int16_t>(std::vector<gr::Message>(sent.end() - 1, sent.end()), "TargetB", "received", finalValue)) << std::format("the last message carries the final value {}", finalValue);
            };

            "a range beyond the number type is clamped to the type's limits"_test = [&] {
                std::ignore = g_state.byte->takeSent();
                ctx->ItemDragWithDelta("**/##Byte", ImVec2(600.f, 0.f));
                const auto sent = g_state.byte->takeSent();
                expect(!sent.empty() && sentExactly<std::uint8_t>(std::vector<gr::Message>(sent.end() - 1, sent.end()), "TargetA", "byte", std::numeric_limits<std::uint8_t>::max())) << "dragged to the right end: uint8's 255";
            };

            "number input is clamped to the target property's min and max"_test = [&] {
                std::ignore = g_state.steps->takeSent();
                ctx->ItemInputValue("**/##Steps", 500);
                const auto above = g_state.steps->takeSent();
                expect(!above.empty() && sentExactly<std::int16_t>(std::vector<gr::Message>{above.back()}, "TargetB", "received", 10)) << "clamped to max";
                ctx->ItemInputValue("**/##Steps", -500);
                const auto below = g_state.steps->takeSent();
                expect(!below.empty() && sentExactly<std::int16_t>(std::vector<gr::Message>{below.back()}, "TargetB", "received", -10)) << "clamped to min";
            };

            "the min and max is decided as an intersection of the property's min and max, and the min and max settings of the control block"_test = [&] {
                // standalone_limited has min -1 and max 1
                // the target property should have 0 to 10
                const auto lastSent = [&](const char* typed) {
                    std::ignore = g_state.toggleMinAndMaxControl->takeSent();
                    ctx->ItemInputValue("**/##Limited", typed);
                    const auto sent = g_state.toggleMinAndMaxControl->takeSent();
                    expect(!sent.empty()) << fatal << std::format("typing {} sends a value", typed);
                    return std::vector<gr::Message>(sent.end() - 1, sent.end());
                };
                expect(!g_state.isMinMaxEnabledForToggleMinMaxControl) << fatal;
                expect(sentExactly<double>(lastSent("5"), "TargetA", "limited", 1.)) << "clamping seems to respect the control block's max";
                expect(sentExactly<double>(lastSent("-5"), "TargetA", "limited", -1.)) << "clamping seems to respect the control block's min";

                // now try with the property's min and max values being sent
                g_state.isMinMaxEnabledForToggleMinMaxControl = true;
                expect(sentExactly<double>(lastSent("0.5"), "TargetA", "limited", 0.5));
                expect(sentExactly<double>(lastSent("50"), "TargetA", "limited", 1.)) << "when the property's min and max are sent, the block's max is still respected";
                expect(sentExactly<double>(lastSent("-5"), "TargetA", "limited", 0.)) << "when both property and block min/max settings are in use, the property's min value is respected";
                g_state.isMinMaxEnabledForToggleMinMaxControl = false;
            };

            "a number control has no limits by default"_test = [&] {
                expect(eq(g_state.ratio->block.min, std::numeric_limits<double>::lowest()) && eq(g_state.ratio->block.max, std::numeric_limits<double>::max())) << "float64 lowest and max";
                std::ignore = g_state.ratio->takeSent();
                ctx->ItemInputValue("**/##Ratio", "-1000000000");
                expect(sentExactly<double>(g_state.ratio->takeSent(), "TargetA", "ratio", -1e9)) << "not clamped";
            };

            "if a number control is set to a number that exceeds double precision (the internal representation in ImControlNumber) it static casts to a double"_test = [&] {
                expect(eq(g_state.doubleConversionTestControl->block.value, static_cast<double>(kValueBeforeExceedingDoublePrecision))) << "value doesn't exceed double precision at start of test";
                std::ignore = g_state.doubleConversionTestControl->takeSent();
                ctx->ItemClick("**/##DoubleConversion/+");
                const auto sent = g_state.doubleConversionTestControl->takeSent();
                expect(!sent.empty() && sentExactly<std::uint64_t>(std::vector<gr::Message>{sent.back()}, "TargetA", "test_property", kValueBeforeExceedingDoublePrecision)) << "control displays the value but also sends the value that is within double precision";
            };

            "a slider starting at 0 steps by a tenth of its range at most"_test = [&] {
                ctx->ItemDragWithDelta("**/##Fraction", ImVec2(40.f, 0.f));
                const double dragged = g_state.fraction->block.value;
                expect(dragged > 0. && dragged < 1.) << std::format("0..1 dragged a quarter to {}, not to an end", dragged);
                expect(std::abs(dragged * 10. - std::round(dragged * 10.)) < 1e-9) << std::format("{} is on the 0.1 grid", dragged);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "imcontrolnumber";
    TestApp app(options);

    app.initImGui();

    auto& registry = gr::globalBlockRegistry();
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    using Standalone                    = StandaloneControl<DigitizerUi::ImControlNumber>;
    g_state.steps                       = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_steps"}, {"label", "Steps"}, {"style", "slider"}, {"target_map", "TargetA,TargetB:received"}});
    g_state.ratio                       = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_ratio"}, {"label", "Ratio"}, {"style", "field"}, {"target_map", "TargetA:ratio"}});
    g_state.fraction                    = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_fraction"}, {"label", "Fraction"}, {"style", "slider"}, {"value", 0.}, {"target_map", "TargetA:fraction"}});
    g_state.byte                        = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_byte"}, {"label", "Byte"}, {"style", "slider"}, {"value", -5.}, {"target_map", "TargetA:byte"}});
    g_state.coarseControl               = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_coarse"}, {"label", "Coarse"}, {"style", "slider"}, {"value", 2.}, {"target_map", "TargetA:coarse"}});
    g_state.rateControl                 = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_rate"}, {"label", "Rate"}, {"style", "spinBox"}, {"value", 50.}, {"target_map", "TargetA:rate"}});
    g_state.toggleMinAndMaxControl      = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_limited"}, {"label", "Limited"}, {"style", "field"}, {"min", -1.}, {"max", 1.}, {"target_map", "TargetA:limited"}});
    g_state.doubleConversionTestControl = std::make_unique<Standalone>(gr::property_map{{"name", "standalone_double_conversion"}, {"label", "DoubleConversion"}, {"style", "spinBox"}, {"step", 1.}, {"target_map", "TargetA:test_property"}});

    auto grcFile      = cmrc::ui_test_assets::get_filesystem().open("examples/qa_controls.grc");
    g_state.dashboard = DigitizerUi::Dashboard::create(nullptr, DigitizerUi::DashboardDescription::createEmpty("controls"));
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [](gr::Graph&& graph) { g_state.dashboard->session.emplaceGraph(std::move(graph)); });
    const bool result = app.runTests();
    g_state.unloadDashboard();
    return result ? 0 : 1;
}
