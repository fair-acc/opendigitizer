#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLCHOICE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLCHOICE_HPP

#include "ImControl.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <string>
#include <utility>
#include <vector>

namespace DigitizerUi {

struct ImControlChoice : ImControl<ImControlChoice> {
    using Description = gr::Doc<"UI control that picks one of the enum values of the controlled property and sets it on the block settings determined by target_map">;

    std::string       value;
    const std::string compatible_type_map = "enum";

    GR_MAKE_REFLECTABLE(ImControlChoice, value, compatible_type_map);

    explicit ImControlChoice(gr::property_map initParameters = {}) : ImControl<ImControlChoice>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const SizeAndLayout layout       = prepareDraw(parsedConfig, control::kImControlTextInputWidth, control::kImControlMinimumWidth, labelWidth());
        loadValueIfConnectionChanged(value, parsedConfig);
        drawLabelled(layout, [&](float width) {
            const std::string id = "##" + label;
            IMW::Disabled     nothingToChoose(parsedConfig.aggregateControlledPropertyHints.choices.empty());
            ImGui::SetNextItemWidth(width);
            if (auto combo = IMW::Combo(id.c_str(), value.c_str(), 0)) {
                for (const std::string& choice : parsedConfig.aggregateControlledPropertyHints.choices) {
                    if (ImGui::Selectable(choice.c_str(), choice == value)) {
                        value       = choice;
                        std::ignore = settings().setStaged({{"value", value}});
                        sendToTargets(value);
                    }
                }
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
