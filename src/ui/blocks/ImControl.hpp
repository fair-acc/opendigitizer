#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP

#include "../common/ImguiWrap.hpp"
#include "DrawableBlockConfig.hpp"
#include "TargetMap.hpp"
#include "ToolbarBlock.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/formatter/ValueFormatter.hpp>

#include <algorithm>
#include <concepts>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace DigitizerUi {

/*
 * CRTP wrapper for class which should be a block that is an item in the
 * toolbar, drawn using ImGui. This class adds some common utilities that,
 * along with doing UI control by sending messages to other blocks.
 */
template<typename TDerived>
struct ImControl : gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    using DrawableBlock = gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">>;

    // information about how big a control should be (its placement in the
    // toolbar) and how its internal components should be laid out within that
    // area
    struct SizeAndLayout {
        float width            = 0.f;
        bool  showLabel        = true;
        bool  hasAnyConnection = true;
    };

    std::string target_map; // of the form "BlockA,BlockB:property;BlockC:property;*:other_property"
    std::string label = "value";
    std::string icon;
    std::string tooltip;

    GR_MAKE_REFLECTABLE(ImControl, target_map, label, icon, tooltip);

private:
    // used to track whether the property we are connected to has changed, to invalidate cache
    std::string _lastDrawnTarget;

public:
    explicit ImControl(gr::property_map initParameters = {}) : DrawableBlock(std::move(initParameters)) {}

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (!newSettings.contains("target_map")) {
            return;
        }
        const auto targets = TargetMap::fromString(target_map);
        if (!targets) {
            gr::log::warning("{}: invalid target_map '{}': {}", this->unique_name, target_map, targets.error());
        } else if (std::ranges::any_of(targets->entries(), &TargetEntry::isGlob)) {
            // TODO: GR4 support
            gr::log::warning("{}: target_map '{}' tries to use glob-selector '*' but that is not implemented yet", this->unique_name, target_map);
        }
    }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    [[nodiscard]] float labelWidth() const { return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x; }

    /// Setup at the start of draw(). It applies changed settings, publishes the width constraints onto the imgui
    /// stack, and returns how to lay things out given config (the toolbar layout constraints) and the other
    /// arguments (this widget's layout constraints)
    /// @param labelPart is 0 for a widget such as a button, something that does not have a separate label component
    [[nodiscard]] SizeAndLayout prepareDraw(const DrawableBlockConfig& config, float naturalWidget, float minimumWidget, float labelPart) {
        this->applyChangedSettings(); // no work() applies them
        const float maxWidth  = config.maxWidth.value_or(std::numeric_limits<float>::max());
        const bool  showLabel = config.showLabel.value_or(true);
        toolbar::publishWidths(this->unique_name, {.natural = naturalWidget + labelPart, .minimum = minimumWidget + labelPart, .label = labelPart});
        return {.width = std::clamp(maxWidth - (showLabel ? labelPart : 0.f), minimumWidget, naturalWidget), .showLabel = showLabel, .hasAnyConnection = config.firstControlledProperty.has_value()};
    }

    /// Update a cache @param value with the controlled property's value if the
    /// @param config indicates that we are connected to something different
    /// Doing this causes control blocks to load the value from their target
    /// when initially connected, and unload it when disconnected.
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
        std::ignore = this->settings().setStaged({{"value", value}});
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

    struct LabelAndFont {
        ImControl*               _self;
        std::optional<IMW::Font> _font;

        [[nodiscard]] bool shouldUseIcon() const { return !_self->icon.empty(); }
        explicit LabelAndFont(ImControl& self) : _self(std::addressof(self)) {
            if (shouldUseIcon()) {
                _font.emplace(LookAndFeel::instance().fontIconsSolid);
            }
        }
        [[nodiscard]] const std::string& selectedLabel() { return shouldUseIcon() ? _self->icon : _self->label; }
    };

    /// Draw something with a label before it and support shrinking it into a tooltip if the label wouldn't fit
    void drawWithLabelAndTooltip(const SizeAndLayout& layout, auto drawWidget) {
        IMW::ChangeStrId scope(this->unique_name.value().c_str());

        const auto setTooltip = [this] {
            (void)this;
            // TODO: fix tooltips not drawing correctly in the flowgraph. this is
            // the same issue as ImControlChoice being disabled when drawn in the
            // flowgraph.
            // if (!tooltip.empty()) {
            //     IMW::Font font(LookAndFeel::instance().fontIconsSolid);
            //     ImGui::SetItemTooltip("%s", tooltip.c_str());
            // } else if (!label.empty() && !layout.showLabel) {
            //     ImGui::SetItemTooltip("%s", label.c_str());
            // }
        };

        {
            LabelAndFont labelAndFont(*this);
            if (layout.showLabel && !labelAndFont.selectedLabel().empty()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(labelAndFont.selectedLabel().c_str());
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            }
        }
        setTooltip();

        {
            IMW::Group    widgets;
            IMW::Disabled disconnected(!layout.hasAnyConnection);
            drawWidget(layout.width);
        }
        setTooltip();
    }

    /// Send a settings update message to every block identified by the target_map
    void sendToTargets(const auto& payload) {
        const auto targets = TargetMap::fromString(target_map);
        if (!targets) {
            return; // this is reported during settingsChanged()
        }
        for (const TargetEntry& target : targets->entries()) {
            if (target.isGlob()) {
                continue; // TODO: GR4 support
            }
            toolbar::sendSettings(this->msgOut, target.blockTarget, gr::property_map{{std::pmr::string(target.propertyName), payload}});
        }
    }
};

} // namespace DigitizerUi

#endif
