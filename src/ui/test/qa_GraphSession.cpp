#include "GraphSession.hpp"
#include "MapUtils.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <optional>
#include <print>
#include <string>
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

[[nodiscard]] bool hasState(const DigitizerUi::GraphSession& session, State state) { return session.state() == state; }

[[nodiscard]] gr::Graph makeRunningGraph() {
    gr::Graph graph;
    auto&     source = graph.emplaceBlock<gr::testing::NullSource<float>>({{"name", "runningSource"}});
    auto&     sink   = graph.emplaceBlock<gr::testing::NullSink<float>>({{"name", "runningSink"}});
    expect(graph.connect<"out", "in">(source, sink).has_value()) << fatal;
    return graph;
}

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

constexpr std::string_view kOtherGrc = R"(blocks:
  - id: gr::testing::NullSource<float32>
    parameters:
      name: otherSource
  - id: gr::testing::NullSink<float32>
    parameters:
      name: otherSink
connections:
  - [otherSource, 0, otherSink, 0]
)";

struct UiSide {
    DigitizerUi::GraphSession session;

    UiSide() { session.emplaceGraph(makeRunningGraph()); }

    void setGrc(std::string_view grc) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.serviceName = std::string(session.schedulerUniqueName());
        message.endpoint    = gr::scheduler::property::kGraphGRC;
        message.data        = gr::property_map{{"value", std::string(grc)}};
        session.sendMessage(std::move(message));
    }

    [[nodiscard]] bool pumpUntil(auto predicate, std::chrono::seconds limit = std::chrono::seconds(30)) {
        return waitFor(
            [&] {
                session.handleMessages();
                return predicate();
            },
            limit);
    }

    [[nodiscard]] bool showsReplacementGraph() { return static_cast<bool>(session.graphModel.recursiveFindBlockByName("grcSource")); }

    void emplaceBlock(std::string_view blockType, std::string_view blockName) {
        session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, //
            gr::property_map{{"type", std::string(blockType)}, {"properties", gr::property_map{{"name", std::string(blockName)}}}});
    }

    void setSetting(std::string_view blockUniqueName, std::string_view key, gr::pmt::Value value) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.serviceName = std::string(blockUniqueName);
        message.endpoint    = gr::block::property::kSetting;
        message.data        = gr::property_map{{std::pmr::string(key), std::move(value)}};
        session.sendMessage(std::move(message));
    }

    [[nodiscard]] std::optional<std::string> uniqueNameOf(std::string_view blockName) {
        const auto found = session.graphModel.recursiveFindBlockByName(blockName);
        return found ? std::optional<std::string>(found.block->blockUniqueName) : std::nullopt;
    }

    [[nodiscard]] std::optional<bool> getDisconnectOnDoneForBlock(std::string_view blockName) {
        const auto found = session.graphModel.recursiveFindBlockByName(blockName);
        if (!found) {
            return std::nullopt;
        }
        const auto value = getOptionalProperty<bool>(found.block->blockSettings, "disconnect_on_done");
        return value ? std::optional<bool>(*value) : std::nullopt;
    }
};

const suite<"GraphSession lifecycle"> _lifecycle = [] {
    "destroying a running scheduler returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto session = std::make_unique<DigitizerUi::GraphSession>();
            session->emplaceGraph(makeRunningGraph());
            expect(waitFor([&] { return hasState(*session, State::RUNNING); }));
            expectReturns("destroying a running scheduler", [&] { session.reset(); });
        }
    };

    "a second start while the first is pending does not block"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            DigitizerUi::GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            expect(waitFor([&] { return hasState(session, State::RUNNING); }));
            session.stop();
            expect(waitFor([&] { return hasState(session, State::STOPPED); }));
            expectReturns("start() twice from STOPPED", [&] {
                session.start();
                session.start();
            });
            expect(waitFor([&] { return hasState(session, State::RUNNING); })) << std::format("restarted exactly once and running, state {}", magic_enum::enum_name(session.state()));
        }
    };

    "a host asking to start on every frame while the stopped run still leaves gets one start job"_test = [] {
        const auto pool = gr::thread_pool::Manager::defaultIoPool();
        expect(waitFor([&] { return pool->numTasksRunning() == 0UZ; })) << fatal << "watchdogs of earlier tests have left the pool";
        DigitizerUi::GraphSession session;
        session.emplaceGraph(makeHoldingGraph());
        expect(waitFor([&] { return hasState(session, State::RUNNING); })) << fatal;
        HoldingSink::hold = true;
        expect(waitFor([] { return HoldingSink::holding.load(); })) << fatal << "the sink's worker is held";
        session.stop();
        expect(waitFor([&] { return hasState(session, State::STOPPED); })) << fatal << "stopped while a job of the run is still held";

        constexpr std::size_t kRequests = 1000UZ;
        std::size_t           peakTasks = 0UZ;
        for (std::size_t frame = 0UZ; frame < kRequests; ++frame) {
            session.start();
            peakTasks = std::max(peakTasks, pool->numTasksRunning() + pool->numTasksQueued());
        }
        HoldingSink::hold = false;
        HoldingSink::hold.notify_all();
        expect(waitFor([&] { return hasState(session, State::RUNNING); })) << "started once the held job has left";
        expect(lt(peakTasks, kRequests / 10UZ)) << std::format("running and queued pool tasks stay independent of the {} requests: peak {}", kRequests, peakTasks);
    };

    "a stopped scheduler holds no IO pool thread"_test = [] {
        expect(waitFor([] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; })) << fatal << "watchdogs of earlier tests have left the pool";
        DigitizerUi::GraphSession session;
        session.emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(session, State::RUNNING); }));
        session.stop();
        expect(waitFor([&] { return hasState(session, State::STOPPED); }));
        expect(waitFor([&] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; })) << "frame pacer and watchdog released";
    };

    "destroying a stopped scheduler returns"_test = [] {
        auto session = std::make_unique<DigitizerUi::GraphSession>();
        session->emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(*session, State::RUNNING); }));
        session->stop();
        expect(waitFor([&] { return hasState(*session, State::STOPPED); }));
        expectReturns("destroying a stopped scheduler", [&] { session.reset(); });
    };

    "destroying a paused scheduler returns"_test = [] {
        auto session = std::make_unique<DigitizerUi::GraphSession>();
        session->emplaceGraph(makeRunningGraph());
        expect(waitFor([&] { return hasState(*session, State::RUNNING); }));
        session->pause();
        expect(waitFor([&] { return hasState(*session, State::PAUSED); }));
        expectReturns("destroying a paused scheduler", [&] { session.reset(); });
    };

    "destroying a scheduler whose start has not run yet returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto session = std::make_unique<DigitizerUi::GraphSession>();
            session->emplaceGraph(makeRunningGraph());
            expectReturns("destroying a starting scheduler", [&] { session.reset(); });
        }
    };

    "a second stop while the first is pending ends stopped, and a stopped session stays stopped"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            DigitizerUi::GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            expect(waitFor([&] { return hasState(session, State::RUNNING); })) << fatal;
            session.stop();
            session.stop();
            expect(waitFor([&] { return hasState(session, State::STOPPED); })) << std::format("stopped, state {}", magic_enum::enum_name(session.state()));
            session.stop();
            expect(hasState(session, State::STOPPED));
        }
    };

    "a graph emplaced over a running one runs and its progress requests frames"_test = [] {
        DigitizerUi::GraphSession session;
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            session.emplaceGraph(makeRunningGraph());
            expect(hasState(session, State::RUNNING)) << fatal << std::format("graph {} runs", i);
            const std::uint64_t requestsBefore = DigitizerUi::globalFramePacer().requestCount();
            expect(waitFor([&] { return DigitizerUi::globalFramePacer().requestCount() > requestsBefore; })) << fatal << std::format("graph {} paces frames", i);
        }
    };

    "the first start runs on the caller's thread, without waiting for a pool thread"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            DigitizerUi::GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            expect(hasState(session, State::RUNNING)) << "running when emplaceGraph() returns";
        }
    };

    "the scheduler runs its graph on GR4's pool, not on the thread that started it"_test = [] {
        DigitizerUi::GraphSession session;
        expectReturns("emplacing a graph", [&] { session.emplaceGraph(makeRunningGraph()); });
        expect(waitFor([&] { return hasState(session, State::RUNNING); })) << "running while this thread is free";
    };

    "setting a .grc on a running scheduler runs the new graph"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::RUNNING); })) << fatal;
        ui.setGrc(kReplacementGrc);
        expect(ui.pumpUntil([&] { return ui.showsReplacementGraph() && hasState(ui.session, State::RUNNING); })) << "the UI model shows the new graph and the scheduler runs again";
    };

    "from sending a .grc until its reply the UI side keeps off the graph, also when it is set again and again"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::RUNNING); })) << fatal;
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            ui.setGrc(i % 2UZ == 0UZ ? kReplacementGrc : kOtherGrc);
            expect(ui.session.isExchangingGraph()) << "exchanging once the .grc is sent";
            const bool done = ui.pumpUntil([&] { return !ui.session.isExchangingGraph() && hasState(ui.session, State::RUNNING); });
            expect(done) << std::format("exchange {}: the reply ends the exchange and the scheduler runs again; exchanging {}, state {}", i, ui.session.isExchangingGraph(), magic_enum::enum_name(ui.session.state()));
            if (!done) {
                return;
            }
        }
    };

    "a .grc that does not load ends the exchange and leaves the scheduler running"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::RUNNING); })) << fatal;
        ui.setGrc("blocks: [ this is not a flowgraph");
        expect(ui.pumpUntil([&] { return !ui.session.isExchangingGraph(); })) << "the error reply ends the exchange";
        expect(hasState(ui.session, State::RUNNING));
    };

    "setting a .grc on a paused scheduler leaves the new graph paused"_test = [] {
        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::RUNNING); })) << fatal;
        ui.session.pause();
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::PAUSED); })) << fatal;
        ui.setGrc(kReplacementGrc);
        expect(ui.pumpUntil([&] { return ui.showsReplacementGraph() && hasState(ui.session, State::PAUSED); })) << "the UI model shows the new graph and the scheduler is paused again";
    };
};

const suite<"GraphSession settings subscription"> _subscription = [] {
    "subscription to all blocks' settings includes blocks emplaced after the request was made"_test = [] {
        constexpr std::string_view kLateSinkType = "gr::testing::NullSink<float32>";
        constexpr std::string_view kLateSinkName = "lateSink";

        UiSide ui;
        expect(ui.pumpUntil([&] { return hasState(ui.session, State::RUNNING); })) << fatal;
        expect(ui.pumpUntil([&] { return ui.uniqueNameOf("runningSink").has_value(); })) << fatal << "the model holds the initial graph";

        ui.emplaceBlock(kLateSinkType, kLateSinkName);
        expect(ui.pumpUntil([&] { return ui.uniqueNameOf(kLateSinkName).has_value(); })) << fatal << "block has been emplaced";
        expect(ui.getDisconnectOnDoneForBlock(kLateSinkName) == std::optional<bool>(true)) << fatal << "block has the expected defaults";

        ui.setSetting(*ui.uniqueNameOf(kLateSinkName), "disconnect_on_done", false);
        expect(ui.pumpUntil([&] { return ui.getDisconnectOnDoneForBlock(kLateSinkName) == std::optional<bool>(false); }, std::chrono::seconds(5))) //
            << "the UI graph has received and updated the value for disconnect_on_done";
    };
};

} // namespace

int main() {
    std::ignore = gr::registerBlock<gr::testing::NullSource, float>(gr::globalBlockRegistry());
    std::ignore = gr::registerBlock<gr::testing::NullSink, float>(gr::globalBlockRegistry());
    return boost::ut::cfg<boost::ut::override>.run();
}
