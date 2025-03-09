#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <expected>

#include <gnuradio-4.0/Scheduler.hpp>

#include "GraphModel.hpp"

namespace DigitizerUi {

struct Scheduler {
private:
    // The thread limit here is mainly for emscripten because the default thread pool will exhaust the browser's limits and be recreated for every new scheduler
    std::shared_ptr<gr::thread_pool::BasicThreadPool> schedulerThreadPool = std::make_shared<gr::thread_pool::BasicThreadPool>("scheduler-pool", gr::thread_pool::CPU_BOUND, 1, 1);

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
                throw fmt::format("Failed to connect _toScheduler -> _scheduler.msgIn\n");
            }
            if (_scheduler.msgOut.connect(_fromScheduler) != gr::ConnectionResult::SUCCESS) {
                throw fmt::format("Failed to connect _scheduler.msgOut -> _fromScheduler\n");
            }
            gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {}, "UI");
            gr::sendMessage<gr::message::Command::Subscribe>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");
            gr::sendMessage<gr::message::Command::Get>(_toScheduler, "", gr::block::property::kSetting, {}, "UI");

            _thread = std::thread([this]() {
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::INITIALISED); !e) {
                    throw fmt::format("Failed to initialize flowgraph");
                }
                if (auto e = _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); !e) {
                    throw fmt::format("Failed to start flowgraph processing");
                }
                // NOTE: the single threaded scheduler runs its main loop inside its start() function and only returns after its state changes to non-active
                // We once have to directly change the state to running, after this, all further state updates are performed via the msg API
            });
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
                    graphModel.processMessage(message);
                }
                std::ignore = messages.consume(available);
            }
        }

        std::expected<void, gr::Error> start() final { return _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); }
        std::expected<void, gr::Error> stop() final { return _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_STOP); }
        std::expected<void, gr::Error> pause() final { return _scheduler.changeStateTo(gr::lifecycle::State::REQUESTED_PAUSE); }
        std::expected<void, gr::Error> resume() final { return _scheduler.changeStateTo(gr::lifecycle::State::RUNNING); }

        ~SchedulerImpl() noexcept final {
            gr::sendMessage<gr::message::Command::Set>(_toScheduler, _scheduler.unique_name, gr::block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(gr::lifecycle::State::REQUESTED_STOP))}}, "UI");
            _thread.join();
        }
    };

    std::unique_ptr<SchedulerModel> _scheduler;

public:
    template<typename... Args>
    void emplaceScheduler(Args&&... args) {
        using TScheduler = gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::singleThreadedBlocking>;

        _scheduler = std::make_unique<SchedulerImpl<TScheduler>>(std::forward<Args>(args)..., schedulerThreadPool);
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
