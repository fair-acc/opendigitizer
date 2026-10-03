#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <thread>

#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"
#include "common/FramePacer.hpp"
#include "components/ImGuiNotify.hpp"

namespace DigitizerUi {

// a second stop request while one is pending is an invalid lifecycle transition (REQUESTED_STOP -> STOPPED is the
// scheduler's own), so stop() is only requested from states that can still be stopped
template<typename TScheduler>
[[nodiscard]] std::expected<void, gr::Error> stopUnlessPending(TScheduler& scheduler) {
    const gr::lifecycle::State state = scheduler.state();
    if (state == gr::lifecycle::State::REQUESTED_STOP || state == gr::lifecycle::State::STOPPED) {
        return {};
    }
    return scheduler.stop();
}

struct Scheduler {
private:
    // TODO: When GR gets a type-erased scheduler, this will be replaced with it
    struct SchedulerModel {
        virtual ~SchedulerModel() noexcept                        = default;
        virtual std::string_view uniqueName() const               = 0;
        virtual void             sendMessage(gr::Message message) = 0;
        virtual void             handleMessages(UiGraphModel& fg) = 0;

        virtual std::expected<void, gr::Error> start() = 0;
        virtual std::expected<void, gr::Error> stop()  = 0;
        virtual std::expected<void, gr::Error> pause() = 0;

        virtual const gr::Graph& graph() const = 0;

        virtual gr::lifecycle::State state() const = 0;
    };

    template<typename TScheduler>
    struct SchedulerImpl final : SchedulerModel {
        // shared with queued pool jobs, so a job that starts after the destructor returns does not touch the owner
        struct JobControl {
            std::mutex mutex;
            bool       ownerGone = false;
        };

        TScheduler                  _scheduler;
        std::atomic<std::size_t>    _startsQueued{0UZ};  // start jobs queued or running; each checks the state when it runs
        std::atomic_bool            _pacerQueued{false}; // a pacer job is queued or running and re-checks before it exits
        std::atomic_bool            _uiUpdateShutdown{false};
        std::shared_ptr<JobControl> _startControl = std::make_shared<JobControl>();
        std::shared_ptr<JobControl> _pacerControl = std::make_shared<JobControl>();

        gr::MsgPortIn  _fromScheduler;
        gr::MsgPortOut _toScheduler;

        template<typename... Args>
        explicit SchedulerImpl(Args&&... args) : _scheduler(std::forward<Args>(args)...) {
            connectAndStart();
        }

        SchedulerImpl(gr::Graph&& graph, gr::property_map initParams) : _scheduler(std::move(initParams)) {
            std::ignore = _scheduler.exchange(std::move(graph));
            connectAndStart();
        }

        void discardMessagesFromScheduler() noexcept {
            auto&             reader    = _fromScheduler.streamReader();
            const std::size_t available = reader.available();
            if (available > 0UZ) {
                std::ignore = reader.get(available).consume(available);
            }
        }

        void wakeProgressWaiters() noexcept {
            _scheduler.graph()._progress->incrementAndGet();
            _scheduler.graph()._progress->notify_all();
        }

        [[nodiscard]] bool schedulerAlive() const { return _startsQueued > 0UZ || gr::lifecycle::isActive(_scheduler.state()); }

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

        // returns once the scheduler is neither running nor starting, so a stopped graph holds no pool thread
        void runFramePacer() {
            gr::thread_pool::thread::setThreadName("ui-FramePacer");
            std::size_t oldProgress = _scheduler.graph().progress().value();
            bool        wasPaused   = false;
            do {
                while (!_uiUpdateShutdown && schedulerAlive()) {
                    const bool paused = _scheduler.state() == gr::lifecycle::State::PAUSED;
                    if (paused && !wasPaused) {
                        DigitizerUi::components::Notification::info("Scheduler is paused");
                    }
                    wasPaused = paused;
                    if (gr::lifecycle::isActive(_scheduler.state())) {
                        std::size_t newProgress = _scheduler.graph().progress().value();
                        if (oldProgress != newProgress) {
                            DigitizerUi::globalFramePacer().requestFrame(); // updated data -> request UI frame update
                        } else {
                            _scheduler.graph().progress().wait(oldProgress);
                        }
                        oldProgress = newProgress;
                    } else {
                        std::this_thread::yield(); // start-up in progress
                    }
                }
                _pacerQueued = false;
            } while (!_uiUpdateShutdown && schedulerAlive() && !_pacerQueued.exchange(true)); // a start raced with the exit
        }

        void connectAndStart() {
            if (!_toScheduler.connect(_scheduler.msgIn)) {
                throw gr::exception("Failed to connect _toScheduler -> _scheduler.msgIn");
            }
            if (!_scheduler.msgOut.connect(_fromScheduler)) {
                throw gr::exception("Failed to connect _scheduler.msgOut -> _fromScheduler");
            }
            gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {}, "UI");
            gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");
            gr::sendMessage<gr::message::Command::Get>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");

            requestStart(gr::lifecycle::State::RUNNING);
        }

        // A stopped or idle scheduler has no running loop to read lifecycle messages, hence the direct state change.
        // `toState` PAUSED restores a paused scheduler, e.g. after a .grc was set: the lifecycle has no
        // INITIALISED -> PAUSED. GR4's start() submits its workers without waiting for them, but first waits for the
        // workers of the previous run: when none are left (always for the first start), the start runs here on the
        // caller's thread, independent of any pool (on WASM a pool may first need the main thread to get a thread).
        // Otherwise a job on the IO pool re-queues itself until that run has left, holding no thread while it waits.
        // Jobs run one after another (`_startControl`), and a job that finds the scheduler already started does nothing.
        void requestStart(gr::lifecycle::State toState) {
            if (_startsQueued == 0UZ && !_scheduler.isProcessing()) {
                std::scoped_lock lock(_startControl->mutex);
                startFromStoppedOrIdle(toState);
            } else {
                ++_startsQueued;
                queueStartAfterPreviousRun(toState);
            }
            requestFramePacer();
        }

        void queueStartAfterPreviousRun(gr::lifecycle::State toState) {
            runUnlessOwnerGone(_startControl, [this, toState] {
                if (_scheduler.isProcessing()) {
                    std::this_thread::yield();
                    queueStartAfterPreviousRun(toState); // again later, after other queued work
                    return;
                }
                gr::thread_pool::thread::setThreadName("ui-sched-start");
                startFromStoppedOrIdle(toState);
                --_startsQueued;
                wakeProgressWaiters(); // the pacer re-checks whether the scheduler is alive
            });
        }

        void startFromStoppedOrIdle(gr::lifecycle::State toState) {
            using enum gr::lifecycle::State;
            const gr::lifecycle::State state = _scheduler.state();
            if (state != STOPPED && state != IDLE) {
                return;
            }
            const auto report = [](std::string_view what, const std::expected<void, gr::Error>& result) {
                if (!result) {
                    DigitizerUi::components::Notification::error(std::format("{}: {}", what, result.error().message));
                }
                return result.has_value();
            };
            if (!report("Failed to initialise the flowgraph", _scheduler.changeStateTo(INITIALISED)) || !report("Failed to start the flowgraph", _scheduler.changeStateTo(RUNNING))) {
                return;
            }
            if (toState == PAUSED || toState == REQUESTED_PAUSE) {
                std::ignore = report("Failed to pause the flowgraph", _scheduler.changeStateTo(REQUESTED_PAUSE));
            }
        }

        std::string_view uniqueName() const final { return _scheduler.unique_name; }

        void sendMessage(gr::Message message) final {
            auto output = _toScheduler.streamWriter().reserve<gr::SpanReleasePolicy::ProcessAll>(1UZ);
            output[0]   = std::move(message);
        }

        // replies reach the UI as Final or Notify; a Set is a block's request addressed to another block
        [[nodiscard]] bool isRequestBetweenBlocks(const gr::Message& message) const { return message.cmd == gr::message::Command::Set && !message.serviceName.empty() && message.serviceName != _scheduler.unique_name && message.serviceName != _scheduler.name; }

        void handleMessages(UiGraphModel& graphModel) final {
            const auto available = _fromScheduler.streamReader().available();
            if (available > 0) {
                auto messages = _fromScheduler.streamReader().get(available);
                for (const auto& message : messages) {
                    if (isRequestBetweenBlocks(message)) { // e.g. from a toolbar block: the scheduler forwards it to the addressed block
                        sendMessage(message);
                        continue;
                    }
                    if (message.endpoint == gr::scheduler::property::kGraphGRC) {
                        if (!message.data) {
                            DigitizerUi::components::Notification::error(std::format("Not processed: {} data: {}\n", message.endpoint, message.data.error().message));
                            continue;
                        }

                        const auto& data = *message.data;
                        if (auto it = data.find("originalSchedulerState"); it != data.end()) {
                            // Process reply to kGraphGRC SET message. We need to restart the scheduler

                            if (const auto* originalStateValue = it->second.get_if<int>()) {
                                const auto originalState = static_cast<gr::lifecycle::State>(*originalStateValue);

                                requestStart(originalState);

                                graphModel.requestFullUpdate();
                            } else {
                                DigitizerUi::components::Notification::error(std::format("Invalid originalSchedulerState type in {}", message.endpoint));
                            }
                        } else {
                            // Process reply to kGraphGRC GET message
                            graphModel.processMessage(message);
                        }
                    } else {
                        // process all other messages
                        graphModel.processMessage(message);
                    }
                }
                std::ignore = messages.consume(available);
            }
        }

        [[nodiscard]] bool restartIfNotRunning() {
            const gr::lifecycle::State state = _scheduler.state();
            if (state != gr::lifecycle::State::STOPPED && state != gr::lifecycle::State::IDLE) {
                return false;
            }
            requestStart(gr::lifecycle::State::RUNNING);
            return true;
        }

        std::expected<void, gr::Error> start() final {
            if (restartIfNotRunning()) {
                return {};
            }
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::RUNNING))}}, "UI");
            return {};
        }
        std::expected<void, gr::Error> stop() final {
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_STOP))}}, "UI");
            return {};
        }
        std::expected<void, gr::Error> pause() final {
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_PAUSE))}}, "UI");
            return {};
        }

        const gr::Graph& graph() const final { return _scheduler.graph(); }

        gr::lifecycle::State state() const final { return _scheduler.state(); }

        ~SchedulerImpl() noexcept final {
            _uiUpdateShutdown = true;
            {
                std::scoped_lock lock(_startControl->mutex); // a running start job finishes, a queued one is skipped
                _startControl->ownerGone = true;
            }
            // ~SchedulerBase stops only a RUNNING scheduler before it waits for its workers, a paused one would never finish
            if (gr::lifecycle::isActive(_scheduler.state())) {
                std::ignore = _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);
            }
            // a worker blocks on a full message port, and nobody reads the UI's side any more: drain it until all have left
            while (_scheduler.isProcessing()) {
                discardMessagesFromScheduler();
                std::this_thread::yield();
            }
            _scheduler.waitDone();                       // the workers write to the UI's message ports, which are destroyed before the scheduler
            wakeProgressWaiters();                       // a pacer parked on progress sees the shutdown
            std::scoped_lock lock(_pacerControl->mutex); // a running pacer holds the lock until it has seen the shutdown
            _pacerControl->ownerGone = true;
        }
    };

    std::unique_ptr<SchedulerModel> _scheduler;

public:
    void emplaceGraph(gr::Graph&& graph) {
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreadedBlocking>; // GR4 owns the worker threads
        _scheduler       = std::make_unique<SchedulerImpl<TScheduler>>(std::move(graph), gr::property_map{});
    }

    std::string_view schedulerUniqueName() const { return _scheduler->uniqueName(); }

    void sendMessage(gr::Message message, std::source_location) {
        if (_scheduler) {
            _scheduler->sendMessage(std::move(message));
        }
    }

    void handleMessages(UiGraphModel& graphModel) {
        if (_scheduler) {
            _scheduler->handleMessages(graphModel);
        }
    }

    [[nodiscard]] std::expected<void, gr::Error> stopUnlessPending() { return DigitizerUi::stopUnlessPending(*_scheduler); }

    auto*       operator->() { return _scheduler.operator->(); }
    const auto* operator->() const { return _scheduler.operator->(); }

    operator bool() const { return static_cast<bool>(_scheduler); }
};

} // namespace DigitizerUi

#endif
