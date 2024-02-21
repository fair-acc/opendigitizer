#ifndef OPENDIGITIZER_TOOLBAR_BLOCK_H
#define OPENDIGITIZER_TOOLBAR_BLOCK_H

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS true
#endif

#include "app.h"
#include "imgui.h"

#include <gnuradio-4.0/Block.hpp>

namespace DigitizerUi {

inline bool toolbarButton(const char *label, bool disabled) {
    ImGui::BeginDisabled(disabled);
    ImGui::PushFont(DigitizerUi::App::instance().fontIconsSolid);
    const bool clicked = ImGui::Button(label, ImVec2(28, 28));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::EndDisabled();
    return clicked;
}

template<typename T>
struct GRPlayStopToolbarBlock : public gr::Block<GRPlayStopToolbarBlock<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    using super_t = gr::Block<GRPlayStopToolbarBlock<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">>;

    enum State {
        Initial,
        PlayStop,
        Play,
        Stream,
        Pause
    };

    State m_state = Initial;

    GRPlayStopToolbarBlock() {}

    void playStop() {
        // TODO: needs proper impl
        m_state = PlayStop;
    }

    void play() {
        // TODO: needs proper impl
        m_state = Play;
    }

    void stream() {
        // TODO: needs proper impl
        m_state = Stream;
    }

    void stop() {
        // TODO: needs proper impl
        m_state = Initial;
    }

    void pause() {
        // TODO: needs proper impl
        m_state == Pause;
    }

    gr::work::Status
    draw() noexcept {
        if (DigitizerUi::toolbarButton("\uf051", m_state != Initial)) { // play-stop
            playStop();
        }

        if (DigitizerUi::toolbarButton("\uf04b", isPlayDisabled())) { // play
            play();
        }

        if (DigitizerUi::toolbarButton("\uf04e", isStreamDisabled())) { // forward
            stream();
        }

        if (DigitizerUi::toolbarButton("\uf04c", isPauseDisabled())) { // pause
            pause();
        }

        if (DigitizerUi::toolbarButton("\uf04d", isStopDisabled())) { // stop
            stop();
        }

        return gr::work::Status::DONE;
    }

    bool isStreamDisabled() const { return m_state != Initial && m_state != Pause; }
    bool isPlayDisabled() const { return m_state != Initial && m_state != Pause; }
    bool isStopDisabled() const { return m_state == Initial || m_state == PlayStop; }
    bool isPauseDisabled() const { return m_state == Initial || m_state == PlayStop; }
};

template<typename T>
struct GRLabelToolbarBlock : public gr::Block<GRLabelToolbarBlock<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::Toolbar, "Dear ImGui">> {
    GRLabelToolbarBlock() {}

    void
    processMessages(auto &, std::span<const gr::Message>) {
        //
    }

    gr::work::Status
    draw() noexcept {
        ImGui::Text("Text block");
        return gr::work::Status::DONE;
    }
};

} // namespace DigitizerUi

ENABLE_REFLECTION_FOR_TEMPLATE(DigitizerUi::GRPlayStopToolbarBlock, msgOut)
ENABLE_REFLECTION_FOR_TEMPLATE(DigitizerUi::GRLabelToolbarBlock, msgIn)

#endif
