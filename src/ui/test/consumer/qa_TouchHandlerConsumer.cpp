#include "common/TouchHandler.hpp"

#include <boost/ut.hpp>

using namespace boost::ut;

static_assert(sizeof(ImDrawIdx) == 4, "32-bit vertex indices must propagate with the imgui target");
static_assert(sizeof(ImWchar) == 4, "IMGUI_USE_WCHAR32 must propagate with the imgui target");

namespace {

// instantiates the pinch-zoom path, which must not need any OpenDigitizer library code (e.g. LookAndFeel.cpp)
[[maybe_unused]] constexpr auto kZoomPath = &DigitizerUi::TouchHandler<std::chrono::system_clock, true>::EndZoomablePlot;

const suite<"TouchHandler outside src/ui"> _consumer = [] {
    "consumer and compiled ImGui agree on version and data layout"_test = [] {
        ImGui::CreateContext();
        expect(IMGUI_CHECKVERSION());
    };

    "tap is tracked without linking the UI library"_test = [] {
        using Handler              = DigitizerUi::TouchHandler<>;
        ImGui::GetIO().DisplaySize = {100.f, 100.f};

        SDL_Event event{};
        event.type             = SDL_EVENT_FINGER_DOWN;
        event.tfinger.fingerID = 1;
        Handler::processSDLEvent(event);
        expect(eq(Handler::nFingers, 1UZ));

        event.type = SDL_EVENT_FINGER_UP;
        Handler::processSDLEvent(event);
        Handler::endFrame();
        expect(eq(Handler::nFingers, 0UZ));
        expect(!Handler::touchActive);
    };
};

} // namespace

// suites run here, not at exit, where the static state they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
