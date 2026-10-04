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

        virtual std::expected<void, gr::Error> start()  = 0;
        virtual std::expected<void, gr::Error> stop()   = 0;
        virtual std::expected<void, gr::Error> pause()  = 0;
        virtual std::expected<void, gr::Error> resume() = 0;

        virtual const gr::Graph& graph() const = 0;

        virtual gr::lifecycle::State state() const = 0;
    };

    template<typename TScheduler>
    struct SchedulerImpl final : SchedulerModel {
        // shared with queued pacer jobs, so a job that starts after the destructor returns does not touch it
        struct PacerControl {
            std::mutex mutex;
            bool       ownerGone = false;
        };

        TScheduler                    _scheduler;
        std::thread                   _thread;
        std::atomic_bool              _threadRunning{false}; // set when a scheduler thread is created, cleared as its last step
        std::atomic_bool              _startPending{false};  // a start was requested and the thread has not yet left STOPPED/IDLE
        std::atomic_bool              _pacerQueued{false};   // a pacer job is queued or running and re-checks before it exits
        std::atomic_bool              _uiUpdateShutdown{false};
        std::shared_ptr<PacerControl> _pacerControl = std::make_shared<PacerControl>();

        gr::MsgPortIn  _fromScheduler;
        gr::MsgPortOut _toScheduler;

        template<typename... Args>
        explicit SchedulerImpl(Args&&... args) : _scheduler(std::forward<Args>(args)...) {
            connectAndStart();
        }

        // the graph is exchanged before the thread starts: exchanging the graph of a starting scheduler races its start-up
        SchedulerImpl(gr::Graph&& graph, gr::property_map initParams) : _scheduler(std::move(initParams)) {
            std::ignore = _scheduler.exchange(std::move(graph));
            connectAndStart();
        }

        void wakeProgressWaiters() noexcept {
            _scheduler.graph()._progress->incrementAndGet();
            _scheduler.graph()._progress->notify_all();
        }

        struct ThreadExit {
            SchedulerImpl& self;
            ~ThreadExit() {
                self._threadRunning = false;
                self.wakeProgressWaiters(); // the pacer waits on progress and must see the thread gone
            }
        };

        [[nodiscard]] bool schedulerAlive() const { return _threadRunning || gr::lifecycle::isActive(_scheduler.state()); }

        void requestFramePacer() {
            if (_pacerQueued.exchange(true)) {
                return;
            }
            gr::thread_pool::Manager::defaultIoPool()->execute([this, control = _pacerControl]() {
                std::scoped_lock lock(control->mutex);
                if (!control->ownerGone) {
                    runFramePacer();
                }
            });
        }

        // returns once the scheduler is neither running nor starting, so a stopped graph holds no pool thread
        void runFramePacer() {
            gr::thread_pool::thread::setThreadName("ui-FramePacer");
            std::size_t oldProgress = _scheduler.graph().progress().value();
            do {
                while (!_uiUpdateShutdown && schedulerAlive()) {
                    if (_scheduler.state() == gr::lifecycle::State::PAUSED) {
                        DigitizerUi::components::Notification::info("Scheduler is paused");
                    }
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

            startThread(gr::lifecycle::State::RUNNING);
        }

        /// Start the thread but allows to go to other active states than RUNNING, for example pause.
        /// Example use-case
        /// - Scheduler is paused
        /// - Scheduler receives "kGraphGRC" SET message to set YAML
        /// - Scheduler is stopped, now should go back to its original state, PAUSED
        /// It's a bit awkward because lifecycle doesn't allow INITIALIZE->PAUSED
        void startThread(gr::lifecycle::State toState) {
            // claimed before the state is read: a thread started in between has then left STOPPED/IDLE and is not joined
            if (_startPending.exchange(true)) {
                return; // the thread of an earlier start has not left STOPPED/IDLE yet, joining it would block until it is stopped
            }

            const auto currentState = _scheduler.state();

            if (currentState == toState) {
                _startPending = false;
                return;
            }

            if (currentState != gr::lifecycle::State::STOPPED && currentState != gr::lifecycle::State::IDLE) {
                std::println("Cannot start thread in state: {}", magic_enum::enum_name(currentState));
                _startPending = false;
                return;
            }

            // The old thread is stopped, clean it
            if (_thread.joinable()) {
                _thread.join();
                std::println("Thread joined");
            }

            switch (toState) {
            case gr::lifecycle::State::INITIALISED:
            case gr::lifecycle::State::IDLE:
            case gr::lifecycle::State::REQUESTED_STOP:
            case gr::lifecycle::State::STOPPED:
            case gr::lifecycle::State::ERROR: //
                // Can't happen in practice and we have no use for this.
                std::println("OD Scheduler::startThread: Ignoring moving from {} to {}", magic_enum::enum_name(currentState), magic_enum::enum_name(toState));
                _startPending = false;
                break;

            case gr::lifecycle::State::RUNNING:
                _threadRunning = true;
                _thread        = std::thread([this]() {
                    const ThreadExit threadExit{*this};
                    gr::thread_pool::thread::setThreadName("ui-sched#1");
                    if (_scheduler.state() == gr::lifecycle::State::IDLE || _scheduler.state() == gr::lifecycle::State::STOPPED) {
                        if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                            throw gr::exception("Failed to initialize flowgraph");
                        }
                    }
                    _startPending = false;
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                        throw gr::exception(std::format("Failed to start flowgraph processing. state={}", magic_enum::enum_name(_scheduler.state())));
                    }

                    // NOTE: the single threaded scheduler runs its main loop inside its start() function and only returns after its state changes to non-active
                    // We once have to directly change the state to running, after this, all further state updates are performed via the msg API
                });
                break;
            case gr::lifecycle::State::REQUESTED_PAUSE:
            case gr::lifecycle::State::PAUSED:
                _threadRunning = true;
                _thread        = std::thread([this]() {
                    const ThreadExit threadExit{*this};
                    gr::thread_pool::thread::setThreadName("ui-sched#2");
                    // Lifecycle doesn't allow INITIALIZE->PAUSED
                    if (_scheduler.state() == gr::lifecycle::State::IDLE || _scheduler.state() == gr::lifecycle::State::STOPPED) {
                        if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                            throw gr::exception("Failed to initialize flowgraph");
                        }
                    }
                    _startPending = false;

                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                        throw gr::exception("Failed to start flowgraph processing");
                    }

                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_PAUSE); !e) {
                        throw gr::exception("Failed to request pausing flowgraph processing");
                    }

                    // TODO: Not clear what to do, should we block here waiting ? Allowing INITIALIZE->PAUSED would be preferable
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::PAUSED); !e) {
                        throw gr::exception("Failed to pause flowgraph processing");
                    }
                });
                break;
            }

            requestFramePacer();
        }

        std::string_view uniqueName() const final { return _scheduler.unique_name; }

        void sendMessage(gr::Message message) final {
            auto output = _toScheduler.streamWriter().reserve<gr::SpanReleasePolicy::ProcessAll>(1UZ);
            output[0]   = std::move(message);
        }

        void handleMessages(UiGraphModel& graphModel) final {
            const auto available = _fromScheduler.streamReader().available();
            if (available > 0) {
                auto messages = _fromScheduler.streamReader().get(available);
                for (const auto& message : messages) {
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
                                std::println("Setting Graph GRC finished in GR4, scheduler needs to resume to state {}", magic_enum::enum_name(originalState));

                                startThread(originalState);

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

        // a stopped or idle scheduler has no running loop to read lifecycle messages, so it is restarted directly
        [[nodiscard]] bool restartIfNotRunning() {
            const gr::lifecycle::State state = _scheduler.state();
            if (state != gr::lifecycle::State::STOPPED && state != gr::lifecycle::State::IDLE) {
                return false;
            }
            startThread(gr::lifecycle::State::RUNNING);
            return true;
        }

        std::expected<void, gr::Error> start() final {
            std::print("Scheduler state is {}\n", magic_enum::enum_name(_scheduler.state()));
            if (restartIfNotRunning()) {
                return {};
            }
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::RUNNING))}}, "UI");
            return {};
        }
        std::expected<void, gr::Error> stop() final {
            std::print("Scheduler state is {}\n", magic_enum::enum_name(_scheduler.state()));
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_STOP))}}, "UI");
            return {};
        }
        std::expected<void, gr::Error> pause() final {
            std::print("Scheduler state is {}\n", magic_enum::enum_name(_scheduler.state()));
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_PAUSE))}}, "UI");
            return {};
        }
        std::expected<void, gr::Error> resume() final {
            std::print("Scheduler state is {}\n", magic_enum::enum_name(_scheduler.state()));
            if (restartIfNotRunning()) {
                return {};
            }
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::RUNNING))}}, "UI");
            return {};
        }

        const gr::Graph& graph() const final { return _scheduler.graph(); }

        gr::lifecycle::State state() const final { return _scheduler.state(); }

        ~SchedulerImpl() noexcept final {
            _uiUpdateShutdown = true;

            // the start-up thread may still be on its way to RUNNING: stopping is only possible once it left IDLE or
            // INITIALISED, otherwise the stop below is skipped and join() waits for a scheduler that keeps running
            if (_thread.joinable()) {
                for (auto state = _scheduler.state(); state == gr::lifecycle::State::IDLE || state == gr::lifecycle::State::INITIALISED; state = _scheduler.state()) {
                    _scheduler.waitOnState(state);
                }
            }

            // Direct state change (same approach as GR4's ~SchedulerBase).
            // The message-based stop() requires the scheduler's main loop to process
            // it, which may be blocked on waitUntilChanged.  changeStateTo sets the
            // atomic state directly so the main loop exits on its next iteration check.
            // incrementAndGet() does not notify, and a wait that missed the stop is never woken by the thread
            // itself, so request the stop and wake the waiters until the scheduler thread is gone.
            while (_threadRunning) {
                if (gr::lifecycle::isActive(_scheduler.state())) {
                    std::ignore = _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_STOP);
                }
                wakeProgressWaiters();
                std::this_thread::yield();
            }

            if (_thread.joinable()) {
                _thread.join();
            }

            // a running pacer holds the lock until it has seen the shutdown; a queued one finds the owner gone
            wakeProgressWaiters();
            std::scoped_lock lock(_pacerControl->mutex);
            _pacerControl->ownerGone = true;
        }
    };

    std::unique_ptr<SchedulerModel> _scheduler;

public:
    template<typename TScheduler>
    void emplaceScheduler(gr::property_map initParams = {}) {
        _scheduler = std::make_unique<SchedulerImpl<TScheduler>>(std::move(initParams));
    }

    void emplaceGraph(gr::Graph&& graph) {
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>;
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
