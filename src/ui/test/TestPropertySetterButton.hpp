#ifndef OPENDIGITIZER_UI_TEST_TEST_PROPERTY_SETTER_BUTTON_HPP
#define OPENDIGITIZER_UI_TEST_TEST_PROPERTY_SETTER_BUTTON_HPP

#include "../blocks/ToolbarBlock.hpp"

namespace gr::od::test {

struct TestPropertySetterButton : gr::Block<TestPropertySetterButton, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    // not a public block and without target_map: there is no UX for setting a property_map property, nor for connecting to a block without choosing one of its properties
    using Description = gr::Doc<"UI control button that sends its property_map payload as settings to the target block when pressed">;

    std::string      label = "button";
    std::string      icon;
    std::string      tooltip;
    std::string      target_block;
    gr::property_map payload; // settings applied to the target block, e.g. {amplitude: 2.0}

    GR_MAKE_REFLECTABLE(TestPropertySetterButton, label, icon, tooltip, target_block, payload);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    gr::work::Status draw(const gr::property_map& = {}) noexcept {
        using namespace toolbar;
        if (drawItem(unique_name, label, icon, tooltip, [](const char* text) { return ImGui::Button(text); })) {
            sendSettings(msgOut, target_block, payload);
        }
        return gr::work::Status::OK;
    }
};
} // namespace gr::od::test

#endif
