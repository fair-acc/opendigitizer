#ifndef OPENDIGITIZER_TOOLBAR_BLOCK_H
#define OPENDIGITIZER_TOOLBAR_BLOCK_H

#include <expected>

#include <format>

#include <gnuradio-4.0/Block.hpp>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi {
namespace play_stop {
enum class State { PlayStop, Play, PlayStream, Pause, Stopped, Error };

inline bool isValidTransition(State from, State to) {
    using enum play_stop::State;
    switch (from) {
    case Stopped: return to == PlayStop || to == Play || to == PlayStream;
    case PlayStop:
    case Play:
    case PlayStream: return to == Pause || to == Stopped;
    case Pause: return to == PlayStop || to == Play || to == PlayStream || to == Stopped;
    case Error: return to == Stopped;
    default: return false; // undefined state
    }
}

using gr::lifecycle::StorageType;

template<typename TDerived, StorageType storageType = StorageType::NON_ATOMIC>
class StateMachine {
    using enum play_stop::State;

protected:
    using StateStorage  = std::conditional_t<storageType == StorageType::ATOMIC, std::atomic<State>, State>;
    StateStorage _state = State::Stopped;

    void setAndNotifyState(State newState) {
        if constexpr (requires(TDerived d) { d.stateChanged(newState); }) {
            static_cast<TDerived*>(this)->stateChanged(newState);
        }
        if constexpr (storageType == StorageType::ATOMIC) {
            _state.store(newState, std::memory_order_release);
            _state.notify_all();
        } else {
            _state = newState;
        }
    }

    std::string getBlockName() {
        if constexpr (requires(TDerived d) { d.uniqueName(); }) {
            return std::string{static_cast<TDerived*>(this)->uniqueName()};
        } else if constexpr (requires(TDerived d) {
                                 { d.unique_name } -> std::same_as<const std::string&>;
                             }) {
            return std::string{static_cast<TDerived*>(this)->unique_name};
        } else {
            return "unknown block/item";
        }
    }

public:
    explicit StateMachine(State initialState = State::Stopped) noexcept : _state(initialState) {};

    StateMachine(StateMachine&& other) noexcept
    requires(storageType == StorageType::ATOMIC)
        : _state(other._state.load()) {} // atomic, not moving

    StateMachine(StateMachine&& other) noexcept
    requires(storageType != StorageType::ATOMIC)
        : _state(other._state) {} // plain enum

    [[nodiscard]] std::expected<void, gr::Error> changeToolStateTo(State newState, const std::source_location location = std::source_location::current()) {
#if 0 // TODO port to new messaging architecture
        const State oldState = _state;
        if (isValidTransition(oldState, newState)) {
            setAndNotifyState(newState);
            // TODO: remove once message ports are enabled in the UI
            std::println("change {} state from {} to {}", getBlockName(), magic_enum::enum_name(oldState),
                    magic_enum::enum_name(newState));
            return {};
        } else {
            return std::unexpected(gr::Error{
                    std::format("Block '{}' invalid state transition in {} from {} -> to {}", getBlockName(),
                            gr::meta::type_name<TDerived>(), magic_enum::enum_name(toolState()),
                            magic_enum::enum_name(newState)),
                    location });
        }
#else
        return {};
#endif
    }

    [[nodiscard]] State toolState() const noexcept {
        if constexpr (storageType == StorageType::ATOMIC) {
            return _state.load();
        } else {
            return _state;
        }
    }

    void waitOnState(State oldState)
    requires(storageType == StorageType::ATOMIC)
    {
        _state.wait(oldState);
    }

    [[nodiscard]] bool isPauseState(State testState) const noexcept { return testState == Pause; }

    [[nodiscard]] bool isStateDisabled(State testState) const noexcept {
        switch (testState) {
        case PlayStop: return _state != Stopped && !isPauseState(_state);
        case Play: return _state != Stopped && !isPauseState(_state);
        case PlayStream: return _state != Stopped && !isPauseState(_state);
        case Pause: return _state == Stopped || _state == PlayStop;
        case Stopped: return _state == Stopped;
        case Error:
        default: return true;
        }
    }
};
} // namespace play_stop

template<typename T>
struct PlayStopToolbarBlock : public play_stop::StateMachine<PlayStopToolbarBlock<T>>, public gr::Block<PlayStopToolbarBlock<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    using enum play_stop::State;
    gr::MsgPortOut ctrlOut;

    GR_MAKE_REFLECTABLE(PlayStopToolbarBlock, ctrlOut);

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const gr::work::Status status = gr::work::Status::OK; // this->invokeWork(); // calls work(...) -> processOne(...) (all in the same thread as this 'draw()'
        using namespace gr::message;

        handleButton<PlayStop>();
        handleButton<Play>();
        handleButton<PlayStream>();
        handleButton<Pause>();
        handleButton<Stopped>();

        return status;
    }

private:
    template<play_stop::State buttonType>
    inline void handleButton() {
        using namespace gr::message;

        constexpr static auto buttonName = [] {
            switch (buttonType) {
            case PlayStop: return "\uf051";
            case Play: return "\uf04b";
            case PlayStream: return "\uf04e";
            case Pause: return "\uf04c";
            case Stopped: return "\uf04d";
            case Error:
            default: return "Error";
            }
        };
        const float actualButtonSize = 28.f;
        {
            const bool         disabled = this->isStateDisabled(buttonType);
            IMW::Disabled      _(disabled);
            IMW::Font          font(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            IMW::StyleFloatVar style(ImGuiStyleVar_FrameRounding, .5f * actualButtonSize);
            const bool         clicked = ImGui::Button(buttonName(), ImVec2(actualButtonSize, actualButtonSize));
            ImGui::SameLine();
        }
#if 0 // TODO port to new messaging architecture
        if (clicked && !disabled) {
            if (auto e = this->changeToolStateTo(buttonType); e) {
                this->emitMessage(this->ctrlOut, { { key::Kind, kind::SettingsChanged },
                                                         { key::What, std::format("{} pressed", magic_enum::enum_name(buttonType)) } });
            } else {
                this->emitMessage(this->msgOut, { { key::Kind, kind::Error },
                                                        { key::ErrorInfo, e.error().message },
                                                        { key::Location, e.error().srcLoc() } });
            }
        }
#endif
    }
};

template<typename T>
struct LabelToolbarBlock : public gr::Block<LabelToolbarBlock<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    gr::MsgPortIn ctrlIn;
    std::string   message = "<no message>";

    GR_MAKE_REFLECTABLE(LabelToolbarBlock, ctrlIn, message);

    void processMessages(auto&, std::span<const gr::Message> messages) {
        using namespace gr::message;
        for (const gr::Message& msg : messages) {
#if 0 // TODO port to new messaging architecture
            if (msg.contains(key::Kind) && msg.contains(key::What) && std::get<std::string>(msg.at(key::Kind)) == kind::SettingsChanged) {
                this->settings().set({ { std::string("message"), std::get<std::string>(msg.at(key::What)) } });
            }
#endif
        }
    }

    gr::work::Status draw() noexcept {
        this->processScheduledMessages();
        std::ignore                   = this->settings().applyStagedParameters(); // return ignored since there are no tags to be forwarded
        const gr::work::Status status = gr::work::Status::OK;                     // this->invokeWork(); // calls work(...) -> processOne(...) (all in the same thread as this 'draw()'
        ImGui::TextUnformatted(message.c_str());
        return status;
    }
};

} // namespace DigitizerUi

#endif
