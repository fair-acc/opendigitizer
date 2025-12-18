#include <boost/ut.hpp>
#include <chrono>
#include <thread>

// mock SDL for unit testing (avoid SDL dependency)
#ifndef SDL_EVENTS_MOCK
#define SDL_EVENTS_MOCK
struct SDL_Event {
    std::uint32_t type;
};
inline std::uint32_t SDL_RegisterEvents(int) {
    static std::uint32_t sdlEventType = 0;
    sdlEventType++;
    return sdlEventType;
}
inline int SDL_PushEvent(SDL_Event*) { return 1; }
#endif

#include "../common/FramePacer.hpp"

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {

const suite<"FramePacer basics"> _1 = [] {
    "construction"_test = [] {
        "default values"_test = [] {
            DigitizerUi::FramePacer pacer;
            expect(eq(pacer.maxPeriod(), 1s));
            expect(eq(pacer.minPeriod(), 16ms));
            expect(pacer.isDirty()) << "starts dirty to force initial render";
        };

        "custom periods"_test = [] {
            DigitizerUi::FramePacer pacer{500ms, 8ms};
            expect(eq(pacer.maxPeriod(), 500ms));
            expect(eq(pacer.minPeriod(), 8ms));
        };
    };

    "rate configuration"_test = [] {
        "set min rate updates max period"_test = [] {
            DigitizerUi::FramePacer pacer;
            pacer.setMinRate(2.0); // 2 Hz → 500ms period
            expect(approx(pacer.minRateHz(), 2.0, 0.01));
            expect(eq(pacer.maxPeriod(), 500ms));
        };

        "set max rate updates min period"_test = [] {
            DigitizerUi::FramePacer pacer;
            pacer.setMaxRate(30.0); // 30 Hz → ~33ms period
            expect(approx(pacer.maxRateHz(), 30.0, 0.01));
            expect(le(pacer.minPeriod().count() - 33'333'333, 100'000));
        };

        "rate period consistency"_test = [] {
            DigitizerUi::FramePacer pacer;
            pacer.setMaxPeriod(200ms);
            pacer.setMinPeriod(10ms);
            expect(approx(pacer.minRateHz(), 5.0, 0.01));
            expect(approx(pacer.maxRateHz(), 100.0, 0.01));
        };
    };
};

const suite<"FramePacer render logic"> _2 = [] {
    "should render when dirty and min period elapsed"_test = [] {
        DigitizerUi::FramePacer pacer{1s, 5ms};
        pacer.rendered(); // clear dirty, set lastRender to now

        pacer.requestFrame();
        expect(pacer.isDirty());

        std::this_thread::sleep_for(6ms);
        expect(pacer.shouldRender());
    };

    "should not render when dirty but min period not elapsed"_test = [] {
        DigitizerUi::FramePacer pacer{1s, 50ms};
        pacer.rendered();

        pacer.requestFrame();
        expect(pacer.isDirty());
        expect(!pacer.shouldRender()) << "minPeriod not elapsed";
    };

    "should render when_max period elapsed regardless of dirty"_test = [] {
        DigitizerUi::FramePacer pacer{10ms, 5ms};
        pacer.rendered();
        expect(!pacer.isDirty());

        std::this_thread::sleep_for(12ms);
        expect(pacer.shouldRender()) << "forced refresh";
    };

    "rendered clears dirty flag"_test = [] {
        DigitizerUi::FramePacer pacer;
        pacer.requestFrame();
        expect(pacer.isDirty());

        pacer.rendered();
        expect(!pacer.isDirty());
    };
};

const suite<"FramePacer timeout calculation"> _3 = [] {
    "returns zero when should render immediately"_test = [] {
        DigitizerUi::FramePacer pacer{10ms, 5ms};
        pacer.rendered();
        pacer.requestFrame();

        std::this_thread::sleep_for(6ms);
        expect(eq(pacer.getWaitTimeoutMs(), 0));
    };

    "returns short timeout when dirty"_test = [] {
        DigitizerUi::FramePacer pacer{1s, 20ms};
        pacer.rendered();
        pacer.requestFrame();

        const int timeout = pacer.getWaitTimeoutMs();
        expect(ge(timeout, 0));
        expect(le(timeout, 20));
    };

    "returns long timeout when clean"_test = [] {
        DigitizerUi::FramePacer pacer{500ms, 16ms};
        pacer.rendered();
        expect(!pacer.isDirty());

        const int timeout = pacer.getWaitTimeoutMs();
        expect(gt(timeout, 100));
        expect(le(timeout, 500));
    };

    "clamps to at least 1ms"_test = [] {
        DigitizerUi::FramePacer pacer{10ms, 5ms};
        pacer.rendered();

        std::this_thread::sleep_for(4ms);
        const int timeout = pacer.getWaitTimeoutMs();
        expect(ge(timeout, 1)) << "timeout should be at least 1ms";
    };
};

const suite<"FramePacer statistics"> _4 = [] {
    "request count increments"_test = [] {
        DigitizerUi::FramePacer pacer;
        pacer.resetMeasurement();

        expect(eq(pacer.requestCount(), 0));
        pacer.requestFrame();
        pacer.requestFrame();
        pacer.requestFrame();
        expect(eq(pacer.requestCount(), 3));
    };

    "render count increments"_test = [] {
        DigitizerUi::FramePacer pacer;
        pacer.resetMeasurement();

        expect(eq(pacer.renderCount(), 0));
        pacer.rendered();
        pacer.rendered();
        expect(eq(pacer.renderCount(), 2));
    };

    "reset measurement clears counters"_test = [] {
        DigitizerUi::FramePacer pacer;
        pacer.requestFrame();
        pacer.rendered();

        pacer.resetMeasurement();
        expect(eq(pacer.requestCount(), 0));
        expect(eq(pacer.renderCount(), 0));
    };

    "measured fps approximates actual rate"_test = [] {
        DigitizerUi::FramePacer pacer{100ms, 10ms};
        pacer.resetMeasurement();

        for (int i = 0; i < 10; ++i) {
            pacer.rendered();
            std::this_thread::sleep_for(10ms);
        }

        const double fps = pacer.measuredFps();
        expect(gt(fps, 50.0)) << "~100 Hz theoretical, allow margin";
        expect(lt(fps, 150.0));
    };
};

const suite<"FramePacer global instance"> _5 = [] {
    "global pacer is singleton"_test = [] {
        auto& pacer1 = DigitizerUi::globalFramePacer();
        auto& pacer2 = DigitizerUi::globalFramePacer();
        expect(&pacer1 == &pacer2);
    };

    "global pacer has default config"_test = [] {
        auto& pacer = DigitizerUi::globalFramePacer();
        expect(eq(pacer.maxPeriod(), 1s));
        expect(eq(pacer.minPeriod(), 16ms));
    };
};

const suite<"FramePacer SDL event"> _6 = [] {
    "event type registered on first request"_test = [] {
        DigitizerUi::FramePacer pacer;
        pacer.requestFrame();
        expect(not eq(DigitizerUi::FramePacer::sdlEventType(), 0));
    };

    "multiple requests while dirty do not spam events"_test = [] {
        DigitizerUi::FramePacer pacer;
        expect(eq(DigitizerUi::FramePacer::_sdlEventType, 1));
        pacer.rendered(); // clear dirty

        pacer.requestFrame(); // clean→dirty: pushes event
        expect(not eq(DigitizerUi::FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        expect(pacer.isDirty());

        pacer.requestFrame();
        expect(not eq(DigitizerUi::FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        pacer.requestFrame();
        expect(not eq(DigitizerUi::FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        expect(pacer.isDirty());
        expect(ge(pacer.requestCount(), 3));
    };
};

} // namespace

int main() { return 0; }
