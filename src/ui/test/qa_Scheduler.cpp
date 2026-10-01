#include "Scheduler.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>

#include <boost/ut.hpp>

#include <gnuradio-4.0/LifeCycle.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

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

template<typename TPredicate>
[[nodiscard]] bool waitFor(TPredicate&& predicate, std::chrono::seconds limit = std::chrono::seconds(30)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

// a hang cannot be cancelled, so it is reported and the process ends; the limit only decides how long a failure takes
template<typename TFunction>
void expectReturns(std::string_view what, TFunction&& function) {
    std::atomic_bool done{false};
    std::thread      worker([&] {
        function();
        done = true;
    });
    if (!waitFor([&] { return done.load(); })) {
        std::println("hang: {}", what);
        std::fflush(stdout);
        std::_Exit(1);
    }
    worker.join();
}

constexpr std::size_t kRepetitions = 50UZ;

[[nodiscard]] bool hasState(const DigitizerUi::Scheduler& scheduler, State state) { return scheduler->state() == state; }

// GR4 expects a non-empty execution order; a free-running source keeps the scheduler loop reading its messages
[[nodiscard]] gr::Graph makeRunningGraph() {
    gr::Graph graph;
    auto&     source = graph.emplaceBlock<gr::testing::NullSource<float>>();
    auto&     sink   = graph.emplaceBlock<gr::testing::NullSink<float>>();
    expect(graph.connect<"out", "in">(source, sink).has_value()) << fatal;
    return graph;
}

const suite<"Scheduler thread lifecycle"> _lifecycle = [] {
    "destroying a running scheduler returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto scheduler = std::make_unique<DigitizerUi::Scheduler>();
            scheduler->emplaceGraph(makeRunningGraph());
            expect(waitFor([&] { return hasState(*scheduler, State::RUNNING); }));
            expectReturns("destroying a running scheduler", [&] { scheduler.reset(); });
        }
    };

    "a second start while the first is pending does not block"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            DigitizerUi::Scheduler scheduler;
            scheduler.emplaceGraph(makeRunningGraph());
            expect(waitFor([&] { return hasState(scheduler, State::RUNNING); }));
            expect(scheduler->stop().has_value());
            expect(waitFor([&] { return hasState(scheduler, State::STOPPED); }));
            expectReturns("start() twice from STOPPED", [&] {
                expect(scheduler->start().has_value());
                expect(scheduler->start().has_value());
            });
            expect(waitFor([&] { return hasState(scheduler, State::RUNNING); })) << "restarted exactly once and running";
        }
    };

    "a stopped scheduler holds no IO pool thread"_test = [] {
        expect(waitFor([] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; })) << fatal << "watchdogs of earlier tests have left the pool";
        DigitizerUi::Scheduler scheduler;
        scheduler.emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(scheduler, State::RUNNING); }));
        expect(scheduler->stop().has_value());
        expect(waitFor([&] { return hasState(scheduler, State::STOPPED); }));
        expect(waitFor([&] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; })) << "frame pacer and watchdog released";
    };

    "destroying a stopped scheduler returns"_test = [] {
        auto scheduler = std::make_unique<DigitizerUi::Scheduler>();
        scheduler->emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(*scheduler, State::RUNNING); }));
        expect((*scheduler)->stop().has_value());
        expect(waitFor([&] { return hasState(*scheduler, State::STOPPED); }));
        expectReturns("destroying a stopped scheduler", [&] { scheduler.reset(); });
    };
};

} // namespace

// suites run here, not at exit, where the static state they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
