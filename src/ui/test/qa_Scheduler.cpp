#include "Scheduler.hpp"

#include <boost/ut.hpp>

#include <gnuradio-4.0/LifeCycle.hpp>

using namespace boost::ut;
using gr::lifecycle::State;

namespace {

// accepts stop() exactly when GR4's lifecycle table allows the transition to REQUESTED_STOP
struct ScriptedScheduler {
    State       currentState;
    std::size_t stopCalls = 0;

    [[nodiscard]] State state() const { return currentState; }

    std::expected<void, gr::Error> stop() {
        ++stopCalls;
        if (!gr::lifecycle::isValidTransition(currentState, State::REQUESTED_STOP)) {
            return std::unexpected(gr::Error{"invalid transition to REQUESTED_STOP"});
        }
        currentState = State::REQUESTED_STOP;
        return {};
    }
};

const suite<"Scheduler stopUnlessPending"> _stop = [] {
    "running scheduler is asked to stop exactly once"_test = [] {
        ScriptedScheduler scheduler{State::RUNNING};
        expect(DigitizerUi::stopUnlessPending(scheduler).has_value());
        expect(DigitizerUi::stopUnlessPending(scheduler).has_value()) << "second call while the stop is pending";
        expect(eq(scheduler.stopCalls, 1UZ));
        expect(scheduler.currentState == State::REQUESTED_STOP);
    };

    "pending stop is not requested again"_test = [] {
        ScriptedScheduler scheduler{State::REQUESTED_STOP};
        expect(DigitizerUi::stopUnlessPending(scheduler).has_value());
        expect(eq(scheduler.stopCalls, 0UZ));
    };

    "stopped scheduler is left alone"_test = [] {
        ScriptedScheduler scheduler{State::STOPPED};
        expect(DigitizerUi::stopUnlessPending(scheduler).has_value());
        expect(eq(scheduler.stopCalls, 0UZ));
    };

    "paused and idle schedulers can be stopped"_test = [] {
        for (State state : {State::PAUSED, State::IDLE, State::INITIALISED}) {
            ScriptedScheduler scheduler{state};
            expect(DigitizerUi::stopUnlessPending(scheduler).has_value());
            expect(eq(scheduler.stopCalls, 1UZ));
        }
    };

    "scheduler in error reports that it cannot be stopped"_test = [] {
        ScriptedScheduler scheduler{State::ERROR};
        const auto        result = DigitizerUi::stopUnlessPending(scheduler);
        expect(!result.has_value()) << "ERROR -> REQUESTED_STOP is not a valid transition";
        expect(eq(scheduler.stopCalls, 1UZ));
    };
};

} // namespace

// suites run here, not at exit, where the static state they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
