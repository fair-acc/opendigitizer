#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP

#include "ImControl.hpp"

#include "../components/StyledStandardWidgets.hpp"
#include "../utils/EmscriptenHelper.hpp"

#include <opendigitizer/video/CameraTypes.hpp>

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
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

/// one of the target settings a control lists in `target_settings_keys`, or T{} where absent
template<typename T>
[[nodiscard]] T targetSetting(const gr::property_map& map, std::string_view key) {
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

/// combobox of `fixed` entries above the target's `available_devices`, showing the entry or device `selected` names (a device: the
/// target's `active_device` where it has one); returns the entry picked, or the "@id:<id>" selector of the device picked
[[nodiscard]] inline std::optional<std::string> pickDevice(const char* id, const gr::property_map& target, std::string_view selected, std::span<const std::string> fixed = {}) {
    const auto                 devices = targetSetting<std::vector<std::string>>(target, "available_devices");
    const auto                 active  = targetSetting<std::string>(target, "active_device");
    const bool                 isFixed = std::ranges::contains(fixed, selected);
    const auto                 isShown = [&](const std::string& device) { return !isFixed && (active.empty() ? opendigitizer::video::deviceSelector(device) == selected : device.starts_with(active + " [")); };
    const auto                 shown   = std::ranges::find_if(devices, isShown);
    const std::string          preview = shown == devices.end() ? std::string(selected) : shown->substr(0UZ, shown->rfind(" ["));
    std::optional<std::string> picked;
    if (auto combo = DigitizerUi::ComboBox(id, preview.c_str(), 0)) {
        for (const std::string& entry : fixed) {
            if (DigitizerUi::Selectable(entry.c_str(), entry == selected)) {
                picked = entry;
            }
        }
        if (!fixed.empty() && !devices.empty()) {
            ImGui::Separator();
        }
        for (const std::string& device : devices) {
            if (DigitizerUi::Selectable(device.c_str(), isShown(device))) {
                picked = opendigitizer::video::deviceSelector(device);
            }
        }
    }
    return picked;
}

struct ImControlDevice : ImControl<ImControlDevice> {
    using Description = gr::Doc<R""(@brief UI control that picks one of the target's `available_devices` (target_map '<block>:device', e.g. an AudioSource) and sends its "@id:<id>" selector.
Entries of `choices` are listed above the devices: picking one sends its `choice_values` entry, which maps "BlockA,BlockB:setting" keys to values;
picking a device also sends `device_values` so, e.g., one list switches a Selector between a simulation and the soundcard. `value` is applied at start.)"">;

    std::string                    value; // entry or device selector last sent
    std::vector<std::string>       choices;
    gr::property_map               choice_values;
    gr::property_map               device_values;
    const std::string              compatible_type_map = "string";
    const std::vector<std::string> target_settings_keys{"available_devices", "active_device"};

    GR_MAKE_REFLECTABLE(ImControlDevice, value, choices, choice_values, device_values, compatible_type_map, target_settings_keys);

    bool _appliedAtStart = false;

    explicit ImControlDevice(gr::property_map initParameters = {}) : ImControl<ImControlDevice>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig = DrawableBlockConfig::fromMap(config);
        const auto&         style        = LookAndFeel::instance().dashboardStyle;
        const SizeAndLayout layout       = prepareDraw(parsedConfig, style.toolbarControlInputWidth, style.toolbarControlMinimumWidth, labelWidth());
        if (choices.empty()) {
            loadValueIfConnectionChanged(value, parsedConfig);
        } else if (!value.empty() && !std::exchange(_appliedAtStart, true)) {
            apply(value);
        }
        drawWithLabelAndTooltip(layout, [&](float width) {
            IMW::Disabled cannotOpen(parsedConfig.drawingContext == DrawableBlockConfig::DrawingContext::FlowgraphView);
            ImGui::SetNextItemWidth(width);
            if (auto picked = pickDevice("##device", parsedConfig.firstControlledProperty.targetSettings, value, choices)) {
                value       = std::move(*picked);
                std::ignore = settings().setStaged({{"value", value}});
                apply(value);
            }
        });
        if (browserTestHooks()) {
            publishForBrowserTests("odDevices", std::string_view(this->name), jsonArray(targetSetting<std::vector<std::string>>(parsedConfig.firstControlledProperty.targetSettings, "available_devices")));
        }
        return gr::work::Status::OK;
    }

private:
    void apply(const std::string& entryOrDevice) {
        if (!std::ranges::contains(choices, entryOrDevice)) {
            sendToTargets(entryOrDevice);
            sendByTarget(device_values);
        } else if (const auto entry = choice_values.find(std::pmr::string(entryOrDevice)); entry != choice_values.end()) {
            if (const auto valuesByTarget = entry->second.get_if<gr::property_map>()) {
                sendByTarget(*valuesByTarget);
            }
        }
    }
};

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP
