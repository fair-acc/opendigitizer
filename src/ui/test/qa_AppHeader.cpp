#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <common/LookAndFeel.hpp>
#include <components/AppHeader.hpp>

#include <gnuradio-4.0/meta/utils.hpp>

#include <array>
#include <format>
#include <limits>
#include <string>
#include <vector>

using namespace gr::od;

using namespace boost;
using namespace boost::ut;

namespace {
using components::AppHeader;

constexpr std::array kPhoneScreenSizes{
    ImVec2{320.f, 568.f},
    ImVec2{360.f, 640.f},
    ImVec2{375.f, 667.f},
    ImVec2{390.f, 844.f},
    ImVec2{412.f, 915.f},
    ImVec2{430.f, 932.f},
    ImVec2{568.f, 320.f},
    ImVec2{844.f, 390.f},
};
constexpr ImVec2 kDesktopScreenSize{1600.f, 1000.f};
constexpr float  kWidthSweepStep      = 10.f;
constexpr float  kNarrowestSweptWidth = 300.f;

struct TestState {
    AppHeader   header;
    std::string title = "Pulsed power converter acquisition overview";
};

TestState* gState = nullptr;

constexpr const char* kMainMenuWindow      = "MenuPopup_1";
constexpr const char* kDeveloperMenuWindow = "MenuPopup_2";

ImRect screenRect() { return ImRect(ImVec2{}, ImGui::GetMainViewport()->Size); }

struct NamedRect {
    std::string name;
    ImRect      rect;
};

/// @returns the screen rects of all header items that are drawn with a nonzero area
std::vector<NamedRect> findAllDrawnHeaderItemRects(ImGuiTestContext* ctx) {
    ImGuiTestItemList gatheredItems;
    ctx->GatherItems(&gatheredItems, std::format("//Test Window/{}", AppHeader::kIdScope).c_str(), 1);
    std::vector<NamedRect> drawnHeaderItemRects;
    for (const ImGuiTestItemInfo& item : gatheredItems) {
        if (item.RectFull.GetArea() > 0.f) {
            drawnHeaderItemRects.push_back({item.DebugLabel, item.RectFull});
        }
    }
    return drawnHeaderItemRects;
}

/// @returns empty rect when the item is not drawn
ImRect drawnItemRect(ImGuiTestContext* ctx, std::string_view label) {
    for (const NamedRect& item : findAllDrawnHeaderItemRects(ctx)) {
        if (item.name == label) {
            return item.rect;
        }
    }
    return {};
}

std::string formatImRect(const ImRect& rect) { return std::format("({}, {})-({}, {})", rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y); }

void expectRectsAreVisibleAndNonOverlapping(const std::vector<NamedRect>& items, std::string_view context) {
    const ImRect screen = screenRect();
    for (std::size_t i = 0UZ; i < items.size(); ++i) {
        expect(screen.Contains(items[i].rect)) << std::format("{}: the {} at {} lies inside the screen {}", context, items[i].name, formatImRect(items[i].rect), formatImRect(screen));
        for (std::size_t j = i + 1UZ; j < items.size(); ++j) {
            expect(!items[i].rect.Overlaps(items[j].rect)) << std::format("{}: the {} at {} does not overlap the {} at {}", context, items[i].name, formatImRect(items[i].rect), items[j].name, formatImRect(items[j].rect));
        }
    }
}

void showScreen(ImGuiTestContext* ctx, ImVec2 screenSize) {
    test::resizeViewport(ctx, screenSize);
    ctx->Yield();
}

bool isWindowActive(const char* windowName) {
    const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    return window != nullptr && window->Active;
}

std::vector<NamedRect> popupButtonRects(ImGuiTestContext* ctx, const char* popupWindowName) {
    std::vector<NamedRect> rects;
    if (!isWindowActive(popupWindowName)) {
        return rects;
    }
    ImGuiTestItemList items;
    ctx->GatherItems(&items, std::format("//{}", popupWindowName).c_str(), 1);
    for (const ImGuiTestItemInfo& item : items) {
        rects.push_back({popupWindowName, item.RectFull});
    }
    return rects;
}
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "app_header", "layout on narrow screens");

        t->GuiFunc = [](ImGuiTestContext*) {
            auto& state = *gState;
            ImGui::SetNextWindowPos(ImVec2{});
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
            imw::Window window("Test Window", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            state.header.draw(state.title, LookAndFeel::instance().fontLarge(), LookAndFeel::instance().style);
            ImGui::Dummy(ImVec2{}); // the header leaves the cursor beneath itself for the next item
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            auto& state = *gState;
            ctx->SetRef("//Test Window");
            // the menu buttons slide out of sight while the mouse is away from them, which is not tested here
            const float autoHideDelaySeconds                    = LookAndFeel::instance().autoHideDelaySeconds;
            LookAndFeel::mutableInstance().autoHideDelaySeconds = std::numeric_limits<float>::infinity();

            "on a variety of phone screen sizes the header items do not seem to overlap or go off the screen"_test = [&] {
                for (const ImVec2 screenSize : kPhoneScreenSizes) {
                    showScreen(ctx, screenSize);
                    expectRectsAreVisibleAndNonOverlapping(findAllDrawnHeaderItemRects(ctx), std::format("{}x{}", screenSize.x, screenSize.y));
                }
            };

            "the title is complete on a desktop screen, and the header items do not overlap at any screen width"_test = [&] {
                showScreen(ctx, kDesktopScreenSize);
                float fullTitleWidth = 0.f;
                {
                    imw::Font titleFont(LookAndFeel::instance().fontLarge());
                    fullTitleWidth = ImGui::CalcTextSize(state.title.c_str()).x;
                }
                expect(eq(drawnItemRect(ctx, AppHeader::kTitleLabel).GetWidth(), fullTitleWidth)) << "the complete title on a desktop screen";

                for (float width = kDesktopScreenSize.x; width >= kNarrowestSweptWidth; width -= kWidthSweepStep) {
                    showScreen(ctx, ImVec2{width, kDesktopScreenSize.y});
                    expectRectsAreVisibleAndNonOverlapping(findAllDrawnHeaderItemRects(ctx), std::format("width {}", width));
                }
            };

            "the radial menu and hamburger menu icons shouldn't overlap on small screens"_test = [&] {
                for (const ImVec2 screenSize : kPhoneScreenSizes) {
                    showScreen(ctx, screenSize);
                    const std::string context = std::format("{}x{}", screenSize.x, screenSize.y);

                    {
                        // force windows to stay open after mouse away so we can easily have them both open for the test
                        gr::on_scope_exit restore = [mouseoutCloseDelaySeconds = std::exchange(LookAndFeel::mutableInstance().popupMenuMouseoutCloseDelaySeconds, std::numeric_limits<float>::infinity())] {
                            LookAndFeel::mutableInstance().popupMenuMouseoutCloseDelaySeconds = mouseoutCloseDelaySeconds; //
                        };

                        ctx->MouseMoveToPos(drawnItemRect(ctx, AppHeader::kMenuButtonLabel).GetCenter());
                        test::waitUntil(ctx, "the main menu is open", [] { return isWindowActive(kMainMenuWindow); });
                        ctx->MouseMoveToPos(drawnItemRect(ctx, AppHeader::kDeveloperMenuButtonLabel).GetCenter());
                        test::waitUntil(ctx, "the developer menu has finished opening", [] { return RadialCircularMenu<2>::animationProgress() >= 1.f; });

                        const std::vector<NamedRect> mainMenuButtons      = popupButtonRects(ctx, kMainMenuWindow);
                        const std::vector<NamedRect> developerMenuButtons = popupButtonRects(ctx, kDeveloperMenuWindow);
                        expect(!mainMenuButtons.empty() && !developerMenuButtons.empty()) << fatal << context << ": both menus are open";
                        for (const NamedRect& developerMenuButton : developerMenuButtons) {
                            expect(screenRect().Contains(developerMenuButton.rect)) << std::format("{}: the developer menu button at {} is inside the screen", context, formatImRect(developerMenuButton.rect));
                            for (const NamedRect& mainMenuButton : mainMenuButtons) {
                                expect(!mainMenuButton.rect.Overlaps(developerMenuButton.rect)) << std::format("{}: the main menu button at {} does not overlap the developer menu button at {}", context, formatImRect(mainMenuButton.rect), formatImRect(developerMenuButton.rect));
                            }
                        }
                    }

                    ctx->MouseMoveToPos(screenRect().GetCenter());
                    test::waitUntil(ctx, "both menus are closed", [] { return !isWindowActive(kMainMenuWindow) && !isWindowActive(kDeveloperMenuWindow); });
                }
            };

            LookAndFeel::mutableInstance().autoHideDelaySeconds = autoHideDelaySeconds;
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    gState = std::addressof(state);

    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "app_header";
    TestApp app(options);
    app.initImGui();
    state.header.loadAssets();
    state.header.requestApplicationSwitchMode  = [](ViewMode) {};
    state.header.requestApplicationSwitchStyle = [](LookAndFeel::Style) {};
    state.header.requestApplicationSwitchTheme = [](LookAndFeel::Theme) {};
    state.header.requestApplicationStop        = [] {};

    const bool result = app.runTests();
    gState            = nullptr;
    return result ? 0 : 1;
}
