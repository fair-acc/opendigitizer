#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <common/LookAndFeel.hpp>
#include <components/StyledWidgets.hpp>

#include <array>
#include <cmath>
#include <format>
#include <utility>

using namespace gr::od;

using namespace boost;
using namespace boost::ut;

namespace {
constexpr ImVec2 kWindowPos{100.f, 60.f};
constexpr ImVec2 kWindowSize{460.f, 160.f};
constexpr float  kSliderWidth = 400.f;
constexpr float  kSliderMin   = 0.f;
constexpr float  kSliderMax   = 10.f;

constexpr std::array<const char*, 2UZ> kComboChoices{"160 Hz", "440 Hz"};

struct TestState {
    float       sliderValue = 1.f;
    std::size_t comboChoice = 0UZ;
};

TestState* gState = nullptr;

/// not exact: the slider has some padding
[[nodiscard]] float whereToClickToGetValue(const ImRect& sliderRegion, float value) { //
    return sliderRegion.Min.x + (sliderRegion.GetWidth() * (value - kSliderMin) / (kSliderMax - kSliderMin));
}

void captureWindow(ImGuiTestContext* ctx) { test::ImGuiTestApp::captureScreenshot(*ctx, ImRect(kWindowPos, kWindowPos + kWindowSize)); }
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "styledwidgets", "slider");

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kWindowPos);
            ImGui::SetNextWindowSize(kWindowSize);
            imw::Window window("Widgets", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetNextItemWidth(kSliderWidth);
            std::ignore = sliderScalar("##slider", {.dataType = ImGuiDataType_Float, .data = &gState->sliderValue, .min = &kSliderMin, .max = &kSliderMax, .format = "%.2f"});
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ctx->SetRef("//Widgets");
            const ImRect sliderRegion = ctx->ItemInfo("##slider").RectFull;
            const auto   dragToX      = [&](float screenX) {
                ctx->MouseMove("##slider");
                ctx->MouseDown();
                ctx->MouseMoveToPos(ImVec2(screenX, sliderRegion.GetCenter().y));
                ctx->MouseUp();
            };

            "slider is clamped to minimum value when dragged to the left"_test = [&] {
                dragToX(sliderRegion.Min.x - sliderRegion.GetWidth());
                expect(eq(gState->sliderValue, kSliderMin));
                captureWindow(ctx);
            };

            "slider is clamped to the maximum value when dragged to the right"_test = [&] {
                dragToX(sliderRegion.Max.x + sliderRegion.GetWidth());
                expect(eq(gState->sliderValue, kSliderMax));
                captureWindow(ctx);
            };

            "click slider to instantly move to the position under cursor"_test = [&] {
                const float clickedValue = 3.f;
                ctx->MouseMoveToPos(ImVec2(whereToClickToGetValue(sliderRegion, clickedValue), sliderRegion.GetCenter().y));
                ctx->MouseClick();
                // the drag handle is about FrameHeight in size, which sets its padding and its min and max positions: the error may equal the change of moving by FrameHeight
                const float tolerance = (kSliderMax - kSliderMin) * ImGui::GetFrameHeight() / sliderRegion.GetWidth();
                expect(std::abs(gState->sliderValue - clickedValue) < tolerance) << gState->sliderValue;
                captureWindow(ctx);
            };

            "typing into the text field"_test = [&] {
                const float typedValue = 2.5f;
                ctx->ItemInputValue("##slider/##value", "2.5");
                expect(eq(gState->sliderValue, typedValue));
            };

            "typing a value into the text field that is out of range"_test = [&] {
                ctx->ItemInputValue("##slider/##value", "1000");
                expect(eq(gState->sliderValue, kSliderMax));
            };

            const auto isTextInputActive        = [&] { return ImGui::TempInputIsActive(ctx->GetID("##slider/##value")); };
            const auto waitUntilTextInputActive = [&] {
                while (!isTextInputActive()) {
                    ctx->Yield();
                }
            };

            "right-clicking the slider turns the number label into a text input"_test = [&] {
                expect(!isTextInputActive());
                ctx->ItemClick("##slider", ImGuiMouseButton_Right);
                waitUntilTextInputActive();
                ctx->KeyCharsReplaceEnter("4");
                expect(eq(gState->sliderValue, 4.f));
                expect(!isTextInputActive());
            };

            "right-clicking the number label turns it into a text input"_test = [&] {
                ctx->ItemClick("##slider/##value", ImGuiMouseButton_Right);
                waitUntilTextInputActive();
                ctx->KeyCharsReplaceEnter("6");
                expect(eq(gState->sliderValue, 6.f));
                expect(!isTextInputActive());
            };
        };

        ImGuiTest* comboTest = IM_REGISTER_TEST(engine(), "styledwidgets", "combo");

        comboTest->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kWindowPos);
            ImGui::SetNextWindowSize(kWindowSize);
            imw::Window window("Widgets", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetNextItemWidth(comboWidthForText(ImGui::CalcTextSize(kComboChoices[1]).x));
            if (auto combo = ComboBox("##combo", kComboChoices[gState->comboChoice], 0)) {
                for (std::size_t i = 0UZ; i < kComboChoices.size(); ++i) {
                    if (selectable(kComboChoices[i], i == gState->comboChoice)) {
                        gState->comboChoice = i;
                    }
                }
            }
        };

        comboTest->TestFunc = [](ImGuiTestContext* ctx) {
            ctx->SetRef("//Widgets");

            "combo is wide enough for its value and its chevron"_test = [&] {
                const float valueWidth = ImGui::CalcTextSize(kComboChoices[1]).x;
                expect(gt(ctx->ItemInfo("##combo").RectFull.GetWidth(), valueWidth + ImGui::GetFontSize() * LookAndFeel::instance().comboChevronWidthEms));
                captureWindow(ctx);
            };

            "clicking anywhere in the combo opens its list, and picking an entry selects it"_test = [&] {
                const ImRect frame = ctx->ItemInfo("##combo").RectFull;
                ctx->MouseMoveToPos(ImVec2(frame.Min.x + ImGui::GetStyle().FramePadding.x, frame.GetCenter().y));
                ctx->MouseClick();
                captureWindow(ctx);
                ctx->ItemClick(std::format("//$FOCUSED/{}", kComboChoices[1]).c_str());
                expect(eq(gState->comboChoice, 1UZ));
                captureWindow(ctx);
            };

            "combo width stops growing at the cap, beyond which the value is drawn with an ellipsis"_test = [&] {
                const float cap = ImGui::GetFontSize() * LookAndFeel::instance().comboTextMaxWidthEms;
                expect(lt(comboWidthForText(cap / 2.f), comboWidthForText(cap)));
                expect(eq(comboWidthForText(cap), comboWidthForText(2.f * cap)));
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "styledwidgets";

    TestApp   app(options);
    TestState state;
    gState = std::addressof(state);
    return app.runTests() ? 0 : 1;
}
