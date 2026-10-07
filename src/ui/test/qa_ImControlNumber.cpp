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

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <vector>

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace opendigitizer::test;

namespace {
constexpr ImVec2 kHostPos{40.f, 80.f};
constexpr ImVec2 kHostSize{900.f, 120.f};
constexpr ImVec2 kStandalonePos{40.f, 240.f};
constexpr int    kMaxFrames = 600;

struct TestState : opendigitizer::test::TestDashboardRunner {
    DigitizerUi::ToolbarView                                         view;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> steps;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> ratio;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> byte;
    std::unique_ptr<StandaloneControl<DigitizerUi::ImControlNumber>> fraction;
    float                                                            hostWidth = kHostSize.x;
};

TestState g_state;

TESTING_BLOCK_INSPECTION_UTILS_MAKE_ALL_GLOBAL_STATE_AWARE_OVERLOADS(g_state.dashboard->session)

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

        t->GuiFunc = [](ImGuiTestContext*) {
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
            std::ignore = g_state.steps->block.draw();
            ImGui::SameLine();
            std::ignore = g_state.ratio->block.draw();
            ImGui::SameLine();
            std::ignore = g_state.byte->block.draw();
            std::ignore = g_state.fraction->block.draw();
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

            "each style is drawn in the toolbar"_test = [&] {
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
                g_state.waitUntil(ctx, "slider syncs to 80 due to receiving a message from one of its targets on the previous test", [] { return blockHasBasicTypeKeyValuePair<double>("control_d_sliderSpinner", "value", 80.0); });
                ctx->ItemClick("**/##Gain/+");
                expect(g_state.waitUntil(ctx, "slider spinbox buttons increment the value by 1.0", [] { return blockHasBasicTypeKeyValuePair<double>("control_d_sliderSpinner", "value", 81.0); })) << "80.0000 -> step 1";
            };

            "the step follows the last significant digit of the last typed value"_test = [&] {
                for (const auto& [typed, afterPlus] : std::array{std::pair{"50.1", 50.2}, std::pair{"50", 51.}, std::pair{"50.01", 50.02}}) {
                    ctx->ItemInputValue("**/##Gain", typed);
                    ctx->ItemClick("**/##Gain/+");
                    expect(g_state.waitUntil(ctx, "slider control's internal value increments to expected value", [afterPlus] { return blockApproximatelyHasDoubleKeyValuePair("control_d_sliderSpinner", "value", afterPlus); })) << std::format("typed {}, then '+'", typed);
                }
                ctx->ItemClick("**/##Gain/+");
                ctx->ItemClick("**/##Gain/-");
                ctx->ItemClick("**/##Gain/-");
                expect(g_state.waitUntil(ctx, "slider control's internal values to update after pressing gain +/-", [] { return blockApproximatelyHasDoubleKeyValuePair("control_d_sliderSpinner", "value", 50.01); })) << "the step buttons keep the step of 0.01";
            };

            "dragging the slider moves the value in steps of the last typed value's significant digit"_test = [&] {
                ctx->ItemInputValue("**/##Level", "4");
                expect(g_state.waitUntil(ctx, "signal generator's frequency to be set", [] { return blockHasBasicTypeKeyValuePair<float>("SignalGenerator2", "frequency", 4.f); })) << fatal;
                ctx->ItemDragWithDelta("**/##Level", ImVec2(37.f, 0.f));
                float dragged = 4.f;
                expect(g_state.waitUntil(ctx, "signal generator's value to change from the initial, after the mouse dragged the controlling slider a bit",
                    [&dragged] {
                        dragged = setting("SignalGenerator2", "frequency").value_or<float>(4.f);
                        return dragged != 4.f;
                    }))
                    << fatal << "the drag moved the frequency";
                expect(eq(dragged, std::round(dragged))) << std::format("dragged to {}: whole Hz", dragged);
            };

            "the toolbar fits its host: natural row, shrunk row, two rows, then without labels"_test = [&] {
                // qa_controls.grc: field, spin box 110 px, sliders 160; at least 60 per widget, plus step buttons and labels
                const ImGuiStyle&                  style      = ImGui::GetStyle();
                const auto                         labelWidth = [&](const char* label) { return ImGui::CalcTextSize(label).x + style.ItemInnerSpacing.x; };
                const std::array<const char*, 4UZ> labels{"Amplitude", "Frequency", "Level", "Gain"};
                const float                        spinner = 2.f * (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x);
                const std::array<float, 4UZ>       naturalWidget{110.f, 110.f + spinner, 160.f, 160.f + spinner};
                const std::array<float, 4UZ>       minimumWidget{60.f, 60.f + spinner, 60.f, 60.f + spinner};
                std::array<float, 4UZ>             minimum{};
                float                              naturalRow = 3.f * style.ItemSpacing.x;
                float                              minimumRow = 3.f * style.ItemSpacing.x;
                for (std::size_t i = 0UZ; i < labels.size(); ++i) {
                    minimum[i] = minimumWidget[i] + labelWidth(labels[i]);
                    naturalRow += naturalWidget[i] + labelWidth(labels[i]);
                    minimumRow += minimum[i];
                }
                const float frame    = 16.f + 2.f * style.WindowPadding.x;
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
                const float rowStart       = kHostPos.x + style.WindowPadding.x + 16.f;
                const auto  firstFrameLeft = [&] { return ctx->ItemInfo("**/##Amplitude").RectFull.Min.x; };

                layoutAt(naturalRow + 40.f);
                auto frames = visibleFrames();
                expect(eq(frames.size(), 4UZ) && eq(rowsOf(frames), 1UZ) && insideHost(frames)) << "one row";
                expect(std::abs(sliderWidth() - 160.f) < 1.f) << std::format("the slider at its natural 160 px: {}", sliderWidth());
                expect(std::abs(firstFrameLeft() - rowStart - labelWidth("Amplitude")) < 1.f) << "the label comes first, then its field";

                layoutAt(0.5f * (naturalRow + minimumRow));
                frames = visibleFrames();
                expect(eq(frames.size(), 4UZ) && eq(rowsOf(frames), 1UZ) && insideHost(frames)) << "still one row";
                expect(sliderWidth() > 60.f && sliderWidth() < 160.f) << std::format("the slider shrunk to {} px", sliderWidth());
                captureScreenshot(*ctx, ImRect(kHostPos, kHostPos + ImVec2(g_state.hostWidth, kHostSize.y)));

                layoutAt(std::max(minimum[0] + minimum[1], minimum[2] + minimum[3]) + style.ItemSpacing.x + 1.f);
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

            "a control sends a value of exactly its value_type, to each named block"_test = [&] {
                expect(g_state.steps->block.value_type == "int16" && g_state.ratio->block.value_type == "float64") << fatal << "the standalone controls took their settings";
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

            "a range beyond the value_type is clamped to the type's limits"_test = [&] {
                std::ignore = g_state.byte->takeSent();
                ctx->ItemDragWithDelta("**/##Byte", ImVec2(600.f, 0.f));
                const auto sent = g_state.byte->takeSent();
                expect(!sent.empty() && sentExactly<std::uint8_t>(std::vector<gr::Message>(sent.end() - 1, sent.end()), "TargetA", "byte", std::numeric_limits<std::uint8_t>::max())) << "dragged to the right end: uint8's 255";
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
    g_state.steps    = std::make_unique<StandaloneControl<DigitizerUi::ImControlNumber>>(gr::property_map{{"name", "standalone_steps"}, {"label", "Steps"}, {"value_type", "int16"}, {"style", "slider"}, {"min", -10.}, {"max", 10.}, {"target_map", "TargetA,TargetB:received"}});
    g_state.ratio    = std::make_unique<StandaloneControl<DigitizerUi::ImControlNumber>>(gr::property_map{{"name", "standalone_ratio"}, {"label", "Ratio"}, {"value_type", "float64"}, {"style", "field"}, {"target_map", "TargetA:ratio"}});
    g_state.fraction = std::make_unique<StandaloneControl<DigitizerUi::ImControlNumber>>(gr::property_map{{"name", "standalone_fraction"}, {"label", "Fraction"}, {"value_type", "float64"}, {"style", "slider"}, {"value", 0.}, {"min", 0.}, {"max", 1.}, {"target_map", "TargetA:fraction"}});
    g_state.byte     = std::make_unique<StandaloneControl<DigitizerUi::ImControlNumber>>(gr::property_map{{"name", "standalone_byte"}, {"label", "Byte"}, {"value_type", "uint8"}, {"style", "slider"}, {"value", -5.}, {"min", -5.}, {"max", 1000.}, {"target_map", "TargetA:byte"}});

    auto grcFile      = cmrc::ui_test_assets::get_filesystem().open("examples/qa_controls.grc");
    g_state.dashboard = DigitizerUi::Dashboard::create(nullptr, DigitizerUi::DashboardDescription::createEmpty("controls"));
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [](gr::Graph&& graph) { g_state.dashboard->session.emplaceGraph(std::move(graph)); });
    const bool result = app.runTests();
    g_state.unloadDashboard();
    return result ? 0 : 1;
}
