#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLCAMERA_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLCAMERA_HPP

#include "ImControlDevice.hpp"

#include "../components/StyledStandardWidgets.hpp"
#include "../utils/EmscriptenHelper.hpp"

#include <opendigitizer/video/CameraTypes.hpp>

#include <gnuradio-4.0/Block.hpp>

#include <array>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace DigitizerUi {

struct ImControlCamera : ImControl<ImControlCamera> {
    using Description = gr::Doc<R""(@brief UI control for a CameraSource (target_map '<camera>:device'): selects the camera; its popup sets the mode
priority and the camera controls that both the V4L2 and the browser backend offer, and shows mode, rate, drops and fallback.)"">;

    std::string                    value; // device selector last sent
    const std::string              compatible_type_map = "string";
    const std::vector<std::string> target_settings_keys{"available_devices", "available_controls", "active_device", "fallback_reason", "priority", "controls", "actual_width", "actual_height", "measured_frame_rate", "dropped_frames"};

    GR_MAKE_REFLECTABLE(ImControlCamera, value, compatible_type_map, target_settings_keys);

    std::optional<std::pair<std::string, float>> _heldSlider; // control name and value while its slider is dragged

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
            ImGui::SetNextItemWidth(width - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
            if (auto picked = pickDevice("##device", target, value)) {
                value       = std::move(*picked);
                std::ignore = settings().setStaged({{"value", value}});
                sendToTargets(value);
            }
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            if (ImGui::Button("...##cameraSettings", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()))) {
                ImGui::OpenPopup("cameraSettings");
            }
            drawSettingsPopup(target, parsedConfig.firstControlledProperty.targetBlockName);
        });
        if (browserTestHooks()) {
            const std::string_view controlName(this->name);
            const auto             available = targetSetting<gr::property_map>(target, "available_controls");
            publishForBrowserTests("odCameraControls", controlName, jsonArray(available.keys()));
            publishForBrowserTests("odCameraDevices", controlName, jsonArray(targetSetting<std::vector<std::string>>(target, "available_devices")));
        }
        return gr::work::Status::OK;
    }

private:
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
        ImGui::Text("%s", std::format("{}x{} @ {:.1f} fps, {} dropped", targetSetting<gr::Size_t>(target, "actual_width"), targetSetting<gr::Size_t>(target, "actual_height"), targetSetting<float>(target, "measured_frame_rate"), targetSetting<gr::Size_t>(target, "dropped_frames")).c_str());
        if (const auto reason = targetSetting<std::string>(target, "fallback_reason"); !reason.empty()) {
            ImGui::TextColored(look.palette().warningColor, "%s", std::format("test card: {}", reason).c_str());
        }
        const auto priority = targetSetting<std::string>(target, "priority");
        ImGui::SetNextItemWidth(look.chartStyle.menuSettingWidgetWidth);
        if (auto combo = DigitizerUi::ComboBox("priority", priority.c_str(), 0)) {
            for (const opendigitizer::video::ModePriority choice : gr::meta::enumValues<opendigitizer::video::ModePriority>()) {
                const std::string choiceName(gr::meta::enumName(choice).value_or(""));
                if (DigitizerUi::Selectable(choiceName.c_str(), choiceName == priority)) {
                    sendToCamera(camera, {{"priority", choiceName}});
                }
            }
        }
        const auto available = targetSetting<gr::property_map>(target, "available_controls");
        auto       requested = targetSetting<gr::property_map>(target, "controls");
        for (const std::string_view controlName : opendigitizer::video::kCommonControls) {
            const auto control = targetSetting<gr::property_map>(available, controlName);
            if (control.empty()) {
                continue;
            }
            const std::string controlLabel(controlName);
            const bool        held     = _heldSlider.has_value() && _heldSlider->first == controlLabel;
            const auto        reported = static_cast<float>(requested.contains(controlName) ? targetSetting<double>(requested, controlName) : targetSetting<double>(control, "value"));
            auto              current  = held ? _heldSlider->second : reported;
            bool              changed  = false;
            if (targetSetting<std::string>(control, "kind") == "boolean") {
                bool on = current != 0.f;
                changed = ImGui::Checkbox(controlLabel.c_str(), &on);
                current = on ? 1.f : 0.f;
            } else {
                ImGui::SetNextItemWidth(look.chartStyle.menuSettingWidgetWidth);
                ImGui::SliderFloat(controlLabel.c_str(), &current, static_cast<float>(targetSetting<double>(control, "min")), static_cast<float>(targetSetting<double>(control, "max")), "%.0f");
                if (ImGui::IsItemActive()) {
                    _heldSlider.emplace(controlLabel, current);
                } else if (held) {
                    _heldSlider.reset();
                }
                changed = ImGui::IsItemDeactivatedAfterEdit();
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
