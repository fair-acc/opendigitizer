#include "ImGuiTestApp.hpp"

#include "../common/LookAndFeel.hpp"
#include <boost/ut.hpp>

using namespace boost;

namespace {
struct FontSizes {
    float legacyTiny        = 0.f; // pre-1.92 behaviour: face at its load-time size
    float legacySmall       = 0.f;
    float legacyNormal      = 0.f;
    float hostNormalTiny    = 0.f; // host pushes the size of fontNormal
    float hostNormalSmall   = 0.f;
    float hostDoubleTiny    = 0.f; // host pushes twice that size
    float hostDoubleSmall   = 0.f;
    float hostNormalNoFonts = 0.f; // faces not loaded: current size kept
};
} // namespace

class TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_font_size", "follows host font size");
        t->SetVarsDataType<FontSizes>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);
            auto&       sizes      = ctx->GetVars<FontSizes>();
            const auto& lnf        = DigitizerUi::LookAndFeel::instance();
            const auto  idx        = lnf.prototypeMode;
            const float normalSize = lnf.fontNormal[idx]->LegacySize;

            auto sizeOf = [&](const std::array<ImFont*, 2>& faces, float hostSize) {
                IMW::FontWithSize host(nullptr, hostSize);
                IMW::FontWithSize face(faces[idx], lnf.relativeFontSize(faces));
                return ImGui::GetFontSize();
            };
            auto legacySizeOf = [&](const std::array<ImFont*, 2>& faces) {
                IMW::FontWithSize face(faces[idx], faces[idx]->LegacySize);
                return ImGui::GetFontSize();
            };

            sizes.legacyTiny      = legacySizeOf(lnf.fontTiny);
            sizes.legacySmall     = legacySizeOf(lnf.fontSmall);
            sizes.legacyNormal    = legacySizeOf(lnf.fontNormal);
            sizes.hostNormalTiny  = sizeOf(lnf.fontTiny, normalSize);
            sizes.hostNormalSmall = sizeOf(lnf.fontSmall, normalSize);
            sizes.hostDoubleTiny  = sizeOf(lnf.fontTiny, 2.f * normalSize);
            sizes.hostDoubleSmall = sizeOf(lnf.fontSmall, 2.f * normalSize);

            const std::array<ImFont*, 2> noFaces{nullptr, nullptr};
            IMW::FontWithSize            host(nullptr, normalSize);
            IMW::FontWithSize            face(nullptr, DigitizerUi::LookAndFeel::instance().relativeFontSize(noFaces));
            sizes.hostNormalNoFonts = ImGui::GetFontSize();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("chart faces follow the host font size") = [ctx] {
                ctx->Yield(2);
                const auto&     sizes = ctx->GetVars<FontSizes>();
                constexpr float kTol  = 1e-3f;

                ut::expect(sizes.legacyTiny > 0.f && sizes.legacySmall > sizes.legacyTiny);
                ut::expect(ut::approx(sizes.hostNormalTiny, sizes.legacyTiny, kTol)) << "standalone tiny size unchanged";
                ut::expect(ut::approx(sizes.hostNormalSmall, sizes.legacySmall, kTol)) << "standalone small size unchanged";
                ut::expect(ut::approx(sizes.hostDoubleTiny, 2.f * sizes.legacyTiny, kTol)) << "tiny scales with the host size";
                ut::expect(ut::approx(sizes.hostDoubleSmall, 2.f * sizes.legacySmall, kTol)) << "small scales with the host size";
                ut::expect(ut::approx(sizes.hostNormalNoFonts, sizes.legacyNormal, kTol)) << "unloaded faces keep the current size";
            };
        };
    }
};

int main(int, char**) {
    TestApp app({.useInteractiveMode = false, .screenshotPrefix = "chart_font_size"});
    return app.runTests() ? 0 : 1;
}
