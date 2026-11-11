#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLCAMERA_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLCAMERA_HPP

#include "ImControl.hpp"

#include "../components/StyledStandardWidgets.hpp"
#include "../utils/EmscriptenHelper.hpp"

#include <opendigitizer/video/CameraTypes.hpp>

#include <magic_enum.hpp>

#include <gnuradio-4.0/Block.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <array>
#include <format>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace DigitizerUi {

/// a std::vector<std::string> setting as it arrives in UiGraphBlock::blockSettings: a tensor of string values
[[nodiscard]] inline std::vector<std::string> stringList(const gr::pmt::Value& value) {
    const auto strings = value.get_if<gr::TensorView<gr::pmt::Value>>();
    if (!strings) {
        return {};
    }
    return *strings | std::views::filter(&gr::pmt::Value::is_string) | std::views::transform([](const gr::pmt::Value& v) { return v.value_or(std::string{}); }) | std::ranges::to<std::vector>();
}

struct ImControlCamera : ImControl<ImControlCamera> {
    using Description = gr::Doc<R""(@brief UI control for a CameraSource (target_map '<camera>:device'): selects the camera; its popup sets the mode
priority and the camera controls that both the V4L2 and the browser backend offer, and shows mode, rate, drops and fallback.)"">;

    static constexpr auto kPriorities = magic_enum::enum_names<opendigitizer::video::ModePriority>();

    std::string                    value; // device selector last sent
    const std::string              compatible_type_map = "string";
    const std::vector<std::string> target_settings_keys{"available_devices", "available_controls", "active_device", "fallback_reason", "priority", "controls", "actual_width", "actual_height", "measured_frame_rate", "dropped_frames"};

    GR_MAKE_REFLECTABLE(ImControlCamera, value, compatible_type_map, target_settings_keys);

    explicit ImControlCamera(gr::property_map initParameters = {}) : ImControl<ImControlCamera>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const auto&         look         = LookAndFeel::instance();
        const SizeAndLayout layout       = prepareDraw(parsedConfig, look.dashboardStyle.toolbarControlInputWidth, look.dashboardStyle.toolbarControlMinimumWidth, labelWidth());
        loadValueIfConnectionChanged(value, parsedConfig);
        const gr::property_map& target = parsedConfig.firstControlledProperty.targetSettings;
        drawWithLabelAndTooltip(layout, [&](float width) {
            const bool    insideNodeEditor = parsedConfig.drawingContext == DrawableBlockConfig::DrawingContext::FlowgraphView;
            IMW::Disabled cannotOpen(insideNodeEditor);
            const auto    active = setting<std::string>(target, "active_device");
            ImGui::SetNextItemWidth(width - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
            if (auto combo = DigitizerUi::ComboBox("##device", active.c_str(), 0)) {
                for (const std::string& device : setting<std::vector<std::string>>(target, "available_devices")) {
                    if (DigitizerUi::Selectable(device.c_str(), device.starts_with(active + " ["))) {
                        value       = opendigitizer::video::deviceSelector(device);
                        std::ignore = settings().setStaged({{"value", value}});
                        sendToTargets(value);
                    }
                }
            }
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("...##cameraSettings", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
                ImGui::OpenPopup("cameraSettings");
            }
            drawSettingsPopup(target, parsedConfig.firstControlledProperty.targetBlockName);
        });
        publishForBrowserTests(target);
        return gr::work::Status::OK;
    }

private:
    template<typename T>
    [[nodiscard]] static T setting(const gr::property_map& map, std::string_view key) {
        const auto found = map.find_value(key);
        if (!found) {
            return {};
        }
        if constexpr (std::same_as<T, std::string>) {
            return found->value_or(std::string{});
        } else if constexpr (std::same_as<T, std::vector<std::string>>) {
            return stringList(*found);
        } else {
            return gr::pmt::convert_safely<T>(*found).value_or(T{});
        }
    }

    /// browser tests (globalThis.odTestHooks) read the camera's controls and devices from globalThis.odCameraControls/odCameraDevices[<control name>]
    void publishForBrowserTests([[maybe_unused]] const gr::property_map& target) const {
#if defined(__EMSCRIPTEN__)
        if (!browserTestHooks()) {
            return;
        }
        const std::string controlName{std::string_view(this->name)};
        std::string       names;
        for (const auto& [key, _] : setting<gr::property_map>(target, "available_controls")) {
            names += std::format("{},", std::string_view(key));
        }
        std::string devices;
        for (const std::string& device : setting<std::vector<std::string>>(target, "available_devices")) {
            devices += device + '\n';
        }
        // clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
        EM_ASM({ ((name, controls, devices) => { (globalThis.odCameraControls = globalThis.odCameraControls || {})[UTF8ToString(name)] = UTF8ToString(controls).split(',').filter(Boolean); (globalThis.odCameraDevices = globalThis.odCameraDevices || {})[UTF8ToString(name)] = UTF8ToString(devices).split('\n').filter(Boolean); })($0, $1, $2); }, controlName.c_str(), names.c_str(), devices.c_str());
#pragma clang diagnostic pop
        // clang-format on
#endif
    }

    void sendToCamera(const std::string& camera, gr::property_map settings) {
        if (!camera.empty()) {
            toolbar::sendSettings(this->msgOut, camera, std::move(settings));
        }
    }

    void drawSettingsPopup(const gr::property_map& target, const std::string& camera) {
        auto popup = IMW::Popup("cameraSettings", 0);
        if (!popup) {
            return;
        }
        const auto& look = LookAndFeel::instance();
        ImGui::Text("%s", std::format("{}x{} @ {:.1f} fps, {} dropped", setting<gr::Size_t>(target, "actual_width"), setting<gr::Size_t>(target, "actual_height"), setting<float>(target, "measured_frame_rate"), setting<gr::Size_t>(target, "dropped_frames")).c_str());
        if (const auto reason = setting<std::string>(target, "fallback_reason"); !reason.empty()) {
            ImGui::TextColored(look.palette().warningColor, "%s", std::format("test card: {}", reason).c_str());
        }
        const auto priority = setting<std::string>(target, "priority");
        ImGui::SetNextItemWidth(look.chartStyle.menuSettingWidgetWidth);
        if (auto combo = DigitizerUi::ComboBox("priority", priority.c_str(), 0)) {
            for (const std::string_view choice : kPriorities) {
                if (DigitizerUi::Selectable(std::string(choice).c_str(), choice == priority)) {
                    sendToCamera(camera, {{"priority", std::string(choice)}});
                }
            }
        }
        const auto available = setting<gr::property_map>(target, "available_controls");
        auto       requested = setting<gr::property_map>(target, "controls");
        for (const std::string_view controlName : opendigitizer::video::kCommonControls) {
            const auto control = setting<gr::property_map>(available, controlName);
            if (control.empty()) {
                continue; // not offered by the active camera
            }
            const std::string controlLabel(controlName);
            auto              current = static_cast<float>(requested.contains(controlName) ? setting<double>(requested, controlName) : setting<double>(control, "value"));
            bool              changed = false;
            if (setting<std::string>(control, "kind") == "boolean") {
                bool on = current != 0.f;
                changed = ImGui::Checkbox(controlLabel.c_str(), &on);
                current = on ? 1.f : 0.f;
            } else {
                ImGui::SetNextItemWidth(look.chartStyle.menuSettingWidgetWidth);
                changed = ImGui::SliderFloat(controlLabel.c_str(), &current, static_cast<float>(setting<double>(control, "min")), static_cast<float>(setting<double>(control, "max")), "%.0f");
            }
            if (changed) {
                requested.insert_or_assign(controlLabel, static_cast<double>(current));
                sendToCamera(camera, {{"controls", requested}});
            }
        }
    }
};

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_BLOCKS_IMCONTROLCAMERA_HPP
