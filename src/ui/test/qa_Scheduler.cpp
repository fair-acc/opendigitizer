#include "GraphModel.hpp"
#include "Scheduler.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <print>
#include <source_location>
#include <string_view>
#include <thread>

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
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

// holds its worker inside work() while `hold` is set, so a stopped run keeps a job until the test releases it
struct HoldingSink : gr::Block<HoldingSink> {
    using Description = gr::Doc<"test-only sink that holds its worker while asked to">;

    gr::PortIn<float> in;

    GR_MAKE_REFLECTABLE(HoldingSink, in);

    inline static std::atomic_bool hold{false};
    inline static std::atomic_bool holding{false};

    void processOne(float) noexcept {
        while (hold) {
            holding = true;
            hold.wait(true);
        }
        holding = false;
    }
};

// the source and the sink run in separate job lists: the source's worker handles the stop, the sink's holds
[[nodiscard]] gr::Graph makeHoldingGraph() {
    gr::Graph graph;
    auto&     source = graph.emplaceBlock<gr::testing::NullSource<float>>();
    auto&     sink   = graph.emplaceBlock<HoldingSink>();
    expect(graph.connect<"out", "in">(source, sink).has_value()) << fatal;
    return graph;
}

constexpr std::string_view kReplacementGrc = R"(blocks:
  - id: gr::testing::NullSource<float32>
    parameters:
      name: grcSource
  - id: gr::testing::NullSink<float32>
    parameters:
      name: grcSink
connections:
  - [grcSource, 0, grcSink, 0]
)";

// the UI side of a scheduler as the App's frame loop drives it: messages are pumped into a graph model
struct UiSide {
    DigitizerUi::Scheduler    scheduler;
    DigitizerUi::UiGraphModel graphModel;

    UiSide() {
        graphModel.sendMessage_ = [this](gr::Message message, std::source_location location) { scheduler.sendMessage(std::move(message), location); };
        scheduler.emplaceGraph(makeRunningGraph());
    }

    void setGrc(std::string_view grc) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.serviceName = std::string(scheduler.schedulerUniqueName());
        message.endpoint    = gr::scheduler::property::kGraphGRC;
        message.data        = gr::property_map{{"value", std::string(grc)}};
        scheduler.sendMessage(std::move(message), std::source_location::current());
    }

    [[nodiscard]] bool pumpUntil(auto predicate) {
        return waitFor([&] {
            scheduler.handleMessages(graphModel);
            return predicate();
        });
    }

    [[nodiscard]] bool showsReplacementGraph() { return static_cast<bool>(graphModel.recursiveFindBlockByName("grcSource")); }
};

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
            expect(waitFor([&] { return hasState(scheduler, State::RUNNING); })) << std::format("restarted exactly once and running, state {}", magic_enum::enum_name(scheduler->state()));
        }
    };

    "a host asking to start on every frame while the stopped run still leaves gets one start job"_test = [] {
        const auto pool = gr::thread_pool::Manager::defaultIoPool();
        expect(waitFor([&] { return pool->numTasksRunning() == 0UZ; })) << fatal << "watchdogs of earlier tests have left the pool";
        DigitizerUi::Scheduler scheduler;
        scheduler.emplaceGraph(makeHoldingGraph());
        expect(waitFor([&] { return hasState(scheduler, State::RUNNING); })) << fatal;
        HoldingSink::hold = true;
        expect(waitFor([] { return HoldingSink::holding.load(); })) << fatal << "the sink's worker is held";
        expect(scheduler->stop().has_value());
        expect(waitFor([&] { return hasState(scheduler, State::STOPPED); })) << fatal << "stopped while a job of the run is still held";

        const auto peakTasksOver = [&](std::size_t frames) { // as a host re-asserts its wanted state every frame
            std::size_t peak = 0UZ;
            for (std::size_t frame = 0UZ; frame < frames; ++frame) {
                expect(scheduler->start().has_value());
                peak = std::max(peak, pool->numTasksRunning());
            }
            return peak;
        };
        // one queued start job however often asked; GR4's own tasks (watchdog, workers changing over) vary by one or two
        const std::size_t after100  = peakTasksOver(100UZ);
        const std::size_t after1000 = std::max(after100, peakTasksOver(900UZ));
        HoldingSink::hold           = false;
        HoldingSink::hold.notify_all();
        expect(waitFor([&] { return hasState(scheduler, State::RUNNING); })) << "started once the held job has left";
        expect(le(after1000, after100 + 2UZ) && lt(after1000, 20UZ)) << std::format("running pool tasks do not grow with the requests: {} after 100, {} after 1000", after100, after1000);
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

    "destroying a paused scheduler returns"_test = [] {
        auto scheduler = std::make_unique<DigitizerUi::Scheduler>();
        scheduler->emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(*scheduler, State::RUNNING); }));
        expect((*scheduler)->pause().has_value());
        expect(waitFor([&] { return hasState(*scheduler, State::PAUSED); }));
        expectReturns("destroying a paused scheduler", [&] { scheduler.reset(); });
    };

    "destroying a scheduler whose start has not run yet returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto scheduler = std::make_unique<DigitizerUi::Scheduler>();
            scheduler->emplaceGraph(makeRunningGraph());
            expectReturns("destroying a starting scheduler", [&] { scheduler.reset(); });
        }
    };

    "the first start runs on the caller's thread, without waiting for a pool thread"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            DigitizerUi::Scheduler scheduler;
            scheduler.emplaceGraph(makeRunningGraph());
            expect(hasState(scheduler, State::RUNNING)) << "running when emplaceGraph() returns";
        }
    };

    "the scheduler runs its graph on GR4's pool, not on the thread that started it"_test = [] {
        DigitizerUi::Scheduler scheduler;
        expectReturns("emplacing a graph", [&] { scheduler.emplaceGraph(makeRunningGraph()); });
        expect(waitFor([&] { return hasState(scheduler, State::RUNNING); })) << "running while this thread is free";
    };

    "setting a .grc on a running scheduler runs the new graph"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.scheduler, State::RUNNING); })) << fatal;
        ui.setGrc(kReplacementGrc);
        expect(ui.pumpUntil([&] { return ui.showsReplacementGraph() && hasState(ui.scheduler, State::RUNNING); })) << "the UI model shows the new graph and the scheduler runs again";
    };

    "setting a .grc on a paused scheduler leaves the new graph paused"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.scheduler, State::RUNNING); })) << fatal;
        expect(ui.scheduler->pause().has_value());
        expect(ui.pumpUntil([&] { return hasState(ui.scheduler, State::PAUSED); })) << fatal;
        ui.setGrc(kReplacementGrc);
        expect(ui.pumpUntil([&] { return ui.showsReplacementGraph() && hasState(ui.scheduler, State::PAUSED); })) << "the UI model shows the new graph and the scheduler is paused again";
    };
};

} // namespace

// suites run here, not at exit, where the static state they use may already be destroyed
int main() {
    std::ignore = gr::registerBlock<gr::testing::NullSource, float>(gr::globalBlockRegistry()); // used by kReplacementGrc
    std::ignore = gr::registerBlock<gr::testing::NullSink, float>(gr::globalBlockRegistry());
    return boost::ut::cfg<boost::ut::override>.run();
}
