#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLCOLOR_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLCOLOR_HPP

#include "../common/LookAndFeel.hpp"
#include "ImControl.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <utility>

namespace DigitizerUi {

namespace control {
[[nodiscard]] inline std::string colorTypeMap() {
    return std::format("{};{}", kColorUnits | std::views::transform([](std::string_view unit) { return std::format("unit:{}", unit); }) | std::views::join_with(';') | std::ranges::to<std::string>(), gr::meta::type_name<std::uint32_t>());
}
} // namespace control

struct ImControlColor : ImControl<ImControlColor> {
    using Description = gr::Doc<"UI control that picks a color and sends it to connected color properties">;

    gr::Annotated<std::uint32_t, "value", gr::Unit<"Color">> value               = 0U;
    const std::string                                        compatible_type_map = control::colorTypeMap();

    GR_MAKE_REFLECTABLE(ImControlColor, value, compatible_type_map);

    explicit ImControlColor(gr::property_map initParameters = {}) : ImControl<ImControlColor>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const float         swatchSize   = ImGui::GetFrameHeight();
        const SizeAndLayout layout       = prepareDraw(parsedConfig, swatchSize, swatchSize, labelWidth());
        loadValueIfConnectionChanged(value.value, parsedConfig);
        drawWithLabelAndTooltip(layout, [this, &parsedConfig](float) {
            const std::string id    = "##" + label;
            const ImVec4      color = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(value.value));
            std::array        rgb{color.x, color.y, color.z};
            // see ImControl tooltips and ImControlChoice for why we need to disable widgets that have popups while in the flowgraph
            IMW::Disabled cannotOpenPicker(parsedConfig.drawingContext == DrawableBlockConfig::DrawingContext::FlowgraphView);
            if (ImGui::ColorEdit3(id.c_str(), rgb.data(), ImGuiColorEditFlags_NoInputs)) {
                value.value = float4ToRGBA(ImVec4{rgb[0], rgb[1], rgb[2], 1.f}) >> 8;
                std::ignore = settings().setStaged({{"value", value.value}});
                sendToTargets(value.value);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
