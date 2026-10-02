#ifndef OPENDIGITIZER_STATUSBAR_BLOCK_H
#define OPENDIGITIZER_STATUSBAR_BLOCK_H

#include <format>
#include <string>

#include <gnuradio-4.0/Block.hpp>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

namespace DigitizerUi {

struct SchedulerStateIndicator : gr::Block<SchedulerStateIndicator, gr::Drawable<gr::UICategory::StatusBar, "Dear ImGui">> {
    using Description = gr::Doc<"status-bar label with the lifecycle state of its scheduler (a block's state follows its scheduler's)">;

    GR_MAKE_REFLECTABLE(SchedulerStateIndicator);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        using enum gr::lifecycle::State;
        const gr::lifecycle::State state  = this->state();
        const ImVec4               colour = [state] {
            switch (state) {
            case RUNNING: return ImVec4(.2f, .75f, .2f, 1.f);
            case REQUESTED_PAUSE:
            case PAUSED: return ImVec4(1.f, .65f, 0.f, 1.f);
            case ERROR: return LookAndFeel::instance().palette().errorColor;
            case IDLE:
            case INITIALISED:
            case REQUESTED_STOP:
            case STOPPED: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
            return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }();
        // a selectable rather than plain text, so that the shown state is an item hosts and tests can find
        const std::string label = std::format("{}###schedulerState", gr::meta::enumName(state).value_or("?"));
        IMW::StyleColor   textColour(ImGuiCol_Text, colour);
        std::ignore = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::CalcTextSize(label.c_str(), nullptr, true).x, 0.f));
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
