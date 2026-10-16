#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLTRIGGER_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLTRIGGER_HPP

#include <string>
#include <utility>

#include <gnuradio-4.0/Block.hpp>

#include "ImControl.hpp"

namespace DigitizerUi {

struct ImControlTrigger : ImControl<ImControlTrigger> {
    using Description = gr::Doc<"UI control that sends a boolean true settings set message when pressed, to a block and property determined by target_map">;

    const std::string compatible_type_map = "bool";

    GR_MAKE_REFLECTABLE(ImControlTrigger, compatible_type_map);

    explicit ImControlTrigger(gr::property_map initParameters = {}) : ImControl<ImControlTrigger>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        auto parsedConfig = DrawableBlockConfig::fromMap(config);

        parsedConfig.showLabel = false; // this is a button, which already shows the label itself

        const float         button = ImGui::CalcTextSize(label.c_str()).x + 2.f * ImGui::GetStyle().FramePadding.x;
        const SizeAndLayout layout = prepareDraw(parsedConfig, button, button, 0.f);
        const ImVec2        buttonSize(layout.width, 0.f);

        drawWithLabelAndTooltip(layout, [this, buttonSize](float) {
            LabelAndFont labelAndFont(*this);
            if (ImGui::Button(labelAndFont.selectedLabel().c_str(), buttonSize)) {
                sendToTargets(true);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
