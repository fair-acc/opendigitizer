#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLTRIGGER_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLTRIGGER_HPP

#include <string>
#include <utility>

#include <gnuradio-4.0/Block.hpp>

#include "ImControl.hpp"

namespace DigitizerUi {

struct ImControlTrigger : ImControl<ImControlTrigger> {
    using Description = gr::Doc<"UI control that sends a boolean true settings set message when pressed, to a block and property determined by target_map">;

    // although for a trigger this value does not change, the type of a UI control is deduced from the property "value"'s type
    // so by specifying this, the UI will constrain connections from triggers to only go to bool/checkbox properties
    const bool value = true;

    GR_MAKE_REFLECTABLE(ImControlTrigger, value);

    explicit ImControlTrigger(gr::property_map initParameters = {}) : ImControl<ImControlTrigger>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const float         button = ImGui::CalcTextSize(label.c_str()).x + 2.f * ImGui::GetStyle().FramePadding.x;
        const SizeAndLayout layout = prepareDraw(config, button, button, 0.f);
        drawInIdScope([&] {
            if (ImGui::Button(label.c_str(), ImVec2(layout.width, 0.f))) {
                sendToTargets(value);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
