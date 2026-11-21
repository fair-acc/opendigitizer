#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP

#include "../common/Imw.hpp"
#include "DrawableBlockConfig.hpp"
#include "TargetMap.hpp"
#include "ToolbarBlock.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/formatter/ValueFormatter.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace gr::od {

// CRTP base of a toolbar block drawn with ImGui: common utilities for UI control by messages to other blocks
template<typename TDerived>
struct ImControl : gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    using DrawableBlock = gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">>;

    // how large a control is (its place in the toolbar) and how its parts are laid out within that area
    struct SizeAndLayout {
        float width            = 0.f;
        bool  showLabel        = true;
        bool  hasAnyConnection = true;
    };

    struct LabelAndFont {
        ImControl*               _self;
        std::optional<imw::Font> _font;

        explicit LabelAndFont(ImControl& self) : _self(std::addressof(self)) {
            if (shouldUseIcon()) {
                _font.emplace(LookAndFeel::instance().fontIconsSolid);
            }
        }
        [[nodiscard]] bool               shouldUseIcon() const { return !_self->icon.empty(); }
        [[nodiscard]] const std::string& selectedLabel() { return shouldUseIcon() ? _self->icon : _self->label; }
    };

    std::string target_map; // of the form "BlockA,BlockB:property;BlockC:property;*:other_property"
    std::string label = "value";
    std::string icon;
    std::string tooltip;

    GR_MAKE_REFLECTABLE(ImControl, target_map, label, icon, tooltip);

    // used to track whether the property we are connected to has changed, to invalidate cache
    std::string _lastDrawnTarget;

    explicit ImControl(gr::property_map initParameters = {}) : DrawableBlock(std::move(initParameters)) {}

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (!newSettings.contains("target_map")) {
            return;
        }
        const auto targets = TargetMap::fromString(target_map);
        if (!targets) {
            gr::log::warning("{}: invalid target_map '{}': {}", this->unique_name, target_map, targets.error());
        } else if (std::ranges::any_of(targets->entries(), &TargetEntry::isGlob)) {
            gr::log::warning("{}: target_map '{}' tries to use glob-selector '*' but that is not implemented yet", this->unique_name, target_map);
        }
    }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    [[nodiscard]] float labelWidth() const { return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x; }

    /// at the start of draw(): applies changed settings, pushes the width constraints onto the ImGui stack and returns the layout for `config` (the toolbar's constraints) and this widget's constraints;
    /// `labelPart` 0 for a widget without a separate label, such as a button
    [[nodiscard]] SizeAndLayout prepareDraw(const DrawableBlockConfig& config, float naturalWidget, float minimumWidget, float labelPart) {
        this->applyChangedSettings(); // no work() applies them
        const float maxWidth  = config.maxWidth.value_or(std::numeric_limits<float>::max());
        const bool  showLabel = config.showLabel.value_or(true);
        toolbar::publishWidths(this->unique_name, {.natural = naturalWidget + labelPart, .minimum = minimumWidget + labelPart, .label = labelPart});
        return {.width = std::clamp(maxWidth - (showLabel ? labelPart : 0.f), minimumWidget, naturalWidget), .showLabel = showLabel, .hasAnyConnection = config.firstControlledProperty.has_value()};
    }

    /// loads the controlled property's value into `value` when `config` names a different target: controls load the value of a target when connected and unload it when disconnected
    template<typename TValue>
    void loadValueIfConnectionChanged(TValue& value, const DrawableBlockConfig& config) {
        const DrawableBlockConfig::ControlledProperty& controlledProperty = config.firstControlledProperty;
        auto                                           currentDrawnTarget = std::format("{}:{}", controlledProperty.targetBlockName, controlledProperty.targetPropertyName);
        if (currentDrawnTarget == _lastDrawnTarget) {
            return;
        }
        _lastDrawnTarget = std::move(currentDrawnTarget);

        if (!controlledProperty) {
            value = TValue{};
        } else if (const auto converted = tryConvertValue<TValue>(controlledProperty.value)) {
            value = *converted;
        } else {
            return;
        }
        stageValue(value);
    }

    template<typename TValue>
    [[nodiscard]] static std::optional<TValue> tryConvertValue(const gr::pmt::Value& controlledValue) {
        if constexpr (std::same_as<TValue, std::string>) { // deal with string vs. string view not converting with convert_safely
            return controlledValue.is_string() ? std::optional<TValue>(controlledValue.value_or(std::string{})) : std::nullopt;
        } else {
            const auto converted = gr::pmt::convert_safely<TValue>(controlledValue);
            if (converted) {
                return *converted;
            }

            // use static_cast to convert something that fails regular conversion for being out of range. this will clamp it into the range of TValue
            std::optional<TValue> roundedInteger;
            if constexpr (std::floating_point<TValue>) {
                gr::pmt::ValueVisitor([&roundedInteger]<typename TFrom>(const TFrom& from) {
                    if constexpr (std::integral<TFrom> && !std::same_as<TFrom, bool>) {
                        roundedInteger = static_cast<TValue>(from);
                    }
                }).visit(controlledValue);
            }
            return roundedInteger;
        }
    }

    void drawWithLabel(const SizeAndLayout& layout, auto drawWidget) {
        imw::ChangeStrId scope(this->unique_name.value().c_str());

        {
            LabelAndFont labelAndFont(*this);
            if (layout.showLabel && !labelAndFont.selectedLabel().empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(labelAndFont.selectedLabel().c_str());
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            }
        }

        {
            imw::Group    widgets;
            imw::Disabled disconnected(!layout.hasAnyConnection);
            drawWidget(layout.width);
        }
    }

    void sendToTargets(const auto& payload) { toolbar::sendToTargets(this->msgOut, target_map, payload); } // an ill-formed map is reported during settingsChanged()

    void stageValue(const auto& value) { std::ignore = this->settings().setStaged({{"value", value}}); }
    void commitValue(const auto& value) {
        stageValue(value);
        sendToTargets(value);
    }
    [[nodiscard]] std::string widgetId() const { return "##" + label; }

    /// sends each "BlockA,BlockB:property" key's value, every block's settings in one message
    void sendByTarget(const gr::property_map& valuesByTarget) {
        std::map<std::string, gr::property_map, std::less<>> settingsByBlock;
        for (const auto& [key, payload] : valuesByTarget) {
            const auto targets = TargetMap::fromString(std::string_view(key));
            if (!targets) {
                continue; // this is reported during settingsChanged()
            }
            for (const TargetEntry& target : targets->entries() | std::views::filter([](const TargetEntry& entry) { return !entry.isGlob(); })) {
                settingsByBlock[target.blockTarget].insert_or_assign(std::pmr::string(target.propertyName), payload);
            }
        }
        for (auto& [block, settings] : settingsByBlock) {
            toolbar::sendSettings(this->msgOut, block, std::move(settings));
        }
    }
};

} // namespace gr::od

#endif
