#include "GraphSession.hpp"
#include "MapUtils.hpp"
#include "common/FramePacer.hpp"

#include <algorithm>
#include <atomic>
#include <format>
#include <optional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>

#include <boost/ut.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/LifeCycle.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

using namespace gr::od;

using namespace boost::ut;
using gr::lifecycle::State;

namespace {

template<typename Condition>
void waitUntil(std::string_view what, Condition&& condition, std::source_location location = std::source_location::current()) {
    std::println("\twaiting until {} ({}:{})", what, location.file_name(), location.line());
    while (!condition()) {
        std::this_thread::yield();
    }
}

constexpr std::size_t kRepetitions = 50UZ;

[[nodiscard]] bool hasState(const GraphSession& session, State state) { return session.state() == state; }

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

    inline static std::atomic_bool hold{false};
    inline static std::atomic_bool holding{false};

    GR_MAKE_REFLECTABLE(HoldingSink, in);

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
    GraphSession session;

    UiSide() { session.emplaceGraph(makeRunningGraph()); }

    void setGrc(std::string_view grc) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.serviceName = std::string(session.schedulerUniqueName());
        message.endpoint    = gr::scheduler::property::kGraphGRC;
        message.data        = gr::property_map{{"value", std::string(grc)}};
        session.sendMessage(std::move(message));
    }

    void waitUntil(std::string_view what, auto condition, std::source_location location = std::source_location::current()) {
        ::waitUntil(
            what,
            [&] {
                session.receiveMessages();
                return condition();
            },
            location);
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

const suite<"GraphSession lifecycle"> lifecycleTests = [] {
    "destroying a running scheduler returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto session = std::make_unique<GraphSession>();
            session->emplaceGraph(makeRunningGraph());
            waitUntil("the scheduler is RUNNING", [&] { return hasState(*session, State::RUNNING); });
            session.reset();
        }
    };

    "a second start while the first is pending does not block"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            waitUntil("the scheduler is RUNNING", [&] { return hasState(session, State::RUNNING); });
            session.stop();
            waitUntil("the scheduler is STOPPED", [&] { return hasState(session, State::STOPPED); });
            session.start();
            session.start();
            waitUntil("the scheduler restarted exactly once and is RUNNING", [&] { return hasState(session, State::RUNNING); });
        }
    };

    "a host asking to start on every frame while the stopped run still leaves gets one start job"_test = [] {
        const auto pool = gr::thread_pool::Manager::defaultIoPool();
        waitUntil("the watchdogs of earlier tests have left the pool", [&] { return pool->numTasksRunning() == 0UZ; });
        GraphSession session;
        session.emplaceGraph(makeHoldingGraph());
        waitUntil("the scheduler is RUNNING", [&] { return hasState(session, State::RUNNING); });
        HoldingSink::hold = true;
        waitUntil("the sink's worker is held", [] { return HoldingSink::holding.load(); });
        session.stop();
        waitUntil("the scheduler is STOPPED while a job of the run is still held", [&] { return hasState(session, State::STOPPED); });

        constexpr std::size_t kRequests = 1000UZ;
        std::size_t           peakTasks = 0UZ;
        for (std::size_t frame = 0UZ; frame < kRequests; ++frame) {
            session.start();
            peakTasks = std::max(peakTasks, pool->numTasksRunning() + pool->numTasksQueued());
        }
        HoldingSink::hold = false;
        HoldingSink::hold.notify_all();
        waitUntil("the scheduler started once the held job has left", [&] { return hasState(session, State::RUNNING); });
        expect(lt(peakTasks, kRequests / 10UZ)) << std::format("running and queued pool tasks stay independent of the {} requests: peak {}", kRequests, peakTasks);
    };

    "a stopped scheduler holds no IO pool thread"_test = [] {
        waitUntil("the watchdogs of earlier tests have left the pool", [] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; });
        GraphSession session;
        session.emplaceGraph(makeRunningGraph());
        waitUntil("the scheduler is RUNNING", [&] { return hasState(session, State::RUNNING); });
        session.stop();
        waitUntil("the scheduler is STOPPED", [&] { return hasState(session, State::STOPPED); });
        waitUntil("the frame pacer and watchdog released their IO pool threads", [&] { return gr::thread_pool::Manager::defaultIoPool()->numTasksRunning() == 0UZ; });
    };

    "destroying a stopped scheduler returns"_test = [] {
        auto session = std::make_unique<GraphSession>();
        session->emplaceGraph(makeRunningGraph());
        waitUntil("the scheduler is RUNNING", [&] { return hasState(*session, State::RUNNING); });
        session->stop();
        waitUntil("the scheduler is STOPPED", [&] { return hasState(*session, State::STOPPED); });
        session.reset();
    };

    "destroying a paused scheduler returns"_test = [] {
        auto session = std::make_unique<GraphSession>();
        session->emplaceGraph(makeRunningGraph());
        waitUntil("the scheduler is RUNNING", [&] { return hasState(*session, State::RUNNING); });
        session->pause();
        waitUntil("the scheduler is PAUSED", [&] { return hasState(*session, State::PAUSED); });
        session.reset();
    };

    "destroying a scheduler whose start has not run yet returns"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            auto session = std::make_unique<GraphSession>();
            session->emplaceGraph(makeRunningGraph());
            session.reset();
        }
    };

    "a second stop while the first is pending ends stopped, and a stopped session stays stopped"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            waitUntil("the scheduler is RUNNING", [&] { return hasState(session, State::RUNNING); });
            session.stop();
            session.stop();
            waitUntil("the scheduler is STOPPED", [&] { return hasState(session, State::STOPPED); });
            session.stop();
            expect(hasState(session, State::STOPPED));
        }
    };

    "a graph emplaced over a running one runs and its progress requests frames"_test = [] {
        GraphSession session;
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            session.emplaceGraph(makeRunningGraph());
            expect(hasState(session, State::RUNNING)) << fatal << std::format("graph {} runs", i);
            const std::uint64_t requestsBefore = globalFramePacer().requestCount();
            waitUntil(std::format("graph {} paces frames", i), [&] { return globalFramePacer().requestCount() > requestsBefore; });
        }
    };

    "the first start runs on the caller's thread, without waiting for a pool thread"_test = [] {
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            GraphSession session;
            session.emplaceGraph(makeRunningGraph());
            expect(hasState(session, State::RUNNING)) << "running when emplaceGraph() returns";
        }
    };

    "the scheduler runs its graph on GR4's pool, not on the thread that started it"_test = [] {
        GraphSession session;
        session.emplaceGraph(makeRunningGraph());
        waitUntil("the scheduler is RUNNING while this thread is free", [&] { return hasState(session, State::RUNNING); });
    };

    "setting a .grc on a running scheduler runs the new graph"_test = [] {
        UiSide ui;
        ui.waitUntil("the scheduler is RUNNING", [&] { return hasState(ui.session, State::RUNNING); });
        ui.setGrc(kReplacementGrc);
        ui.waitUntil("the UI model shows the new graph and the scheduler runs again", [&] { return ui.showsReplacementGraph() && hasState(ui.session, State::RUNNING); });
    };

    "from sending a .grc until its reply the UI side keeps off the graph, also when it is set again and again"_test = [] {
        UiSide ui;
        ui.waitUntil("the scheduler is RUNNING", [&] { return hasState(ui.session, State::RUNNING); });
        for (std::size_t i = 0; i < kRepetitions; ++i) {
            ui.setGrc(i % 2UZ == 0UZ ? kReplacementGrc : kOtherGrc);
            expect(ui.session.isExchangingGraph()) << "exchanging once the .grc is sent";
            ui.waitUntil(std::format("exchange {}: the reply ends the exchange and the scheduler runs again", i), [&] { return !ui.session.isExchangingGraph() && hasState(ui.session, State::RUNNING); });
        }
    };

    "a .grc that does not load ends the exchange and leaves the scheduler running"_test = [] {
        UiSide ui;
        ui.waitUntil("the scheduler is RUNNING", [&] { return hasState(ui.session, State::RUNNING); });
        ui.setGrc("blocks: [ this is not a flowgraph");
        ui.waitUntil("the error reply ends the exchange", [&] { return !ui.session.isExchangingGraph(); });
        expect(hasState(ui.session, State::RUNNING));
    };

    "setting a .grc on a paused scheduler leaves the new graph paused"_test = [] {
        UiSide ui;
        ui.waitUntil("the scheduler is RUNNING", [&] { return hasState(ui.session, State::RUNNING); });
        ui.session.pause();
        ui.waitUntil("the scheduler is PAUSED", [&] { return hasState(ui.session, State::PAUSED); });
        ui.setGrc(kReplacementGrc);
        ui.waitUntil("the UI model shows the new graph and the scheduler is paused again", [&] { return ui.showsReplacementGraph() && hasState(ui.session, State::PAUSED); });
    };
};

const suite<"GraphSession settings subscription"> subscriptionTests = [] {
    "subscription to all blocks' settings includes blocks emplaced after the request was made"_test = [] {
        constexpr std::string_view kLateSinkType = "gr::testing::NullSink<float32>";
        constexpr std::string_view kLateSinkName = "lateSink";

        UiSide ui;
        ui.waitUntil("the scheduler is RUNNING", [&] { return hasState(ui.session, State::RUNNING); });
        ui.waitUntil("the graph has loaded", [&] { return ui.uniqueNameOf("runningSink").has_value(); });

        ui.emplaceBlock(kLateSinkType, kLateSinkName);
        ui.waitUntil("the block has been emplaced", [&] { return ui.uniqueNameOf(kLateSinkName).has_value(); });
        expect(ui.getDisconnectOnDoneForBlock(kLateSinkName) == std::optional<bool>(true)) << fatal << "block has the expected defaults";

        ui.setSetting(*ui.uniqueNameOf(kLateSinkName), "disconnect_on_done", false);
        ui.waitUntil("the UI graph has received and updated the value for disconnect_on_done", [&] { return ui.getDisconnectOnDoneForBlock(kLateSinkName) == std::optional<bool>(false); });
    };
};

} // namespace

int main() {
    std::ignore = gr::registerBlock<gr::testing::NullSource, float>(gr::globalBlockRegistry());
    std::ignore = gr::registerBlock<gr::testing::NullSink, float>(gr::globalBlockRegistry());
    return boost::ut::cfg<boost::ut::override>.run();
}
