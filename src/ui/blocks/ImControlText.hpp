#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLTEXT_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLTEXT_HPP

#include "ImControl.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <string>
#include <utility>

namespace DigitizerUi {

struct ImControlText : ImControl<ImControlText> {
    using Description = gr::Doc<"UI control that edits a string and sets it on the block settings determined by target_map">;

    std::string value;

    GR_MAKE_REFLECTABLE(ImControlText, value);

    explicit ImControlText(gr::property_map initParameters = {}) : ImControl<ImControlText>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const SizeAndLayout layout = prepareDraw(config, control::kImControlTextInputWidth, control::kImControlMinimumWidth, labelWidth());
        drawLabelled(layout, [this](float width) {
            const std::string id = "##" + label;
            ImGui::SetNextItemWidth(width);
            const bool entered = ImGui::InputText(id.c_str(), &value, ImGuiInputTextFlags_EnterReturnsTrue);
            if (entered || ImGui::IsItemDeactivatedAfterEdit()) { // the target follows the entered text, not every keystroke
                std::ignore = settings().setStaged({{"value", value}});
                sendToTargets(value);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace DigitizerUi

#endif
