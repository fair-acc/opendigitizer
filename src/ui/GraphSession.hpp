#ifndef OPENDIGITIZER_UI_GRAPHSESSION_HPP
#define OPENDIGITIZER_UI_GRAPHSESSION_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/SchedulerModel.hpp>

#include "GraphModel.hpp"
#include "common/FramePacer.hpp"
#include "components/ImGuiNotify.hpp"

namespace DigitizerUi {

/// a running flowgraph as the UI sees it: GR4 scheduler, graph model, frame pacer
class GraphSession {
    class Run {
        struct JobControl {
            std::mutex mutex;
            bool       ownerGone = false;
        };

        gr::MsgPortIn                       _fromScheduler;
        gr::MsgPortOut                      _toScheduler;
        std::unique_ptr<gr::SchedulerModel> _scheduler;
        std::atomic_bool                    _startQueued{false};
        std::atomic<gr::lifecycle::State>   _queuedStartState{gr::lifecycle::State::RUNNING};
        std::atomic_bool                    _startPending{false};
        std::atomic_bool                    _pacerQueued{false};
        std::atomic_bool                    _uiUpdateShutdown{false};
        std::atomic_bool                    _graphExchanging{false};
        std::shared_ptr<JobControl>         _startControl = std::make_shared<JobControl>();
        std::shared_ptr<JobControl>         _pacerControl = std::make_shared<JobControl>();

    public:
        explicit Run(gr::Graph&& graph) {
            using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreadedBlocking>;
            _scheduler       = std::make_unique<gr::SchedulerWrapper<TScheduler>>();
            _scheduler->setGraph(std::move(graph));
            connectAndStart();
        }

        Run(const Run&)            = delete;
        Run& operator=(const Run&) = delete;

        ~Run() noexcept {
            _uiUpdateShutdown = true;
            {
                std::scoped_lock lock(_startControl->mutex);
                _startControl->ownerGone = true;
            }
            // GR4 stops only a RUNNING scheduler before it waits for its workers
            if (gr::lifecycle::isActive(state())) {
                std::ignore = block().changeStateTo(gr::lifecycle::State::REQUESTED_STOP);
            }
            // a worker blocks on a full message port nobody reads any more
            while (!_scheduler->waitDone(std::chrono::milliseconds(10))) {
                discardMessagesFromScheduler();
            }
            wakeProgressWaiters();
            std::scoped_lock lock(_pacerControl->mutex);
            _pacerControl->ownerGone = true;
        }

        [[nodiscard]] gr::BlockModel&      block() const { return *_scheduler->asBlockModel(); }
        [[nodiscard]] gr::lifecycle::State state() const { return block().state(); }
        [[nodiscard]] bool                 isExchangingGraph() const { return _graphExchanging; }

        void sendMessage(gr::Message message) {
            if (message.cmd == gr::message::Command::Set && message.endpoint == gr::scheduler::property::kGraphGRC) {
                _graphExchanging = true;
                wakeProgressWaiters();
                // the pacer must leave before the swap, else it waits on the old graph's progress forever
                std::scoped_lock pacerLeft(_pacerControl->mutex);
                writeMessage(std::move(message));
                return;
            }
            writeMessage(std::move(message));
        }

        void writeMessage(gr::Message message) {
            auto output = _toScheduler.streamWriter().reserve<gr::SpanReleasePolicy::ProcessAll>(1UZ);
            output[0]   = std::move(message);
        }

        void handleMessages(UiGraphModel& model) {
            const auto available = _fromScheduler.streamReader().available();
            if (available == 0UZ) {
                return;
            }
            auto messages = _fromScheduler.streamReader().get(available);
            for (const auto& message : messages) {
                if (isRequestBetweenBlocks(message)) {
                    sendMessage(message);
                    continue;
                }
                if (message.endpoint != gr::scheduler::property::kGraphGRC) {
                    model.processMessage(message);
                    continue;
                }
                if (!message.data) {
                    _graphExchanging = false;
                    components::Notification::error(std::format("Not processed: {} data: {}\n", message.endpoint, message.data.error().message));
                    continue;
                }
                const auto& data = *message.data;
                const auto  it   = data.find("originalSchedulerState");
                if (it == data.end()) {
                    model.processMessage(message);
                    continue;
                }
                _graphExchanging = false;
                if (const auto* originalState = it->second.get_if<int>()) {
                    requestStart(static_cast<gr::lifecycle::State>(*originalState));
                    model.requestFullUpdate();
                } else {
                    components::Notification::error(std::format("Invalid originalSchedulerState type in {}", message.endpoint));
                }
            }
            std::ignore = messages.consume(available);
        }

        void start() {
            if (const gr::lifecycle::State s = state(); s == gr::lifecycle::State::STOPPED || s == gr::lifecycle::State::IDLE) {
                requestStart(gr::lifecycle::State::RUNNING);
                return;
            }
            requestState(gr::lifecycle::State::RUNNING);
        }

        // REQUESTED_STOP while one is pending is an invalid transition
        void stop() {
            if (const gr::lifecycle::State s = state(); s != gr::lifecycle::State::REQUESTED_STOP && s != gr::lifecycle::State::STOPPED) {
                requestState(gr::lifecycle::State::REQUESTED_STOP);
            }
        }

        void pause() { requestState(gr::lifecycle::State::REQUESTED_PAUSE); }

    private:
        void requestState(gr::lifecycle::State target) { gr::sendMessage<gr::message::Command::Set>(_toScheduler, std::string(block().uniqueName()), gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(target))}}, "UI"); }

        [[nodiscard]] bool isRequestBetweenBlocks(const gr::Message& message) const { return message.cmd == gr::message::Command::Set && !message.serviceName.empty() && message.serviceName != block().uniqueName() && message.serviceName != block().name(); }

        void connectAndStart() {
            if (!_toScheduler.connect(*block().msgIn) || !block().msgOut->connect(_fromScheduler)) {
                components::Notification::error("cannot connect the UI to the scheduler's message ports");
                return;
            }
            const std::string schedulerName(block().uniqueName());
            gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, schedulerName, gr::block::property::kLifeCycleState, {}, "UI");
            writeMessage(subscriptionMessageForAllBlocksSettings());
            gr::sendMessage<gr::message::Command::Get>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");
            requestStart(gr::lifecycle::State::RUNNING);
        }

        void discardMessagesFromScheduler() noexcept {
            auto&             reader    = _fromScheduler.streamReader();
            const std::size_t available = reader.available();
            if (available > 0UZ) {
                std::ignore = reader.get(available).consume(available);
            }
        }

        void wakeProgressWaiters() noexcept {
            if (const std::shared_ptr<gr::Sequence> progress = _scheduler->progressHandle()) {
                progress->incrementAndGet();
                progress->notify_all();
            }
        }

        [[nodiscard]] bool pacerMayRun() const { return !_uiUpdateShutdown && !_graphExchanging && schedulerAlive(); }
        [[nodiscard]] bool schedulerAlive() const { return _startQueued || gr::lifecycle::isActive(state()); }

        static void runUnlessOwnerGone(const std::shared_ptr<JobControl>& control, auto job) {
            gr::thread_pool::Manager::defaultIoPool()->execute([control, job = std::move(job)]() {
                std::scoped_lock lock(control->mutex);
                if (!control->ownerGone) {
                    job();
                }
            });
        }

        void requestFramePacer() {
            if (_pacerQueued.exchange(true)) {
                return;
            }
            runUnlessOwnerGone(_pacerControl, [this] { runFramePacer(); });
        }

        // compares against the value read before pacerMayRun(): a wake-up in between is not lost
        void runFramePacer() {
            gr::thread_pool::thread::setThreadName("ui-FramePacer");
            bool wasPaused = false;
            do {
                const std::shared_ptr<gr::Sequence> progress    = _scheduler->progressHandle();
                std::size_t                         oldProgress = progress ? progress->value() : 0UZ;
                while (progress && pacerMayRun()) {
                    const bool paused = state() == gr::lifecycle::State::PAUSED;
                    if (paused && !wasPaused) {
                        components::Notification::info("Scheduler is paused");
                    }
                    wasPaused = paused;
                    if (gr::lifecycle::isActive(state())) {
                        const std::size_t newProgress = progress->value();
                        if (oldProgress != newProgress) {
                            globalFramePacer().requestFrame();
                        } else {
                            progress->wait(oldProgress);
                        }
                        oldProgress = newProgress;
                    } else {
                        std::this_thread::yield();
                    }
                }
                _pacerQueued = false;
            } while (pacerMayRun() && !_pacerQueued.exchange(true));
        }

        // GR4's start waits for the previous run's workers: one IO job re-queues itself instead of blocking a thread
        void requestStart(gr::lifecycle::State toState) {
            if (!_startQueued && !_scheduler->isProcessing()) {
                std::scoped_lock lock(_startControl->mutex);
                startFromStoppedOrIdle(toState);
            } else {
                _queuedStartState = toState;
                _startPending     = true;
                if (!_startQueued.exchange(true)) {
                    queueStartAfterPreviousRun();
                }
            }
            requestFramePacer();
        }

        void queueStartAfterPreviousRun() {
            runUnlessOwnerGone(_startControl, [this] {
                if (_scheduler->isProcessing()) {
                    std::this_thread::yield();
                    queueStartAfterPreviousRun();
                    return;
                }
                gr::thread_pool::thread::setThreadName("ui-sched-start");
                _startPending = false;
                startFromStoppedOrIdle(_queuedStartState);
                _startQueued = false;
                if (_startPending && !_startQueued.exchange(true)) {
                    queueStartAfterPreviousRun();
                }
                wakeProgressWaiters();
            });
        }

        void startFromStoppedOrIdle(gr::lifecycle::State toState) {
            using enum gr::lifecycle::State;
            if (const gr::lifecycle::State s = state(); s != STOPPED && s != IDLE) {
                return;
            }
            const auto report = [](std::string_view what, const std::expected<void, gr::Error>& result) {
                if (!result) {
                    components::Notification::error(std::format("{}: {}", what, result.error().message));
                }
                return result.has_value();
            };
            if (!report("Failed to initialise the flowgraph", block().changeStateTo(INITIALISED)) || !report("Failed to start the flowgraph", block().changeStateTo(RUNNING))) {
                return;
            }
            if (toState == PAUSED || toState == REQUESTED_PAUSE) {
                std::ignore = report("Failed to pause the flowgraph", block().changeStateTo(REQUESTED_PAUSE));
            }
        }
    };

    std::unique_ptr<Run> _run;

public:
    UiGraphModel graphModel;

    GraphSession() {
        graphModel.sendMessage_ = [this](gr::Message message, std::source_location) { sendMessage(std::move(message)); };
    }
    GraphSession(const GraphSession&)            = delete;
    GraphSession& operator=(const GraphSession&) = delete;

    void emplaceGraph(gr::Graph&& graph) {
        _run.reset();
        _run = std::make_unique<Run>(std::move(graph));
        graphModel.handleTopologyUpdated();
    }

    [[nodiscard]] explicit             operator bool() const noexcept { return static_cast<bool>(_run); }
    [[nodiscard]] gr::lifecycle::State state() const { return _run->state(); }
    [[nodiscard]] std::string_view     schedulerUniqueName() const { return _run->block().uniqueName(); }
    [[nodiscard]] bool                 isExchangingGraph() const { return _run && _run->isExchangingGraph(); }

    [[nodiscard]] gr::Graph& graph() const { return *_run->block().graph(); }

    void start() { _run->start(); }
    void stop() { _run->stop(); }
    void pause() { _run->pause(); }

    void sendToScheduler(std::string_view endpoint, gr::property_map data) {
        gr::Message message;
        message.cmd         = gr::message::Command::Set;
        message.endpoint    = std::string(endpoint);
        message.serviceName = std::string(schedulerUniqueName());
        message.data        = std::move(data);
        sendMessage(std::move(message));
    }

    void sendMessage(gr::Message message) {
        if (_run) {
            _run->sendMessage(std::move(message));
        }
    }

    void handleMessages() {
        if (_run) {
            _run->handleMessages(graphModel);
        }
    }
};

} // namespace DigitizerUi

#endif
