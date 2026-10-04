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

    [[nodiscard]] std::expected<void, gr::Error> changeToolStateTo([[maybe_unused]] State newState, [[maybe_unused]] const std::source_location location = std::source_location::current()) {
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
struct PlayStopToolbarBlock : public play_stop::StateMachine<PlayStopToolbarBlock<T>>, public gr::Block<PlayStopToolbarBlock<T>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    using enum play_stop::State;
    gr::MsgPortOut ctrlOut;

    GR_MAKE_REFLECTABLE(PlayStopToolbarBlock, ctrlOut);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw([[maybe_unused]] const gr::property_map& config = {}) noexcept {
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
            const bool                  disabled = this->isStateDisabled(buttonType);
            IMW::Disabled               _(disabled);
            IMW::Font                   font(DigitizerUi::LookAndFeel::instance().fontIconsSolid);
            IMW::StyleFloatVar          style(ImGuiStyleVar_FrameRounding, .5f * actualButtonSize);
            [[maybe_unused]] const bool clicked = ImGui::Button(buttonName(), ImVec2(actualButtonSize, actualButtonSize));
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
struct LabelToolbarBlock : public gr::Block<LabelToolbarBlock<T>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    gr::MsgPortIn ctrlIn;
    std::string   message = "<no message>";

    GR_MAKE_REFLECTABLE(LabelToolbarBlock, ctrlIn, message);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void processMessages(auto&, std::span<const gr::Message> messages) {
        using namespace gr::message;
        for ([[maybe_unused]] const gr::Message& msg : messages) {
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

namespace toolbar {
// shows the icon glyph in the icon font when set, the label otherwise; ids are scoped by the block's unique name
[[nodiscard]] inline bool drawItem(const std::string& uniqueName, const std::string& label, const std::string& icon, const std::string& tooltip, auto widget) {
    IMW::ChangeStrId id(uniqueName.c_str());
    bool             changed = false;
    if (icon.empty()) {
        changed = widget(label.c_str());
    } else {
        IMW::Font font(LookAndFeel::instance().fontIconsSolid);
        changed = widget(icon.c_str());
    }
    if (!tooltip.empty()) {
        ImGui::SetItemTooltip("%s", tooltip.c_str());
    }
    return changed;
}

inline void sendSettings(gr::MsgPortOutBuiltin& port, const std::string& targetBlock, gr::property_map settings) {
    if (targetBlock.empty()) { // an empty service name would address every block
        return;
    }
    gr::sendMessage<gr::message::Command::Set>(port, targetBlock, gr::block::property::kSetting, std::move(settings));
}
} // namespace toolbar

struct ToolbarButton : gr::Block<ToolbarButton, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    using Description = gr::Doc<"toolbar button that sends its payload as settings to the target block when pressed">;

    std::string      label = "button";
    std::string      icon; // icon-font glyph, shown instead of the label
    std::string      tooltip;
    std::string      target_block; // unique name or name of the receiving block
    gr::property_map payload;      // settings applied to the target block, e.g. {amplitude: 2.0}

    GR_MAKE_REFLECTABLE(ToolbarButton, label, icon, tooltip, target_block, payload);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        if (toolbar::drawItem(unique_name, label, icon, tooltip, [](const char* text) { return ImGui::Button(text); })) {
            toolbar::sendSettings(msgOut, target_block, payload);
        }
        return gr::work::Status::OK;
    }
};

struct ToolbarCheckbox : gr::Block<ToolbarCheckbox, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    using Description = gr::Doc<"toolbar checkbox that sets a boolean setting of the target block to its checked state">;

    std::string label = "checkbox";
    std::string icon; // icon-font glyph, shown instead of the label
    std::string tooltip;
    std::string target_block;   // unique name or name of the receiving block
    std::string target_setting; // boolean setting of the target block
    bool        checked = false;

    GR_MAKE_REFLECTABLE(ToolbarCheckbox, label, icon, tooltip, target_block, target_setting, checked);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        if (toolbar::drawItem(unique_name, label, icon, tooltip, [this](const char* text) { return ImGui::Checkbox(text, &checked); })) {
            toolbar::sendSettings(msgOut, target_block, gr::property_map{{std::pmr::string(target_setting), checked}});
        }
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
