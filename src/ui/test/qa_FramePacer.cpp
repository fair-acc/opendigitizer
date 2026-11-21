#include <boost/ut.hpp>
#include <chrono>

// mock SDL: no SDL dependency
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

using namespace gr::od;

using namespace boost::ut;
using namespace std::chrono_literals;

namespace {

struct ManualClock {
    using duration   = std::chrono::nanoseconds;
    using rep        = duration::rep;
    using period     = duration::period;
    using time_point = std::chrono::time_point<ManualClock>;

    static inline time_point current{1s};
    static time_point        now() { return current; }
    static void              advance(duration step) { current += step; }
};

using Pacer = BasicFramePacer<ManualClock>;

void markPendingFramesAsRendered(auto& pacer) {
    for (std::uint32_t i = 0U; i < FramePacer::kMinFramesNeededToResolveWidgetChanges; ++i) {
        pacer.rendered();
    }
}

const suite<"FramePacer basics"> basicTests = [] {
    "construction"_test = [] {
        "default values"_test = [] {
            FramePacer pacer;
            expect(eq(pacer.maxPeriod(), 1s));
            expect(eq(pacer.minPeriod(), 16ms));
            expect(pacer.isDirty()) << "starts dirty to force initial render";
        };

        "custom periods"_test = [] {
            FramePacer pacer{500ms, 8ms};
            expect(eq(pacer.maxPeriod(), 500ms));
            expect(eq(pacer.minPeriod(), 8ms));
        };
    };

    "rate configuration"_test = [] {
        "set min rate updates max period"_test = [] {
            FramePacer pacer;
            pacer.setMinRate(2.0); // 2 Hz → 500ms period
            expect(approx(pacer.minRateHz(), 2.0, 0.01));
            expect(eq(pacer.maxPeriod(), 500ms));
        };

        "set max rate updates min period"_test = [] {
            FramePacer pacer;
            pacer.setMaxRate(30.0); // 30 Hz → ~33ms period
            expect(approx(pacer.maxRateHz(), 30.0, 0.01));
            expect(le(pacer.minPeriod().count() - 33'333'333, 100'000));
        };

        "rate period consistency"_test = [] {
            FramePacer pacer;
            pacer.setMaxPeriod(200ms);
            pacer.setMinPeriod(10ms);
            expect(approx(pacer.minRateHz(), 5.0, 0.01));
            expect(approx(pacer.maxRateHz(), 100.0, 0.01));
        };
    };
};

const suite<"FramePacer render logic"> renderTests = [] {
    "should render when dirty and min period elapsed"_test = [] {
        Pacer pacer{1s, 5ms};
        pacer.rendered();

        pacer.requestFrame();
        expect(pacer.isDirty());

        ManualClock::advance(6ms);
        expect(pacer.shouldRender());
    };

    "should not render when dirty but min period not elapsed"_test = [] {
        Pacer pacer{1s, 50ms};
        pacer.rendered();

        pacer.requestFrame();
        expect(pacer.isDirty());
        expect(!pacer.shouldRender()) << "minPeriod not elapsed";
    };

    "should render when_max period elapsed regardless of dirty"_test = [] {
        Pacer pacer{10ms, 5ms};
        markPendingFramesAsRendered(pacer);
        expect(!pacer.isDirty());

        ManualClock::advance(12ms);
        expect(pacer.shouldRender()) << "forced refresh";
    };

    "rendered decrements requested frame count"_test = [] {
        Pacer pacer;
        markPendingFramesAsRendered(pacer);
        expect(!pacer.isDirty());

        pacer.requestFrame();
        expect(eq(pacer.pendingFrames(), Pacer::kMinFramesNeededToResolveWidgetChanges));

        pacer.rendered();
        expect(pacer.isDirty()) << "expected more than one frame to be requested with each requestFrame()";

        for (std::uint32_t i = 1U; i < Pacer::kMinFramesNeededToResolveWidgetChanges; ++i) {
            pacer.rendered();
        }
        expect(!pacer.isDirty());

        pacer.rendered();
        expect(eq(pacer.pendingFrames(), 0U)) << "counter should do a saturating subtrack";
    };
};

const suite<"FramePacer timeout calculation"> timeoutTests = [] {
    "returns zero when should render immediately"_test = [] {
        Pacer pacer{10ms, 5ms};
        pacer.rendered();
        pacer.requestFrame();

        ManualClock::advance(6ms);
        expect(eq(pacer.getWaitTimeoutMs(), 0));
    };

    "returns short timeout when dirty"_test = [] {
        Pacer pacer{1s, 20ms};
        pacer.rendered();
        pacer.requestFrame();

        const int timeout = pacer.getWaitTimeoutMs();
        expect(ge(timeout, 0));
        expect(le(timeout, 20));
    };

    "returns long timeout when clean"_test = [] {
        Pacer pacer{500ms, 16ms};
        markPendingFramesAsRendered(pacer);
        expect(!pacer.isDirty());

        const int timeout = pacer.getWaitTimeoutMs();
        expect(gt(timeout, 100));
        expect(le(timeout, 500));
    };

    "clamps to at least 1ms"_test = [] {
        using namespace std::chrono_literals;
        expect(eq(FramePacer::waitTimeoutMs(500us, 10ms), 1)) << "a remaining wait below 1 ms waits 1 ms instead of spinning";
        expect(eq(FramePacer::waitTimeoutMs(3700us, 10ms), 3));
        expect(eq(FramePacer::waitTimeoutMs(0ns, 10ms), 0)) << "a due frame does not wait";
        expect(eq(FramePacer::waitTimeoutMs(-2ms, 10ms), 0)) << "an overdue frame does not wait";
        expect(eq(FramePacer::waitTimeoutMs(50ms, 10ms), 10)) << "never longer than the maximum period";
    };
};

const suite<"FramePacer statistics"> statisticsTests = [] {
    "request count increments"_test = [] {
        Pacer pacer;
        pacer.resetMeasurement();

        expect(eq(pacer.requestCount(), 0));
        pacer.requestFrame();
        pacer.requestFrame();
        pacer.requestFrame();
        expect(eq(pacer.requestCount(), 3));
    };

    "render count increments"_test = [] {
        Pacer pacer;
        pacer.resetMeasurement();

        expect(eq(pacer.renderCount(), 0));
        pacer.rendered();
        pacer.rendered();
        expect(eq(pacer.renderCount(), 2));
    };

    "reset measurement clears counters"_test = [] {
        Pacer pacer;
        pacer.requestFrame();
        pacer.rendered();

        pacer.resetMeasurement();
        expect(eq(pacer.requestCount(), 0));
        expect(eq(pacer.renderCount(), 0));
    };

    "measured fps approximates actual rate"_test = [] {
        Pacer pacer{100ms, 10ms};
        pacer.resetMeasurement();

        for (int i = 0; i < 10; ++i) {
            pacer.rendered();
            ManualClock::advance(10ms);
        }

        const double fps = pacer.measuredFps();
        expect(gt(fps, 50.0)) << "~100 Hz theoretical, allow margin";
        expect(lt(fps, 150.0));
    };
};

const suite<"FramePacer global instance"> globalInstanceTests = [] {
    "global pacer is singleton"_test = [] {
        auto& pacer1 = globalFramePacer();
        auto& pacer2 = globalFramePacer();
        expect(&pacer1 == &pacer2);
    };

    "global pacer has default config"_test = [] {
        auto& pacer = globalFramePacer();
        expect(eq(pacer.maxPeriod(), 1s));
        expect(eq(pacer.minPeriod(), 16ms));
    };
};

const suite<"FramePacer SDL event"> sdlEventTests = [] {
    "event type registered on first request"_test = [] {
        FramePacer pacer;
        pacer.requestFrame();
        expect(not eq(FramePacer::sdlEventType(), 0));
    };

    "multiple requests while dirty do not spam events"_test = [] {
        FramePacer pacer;
        expect(eq(FramePacer::_sdlEventType, 1));
        markPendingFramesAsRendered(pacer);

        pacer.requestFrame(); // clean→pending: pushes event
        expect(not eq(FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        expect(pacer.isDirty());

        pacer.requestFrame();
        expect(not eq(FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        pacer.requestFrame();
        expect(not eq(FramePacer::_sdlEventType, 0)) << "subsequent requests shouldn't register new events";
        expect(pacer.isDirty());
        expect(ge(pacer.requestCount(), 3));
    };
};

} // namespace

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
