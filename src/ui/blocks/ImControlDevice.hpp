#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP

#include "ImControl.hpp"

#include "../components/StyledWidgets.hpp"
#include "../utils/Browser.hpp"

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

namespace gr::od {

/// a std::vector<std::string> setting as it arrives in UiGraphBlock::blockSettings: a tensor of string values
[[nodiscard]] inline std::vector<std::string> stringList(const gr::pmt::Value& value) { return gr::pmt::convertTo_or<std::vector<std::string>>(value, std::vector<std::string>{}); }

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

/// a device of `available_devices` without its " [id]" suffix
[[nodiscard]] inline std::string_view deviceName(std::string_view device) { return device.substr(0UZ, device.rfind(" [")); }

/// whether `device` is the one `selected` names, or the target's `active_device` where it has one
[[nodiscard]] inline bool isSelectedDevice(std::string_view device, std::string_view active, std::string_view selected) { return active.empty() ? opendigitizer::video::deviceSelector(device) == selected : device.starts_with(std::string(active) + " ["); }

/// width at which pickDevice() shows any of its values without truncating it: the `fixed` entries, the device names, or `selected` while it names none of them
[[nodiscard]] inline float pickDeviceWidth(const gr::property_map& target, std::string_view selected, std::span<const std::string> fixed = {}) {
    const auto devices     = targetSetting<std::vector<std::string>>(target, "available_devices");
    const auto active      = targetSetting<std::string>(target, "active_device");
    const auto textWidth   = [](std::string_view text) { return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x; };
    const bool isListed    = std::ranges::contains(fixed, selected) || std::ranges::any_of(devices, [&](const std::string& device) { return isSelectedDevice(device, active, selected); });
    const auto widestOf    = [&](const auto& texts, float widest) { return std::ranges::fold_left(texts, widest, [&](float width, std::string_view text) { return std::max(width, textWidth(text)); }); };
    const auto deviceNames = devices | std::views::transform([](const std::string& device) { return deviceName(device); });
    return comboWidthForText(widestOf(deviceNames, widestOf(fixed, isListed ? 0.f : textWidth(selected))));
}

/// combobox of `fixed` entries above the target's `available_devices`, showing the entry or device `selected` names (a device: the
/// target's `active_device` where it has one); returns the entry picked, or the "@id:<id>" selector of the device picked
[[nodiscard]] inline std::optional<std::string> pickDevice(const char* id, const gr::property_map& target, std::string_view selected, std::span<const std::string> fixed = {}) {
    const auto                 devices = targetSetting<std::vector<std::string>>(target, "available_devices");
    const auto                 active  = targetSetting<std::string>(target, "active_device");
    const bool                 isFixed = std::ranges::contains(fixed, selected);
    const auto                 isShown = [&](const std::string& device) { return !isFixed && isSelectedDevice(device, active, selected); };
    const auto                 shown   = std::ranges::find_if(devices, isShown);
    const std::string          preview = shown == devices.end() ? std::string(selected) : std::string(deviceName(*shown));
    std::optional<std::string> picked;
    if (auto combo = ComboBox(id, preview.c_str(), 0)) {
        for (const std::string& entry : fixed) {
            if (selectable(entry.c_str(), entry == selected)) {
                picked = entry;
            }
        }
        if (!fixed.empty() && !devices.empty()) {
            ImGui::Separator();
        }
        for (const std::string& device : devices) {
            if (selectable(device.c_str(), isShown(device))) {
                picked = opendigitizer::video::deviceSelector(device);
            }
        }
    }
    return picked;
}

struct ImControlDevice : ImControl<ImControlDevice> {
    using Description = gr::Doc<R""(@brief UI control that picks one of the target's `available_devices` (target_map '<block>:device', e.g. an AudioSource) and sends its "@id:<id>" selector.
Entries of `choices` are listed above the devices: picking one sends its `choice_values` entry, which maps "BlockA,BlockB:setting" keys to values;
picking a device also sends `device_values` so, e.g., one list switches a Selector between a simulation and the soundcard. `value` is applied at start.
`fallback`, one of `choices`, is applied and shown while the picked device's target reports a `device_state` other than "open"; the device's `device_values` return with "open".)"">;

    std::string                    value; // entry or device selector last sent
    std::vector<std::string>       choices;
    gr::property_map               choice_values;
    gr::property_map               device_values;
    std::string                    fallback;
    const std::string              compatible_type_map = "string";
    const std::vector<std::string> target_settings_keys{"available_devices", "active_device", "device_state"};

    GR_MAKE_REFLECTABLE(ImControlDevice, value, choices, choice_values, device_values, fallback, compatible_type_map, target_settings_keys);

    bool _appliedAtStart = false;
    bool _fallenBack     = false;

    explicit ImControlDevice(gr::property_map initParameters = {}) : ImControl<ImControlDevice>(std::move(initParameters)) {}

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto  parsedConfig = DrawableBlockConfig::fromMap(config);
        const auto& target       = parsedConfig.firstControlledProperty.targetSettings;
        const auto& style        = LookAndFeel::instance().dashboardStyle;
        if (choices.empty()) {
            loadValueIfConnectionChanged(value, parsedConfig);
        } else if (!value.empty() && !std::exchange(_appliedAtStart, true)) {
            apply(value);
        }
        followDeviceState(targetSetting<std::string>(target, "device_state"));
        const std::string&  shown      = _fallenBack ? fallback : value;
        const float         inputWidth = std::max(style.toolbarControlMinimumWidth, pickDeviceWidth(target, shown, choices));
        const SizeAndLayout layout     = prepareDraw(parsedConfig, inputWidth, style.toolbarControlMinimumWidth, labelWidth());
        drawWithLabel(layout, [&](float width) {
            imw::Disabled cannotOpen(parsedConfig.drawingContext == DrawableBlockConfig::DrawingContext::FlowgraphView);
            ImGui::SetNextItemWidth(width);
            if (auto picked = pickDevice("##device", target, shown, choices)) {
                value       = std::move(*picked);
                _fallenBack = false;
                stageValue(value);
                apply(value);
            }
        });
        if (browserTestHooks()) {
            publishForBrowserTests("odDevices", std::string_view(this->name), jsonArray(targetSetting<std::vector<std::string>>(target, "available_devices")));
        }
        return gr::work::Status::OK;
    }

    void followDeviceState(std::string_view state) {
        if (state.empty() || !std::ranges::contains(choices, fallback) || std::ranges::contains(choices, value)) {
            return;
        }
        if (state != "open" && !_fallenBack) {
            _fallenBack = true;
            apply(fallback);
        } else if (state == "open" && std::exchange(_fallenBack, false)) {
            sendByTarget(device_values);
        }
    }

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

} // namespace gr::od

#endif // OPENDIGITIZER_UI_BLOCKS_IMCONTROLDEVICE_HPP
