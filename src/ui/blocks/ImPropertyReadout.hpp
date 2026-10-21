#ifndef OPENDIGITIZER_UI_BLOCKS_IMPROPERTYREADOUT_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMPROPERTYREADOUT_HPP

#include "ImControl.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/formatter/ValueFormatter.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace DigitizerUi {

struct ImPropertyReadout : ImControl<ImPropertyReadout> {
    using Description = gr::Doc<"UI readout that shows the live value of the first block setting identified by target_map">;

    const std::string compatible_type_map = "*";

    GR_MAKE_REFLECTABLE(ImPropertyReadout, compatible_type_map);

    explicit ImPropertyReadout(gr::property_map initParameters = {}) : ImControl<ImPropertyReadout>(std::move(initParameters)) {
        // normally UI control blocks will cause their target properties to be marked as "locked" and only editable by the
        // control, this disables that, since we don't write to the property
        this->meta_information.value.insert_or_assign(std::string_view{"viewonly_ui_control"}, true);
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const SizeAndLayout layout       = prepareDraw(parsedConfig, LookAndFeel::instance().dashboardStyle.toolbarControlInputWidth, LookAndFeel::instance().dashboardStyle.toolbarControlMinimumWidth, labelWidth());
        drawWithLabelAndTooltip(layout, [&](float width) {
            const std::string id   = "##" + label;
            std::string       text = readoutText(parsedConfig);
            ImGui::SetNextItemWidth(width);
            ImGui::InputText(id.c_str(), &text, ImGuiInputTextFlags_ReadOnly);
        });
        return gr::work::Status::OK;
    }

    /// empty while the host reports no controlled property
    [[nodiscard]] static std::string readoutText(const DrawableBlockConfig& config) {
        if (!config.firstControlledProperty) {
            return {};
        }
        return config.firstControlledProperty.value.is_string() ? config.firstControlledProperty.value.value_or(std::string{}) : std::format("{}", config.firstControlledProperty.value);
    }
};

} // namespace DigitizerUi

#endif
