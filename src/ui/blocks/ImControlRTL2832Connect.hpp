#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLRTL2832CONNECT_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLRTL2832CONNECT_HPP

#include <cstdint>
#include <format>
#include <string>
#include <utility>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/sdr/RTL2832Device.hpp>

#include "../components/StyledStandardWidgets.hpp"
#include "ImControl.hpp"

namespace DigitizerUi {

struct ImControlRTL2832Connect : ImControl<ImControlRTL2832Connect> {
    using Description = gr::Doc<R""(@brief RTL-SDR connect button: gives the web build access to the RTL-SDR dongle.
A browser opens a USB device only after the user picks it in its USB chooser, right after a click.
Pressing the button opens the chooser; the RTL2832Source then sets up the picked dongle by itself and starts streaming.
`target_map` only shows in the flowgraph editor which source the button belongs to.
Browsers without WebUSB, cannot open the dongle; the native build opens it by itself, so in both the button stays disabled.)"">;

    enum class Connection : std::uint8_t { Native, NoWebUSB, Idle, Connecting, Connected };

    const std::string compatible_type_map = "uint32";

    GR_MAKE_REFLECTABLE(ImControlRTL2832Connect, compatible_type_map);

    explicit ImControlRTL2832Connect(gr::property_map initParameters = {}) : ImControl<ImControlRTL2832Connect>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        auto parsedConfig      = DrawableBlockConfig::fromMap(config);
        parsedConfig.showLabel = false;

#if defined(__EMSCRIPTEN__)
        auto&            usb        = *gr::blocks::common::DeviceRegistry::instance().findAs<gr::blocks::sdr::WebUSBDevice>(gr::blocks::sdr::WebUSBDevice::kId);
        const Connection connection = webConnection(usb);
#else
        const Connection connection = Connection::Native;
#endif
        const std::string   text   = buttonText(connection);
        const float         button = ImGui::CalcTextSize(text.c_str()).x + 2.f * ImGui::GetStyle().FramePadding.x;
        const SizeAndLayout layout = prepareDraw(parsedConfig, button, button, 0.f);

        drawWithLabelAndTooltip(layout, [&](float width) {
            IMW::Disabled busy(connection != Connection::Idle);
            if (DigitizerUi::Button(text.c_str(), ImVec2(width, 0.f))) {
#if defined(__EMSCRIPTEN__)
                usb.requestPermission();
#endif
            }
        });
        return gr::work::Status::OK;
    }

#if defined(__EMSCRIPTEN__)
    [[nodiscard]] static Connection webConnection(const gr::blocks::sdr::WebUSBDevice& usb) {
        if (gr::blocks::sdr::js_rtl_check_usb_api_available() == 0) {
            return Connection::NoWebUSB;
        }
        if (usb.isGranted()) {
            return Connection::Connected;
        }
        return usb.isRequesting() ? Connection::Connecting : Connection::Idle;
    }
#endif

    [[nodiscard]] std::string buttonText(Connection connection) const {
        using enum Connection;
        switch (connection) {
        case Native: return label;
        case NoWebUSB: return std::format("{} needs WebUSB (Chrome)", label);
        case Idle: return std::format("Connect {}", label);
        case Connecting: return std::format("Connecting {}...", label);
        case Connected: return std::format("{} connected", label);
        }
        std::unreachable();
    }
};

} // namespace DigitizerUi

#endif
