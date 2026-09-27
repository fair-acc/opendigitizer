#include "../common/TouchHandler.hpp" // first: the header must compile standalone

#include <boost/ut.hpp>

#include <algorithm>
#include <chrono>
#include <ostream>
#include <vector>

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {

template<int instance> // distinct instance => distinct TouchHandler static state per test
struct ManualClock {
    using duration                  = std::chrono::milliseconds;
    using rep                       = duration::rep;
    using period                    = duration::period;
    using time_point                = std::chrono::time_point<ManualClock>;
    static constexpr bool is_steady = true;

    static inline time_point current{1s};
    static time_point        now() { return current; }
    static void              advance(duration step) { current += step; }
};

SDL_Event fingerEvent(SDL_EventType type, SDL_FingerID fingerId, float x = 0.5f, float y = 0.5f) {
    SDL_Event event{};
    event.type             = type;
    event.tfinger.type     = type;
    event.tfinger.fingerID = fingerId;
    event.tfinger.x        = x;
    event.tfinger.y        = y;
    return event;
}

struct ButtonEvent {
    int  button;
    bool down;
    bool operator==(const ButtonEvent&) const = default;

    friend std::ostream& operator<<(std::ostream& os, const ButtonEvent& event) { return os << std::format("{{button:{} down:{}}}", event.button, event.down); }
};

std::vector<ButtonEvent> queuedButtonEvents() {
    std::vector<ButtonEvent> events;
    for (const ImGuiInputEvent& event : GImGui->InputEventsQueue) {
        if (event.Type == ImGuiInputEventType_MouseButton) {
            events.push_back({event.MouseButton.Button, event.MouseButton.Down});
        }
    }
    return events;
}

void startWithEmptyInputQueue() {
    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().DisplaySize = {1000.f, 1000.f};
    }
    GImGui->InputEventsQueue.resize(0);
}

constexpr SDL_FingerID kFirstFinger  = 101;
constexpr SDL_FingerID kSecondFinger = 202;

const suite<"TouchHandler finger lifecycle"> _lifecycle = [] {
    "short tap is a left click"_test = [] {
        using Clock   = ManualClock<0>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        expect(eq(Handler::nFingers, 1UZ));
        Clock::advance(100ms);
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kFirstFinger));

        expect(eq(Handler::nFingers, 0UZ));
        expect(Handler::fingerUp);
        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}});
    };

    "long press is a right click"_test = [] {
        using Clock   = ManualClock<1>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        Clock::advance(600ms);
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kFirstFinger));

        expect(eq(Handler::nFingers, 0UZ));
        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}, {ImGuiMouseButton_Right, true}, {ImGuiMouseButton_Right, false}});
    };

    "two-finger gesture releases the single-finger press and never clicks"_test = [] {
        using Clock   = ManualClock<2>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger, 0.2f));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kSecondFinger, 0.8f));
        expect(eq(Handler::nFingers, 2UZ));
        expect(Handler::gestureActive);

        Clock::advance(100ms);
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kSecondFinger, 0.8f));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kFirstFinger, 0.2f));
        Handler::updateGestures();

        expect(eq(Handler::nFingers, 0UZ));
        expect(!Handler::gestureActive);
        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}});
    };

    "finger silent for more than 5 s is released"_test = [] {
        using Clock   = ManualClock<3>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        Clock::advance(4s);
        Handler::updateGestures();
        expect(eq(Handler::nFingers, 1UZ)) << "still within the grace period";

        Clock::advance(2s);
        Handler::updateGestures();
        expect(eq(Handler::nFingers, 0UZ));
        expect(!Handler::fingerPressed[0]);
        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}});
    };
};

// SDL3 SDL_EVENT_FINGER_CANCELED: the touch ended without a lift (e.g. browser 'touchcancel'), hence it is no click
const suite<"TouchHandler cancelled touch"> _cancel = [] {
    "cancel releases the finger without clicking or signalling a lift"_test = [] {
        using Clock   = ManualClock<10>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        Handler::endFrame();
        Clock::advance(600ms); // would be a right click had the finger been lifted
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, kFirstFinger));

        expect(eq(Handler::nFingers, 0UZ));
        expect(!Handler::fingerPressed[0]);
        expect(!Handler::fingerUp);
        expect(Handler::fingerIdToIndex.empty());
        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}}) << "press released, no right click";
    };

    "cancelled touch is no longer active in the next frame"_test = [] {
        using Clock   = ManualClock<11>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, kFirstFinger));
        Clock::advance(10s);
        Handler::updateGestures();

        expect(eq(Handler::nFingers, 0UZ)) << "no inactivity recovery needed";
        Handler::endFrame();
        expect(!Handler::touchActive);
        expect(!Handler::fingerDown);
    };

    "cancelling one of two fingers keeps the gesture until the other lifts"_test = [] {
        using Clock   = ManualClock<12>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger, 0.2f));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kSecondFinger, 0.8f));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, kSecondFinger, 0.8f));
        Handler::updateGestures();
        expect(eq(Handler::nFingers, 1UZ));
        expect(Handler::gestureActive);

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kFirstFinger, 0.2f));
        Handler::updateGestures();
        expect(eq(Handler::nFingers, 0UZ));
        expect(!Handler::gestureActive);
        expect(std::ranges::none_of(queuedButtonEvents(), [](const ButtonEvent& event) { return event.button == ImGuiMouseButton_Right; }));
    };

    "cancel or lift of an unknown finger is ignored"_test = [] {
        using Clock   = ManualClock<13>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_CANCELED, kFirstFinger));
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kSecondFinger));

        expect(eq(Handler::nFingers, 0UZ));
        expect(Handler::fingerIdToIndex.empty());
        expect(queuedButtonEvents().empty());
    };
};

// ImGui knows ImGuiMouseButton_COUNT (5) buttons; a lone finger is the primary pointer, whatever slot it occupies
const suite<"TouchHandler finger slots"> _slots = [] {
    "lone finger in a high slot presses the left button"_test = [] {
        using Clock   = ManualClock<20>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        constexpr SDL_FingerID kFingers = 6;
        for (SDL_FingerID finger = 1; finger <= kFingers; ++finger) {
            Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, finger));
        }
        for (SDL_FingerID finger = 1; finger <= kFingers; ++finger) {
            Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, finger));
        }
        Handler::updateGestures();
        Handler::endFrame();
        GImGui->InputEventsQueue.resize(0);

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, kFirstFinger));
        expect(eq(Handler::fingerIdToIndex.at(kFirstFinger), std::size_t{kFingers - 1})) << "reuses the most recently released slot";
        Clock::advance(100ms);
        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_UP, kFirstFinger));

        expect(queuedButtonEvents() == std::vector<ButtonEvent>{{ImGuiMouseButton_Left, true}, {ImGuiMouseButton_Left, false}});
    };

    "motion of an unknown finger is ignored"_test = [] {
        using Clock   = ManualClock<21>;
        using Handler = DigitizerUi::TouchHandler<Clock>;
        startWithEmptyInputQueue();

        Handler::processSDLEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, kFirstFinger));
        expect(Handler::fingerIdToIndex.empty());
        expect(std::ranges::none_of(Handler::fingerPressed, std::identity{}));

        Clock::advance(6s);
        Handler::updateGestures();
        expect(eq(Handler::nFingers, 0UZ)) << "no phantom finger to recover";
    };
};

} // namespace

// suites run here, not at exit, where the static state they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
