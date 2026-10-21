#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLTOGGLE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLTOGGLE_HPP

#include "ImControl.hpp"

#include "../components/StyledStandardWidgets.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <string>
#include <utility>

namespace DigitizerUi {

struct ImControlToggle : ImControl<ImControlToggle> {
    using Description = gr::Doc<"UI control that edits a boolean and sets it on the block and property determined by target_map">;

    bool              value               = false;
    const std::string compatible_type_map = "bool";

    GR_MAKE_REFLECTABLE(ImControlToggle, value, compatible_type_map);

    explicit ImControlToggle(gr::property_map initParameters = {}) : ImControl<ImControlToggle>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const float         checkboxSize = ImGui::GetFrameHeight();
        const SizeAndLayout layout       = prepareDraw(parsedConfig, checkboxSize, checkboxSize, labelWidth());
        loadValueIfConnectionChanged(value, parsedConfig);
        drawWithLabelAndTooltip(layout, [this](float) {
            const std::string id = "##" + label;
            if (DigitizerUi::Checkbox(id.c_str(), &value)) {
                std::ignore = settings().setStaged({{"value", value}});
                sendToTargets(value);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
