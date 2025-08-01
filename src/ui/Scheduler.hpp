#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <expected>

#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"
#include "components/ImGuiNotify.hpp"

namespace DigitizerUi {

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

        virtual gr::lifecycle::State state() const = 0;
    };

    template<typename TScheduler>
    struct SchedulerImpl final : SchedulerModel {
        TScheduler  _scheduler;
        std::thread _thread;

        gr::MsgPortIn  _fromScheduler;
        gr::MsgPortOut _toScheduler;

        template<typename... Args>
        explicit SchedulerImpl(Args&&... args) : _scheduler(std::forward<Args>(args)...) {
            if (_toScheduler.connect(_scheduler.msgIn) != gr::ConnectionResult::SUCCESS) {
                throw std::format("Failed to connect _toScheduler -> _scheduler.msgIn\n");
            }
            if (_scheduler.msgOut.connect(_fromScheduler) != gr::ConnectionResult::SUCCESS) {
                throw std::format("Failed to connect _scheduler.msgOut -> _fromScheduler\n");
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
            const auto currentState = _scheduler.state();

            if (currentState == toState) {
                return;
            }

            if (currentState != gr::lifecycle::State::STOPPED && currentState != gr::lifecycle::State::IDLE) {
                std::println("Cannot start thread in state: {}", magic_enum::enum_name(currentState));
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
                break;

            case gr::lifecycle::State::RUNNING:
                _thread = std::thread([this]() {
                    if (_scheduler.state() == gr::lifecycle::State::IDLE || _scheduler.state() == gr::lifecycle::State::STOPPED) {
                        if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                            throw std::format("Failed to initialize flowgraph");
                        }
                    }
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                        throw std::format("Failed to start flowgraph processing. state={}", magic_enum::enum_name(_scheduler.state()));
                    }

                    // NOTE: the single threaded scheduler runs its main loop inside its start() function and only returns after its state changes to non-active
                    // We once have to directly change the state to running, after this, all further state updates are performed via the msg API
                });
                break;
            case gr::lifecycle::State::REQUESTED_PAUSE:
            case gr::lifecycle::State::PAUSED:
                _thread = std::thread([this]() {
                    // Lifecycle doesn't allow INITIALIZE->PAUSED
                    if (_scheduler.state() == gr::lifecycle::State::IDLE || _scheduler.state() == gr::lifecycle::State::STOPPED) {
                        if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                            throw std::format("Failed to initialize flowgraph");
                        }
                    }

                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                        throw std::format("Failed to start flowgraph processing");
                    }

                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_PAUSE); !e) {
                        throw std::format("Failed to request pausing flowgraph processing");
                    }

                    // TODO: Not clear what to do, should we block here waiting ? Allowing INITIALIZE->PAUSED would be preferable
                    if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::PAUSED); !e) {
                        throw std::format("Failed to pause flowgraph processing");
                    }
                });
                break;
            }
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

                            const auto originalState = static_cast<gr::lifecycle::State>(std::get<int>(it->second));
                            std::println("Setting Graph GRC finished in GR4, scheduler needs to resume to state {}", magic_enum::enum_name(originalState));

                            startThread(originalState);

                            graphModel.requestGraphUpdate();
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

        std::expected<void, gr::Error> start() final {
            std::print("Scheduler state is {}\n", magic_enum::enum_name(_scheduler.state()));
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
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::RUNNING))}}, "UI");
            return {};
        }

        gr::lifecycle::State state() const final { return _scheduler.state(); }

        ~SchedulerImpl() noexcept final {
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_STOP))}}, "UI");
            _thread.join();
        }
    };

    std::unique_ptr<SchedulerModel> _scheduler;

public:
    template<typename TScheduler, typename... Args>
    void emplaceScheduler(Args&&... args) {
        _scheduler = std::make_unique<SchedulerImpl<TScheduler>>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    void emplaceGraph(Args&&... args) {
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>;
        emplaceScheduler<TScheduler, Args...>(std::forward<Args>(args)...);
    }

    std::string_view schedulerUniqueName() const { return _scheduler->uniqueName(); }

    void sendMessage(gr::Message message) {
        if (_scheduler) {
            _scheduler->sendMessage(std::move(message));
        }
    }

    void handleMessages(UiGraphModel& graphModel) {
        if (_scheduler) {
            _scheduler->handleMessages(graphModel);
        }
    }

    auto*       operator->() { return _scheduler.operator->(); }
    const auto* operator->() const { return _scheduler.operator->(); }

    operator bool() const { return static_cast<bool>(_scheduler); }
};

} // namespace DigitizerUi

#endif
