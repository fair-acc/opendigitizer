#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLTOGGLE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLTOGGLE_HPP

#include "ImControl.hpp"

#include "../components/StyledWidgets.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <string>
#include <utility>

namespace gr::od {

struct ImControlToggle : ImControl<ImControlToggle> {
    using Description = gr::Doc<"UI control that edits a boolean and sets it on the block and property determined by target_map">;

    bool              value               = false;
    const std::string compatible_type_map = "bool";

    GR_MAKE_REFLECTABLE(ImControlToggle, value, compatible_type_map);

    explicit ImControlToggle(gr::property_map initParameters = {}) : ImControl<ImControlToggle>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const float         toggleWidth  = toggleSwitchWidth();
        const SizeAndLayout layout       = prepareDraw(parsedConfig, toggleWidth, toggleWidth, labelWidth());
        loadValueIfConnectionChanged(value, parsedConfig);
        drawWithLabel(layout, [this](float) {
            const std::string id = widgetId();
            if (checkbox(id.c_str(), &value)) {
                commitValue(value);
            }
        });
        return gr::work::Status::OK;
    }
};

} // namespace gr::od

#endif
