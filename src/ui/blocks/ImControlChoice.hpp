#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROLCHOICE_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROLCHOICE_HPP

#include "ImControl.hpp"

#include "../components/StyledWidgets.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gr::od {

struct ImControlChoice : ImControl<ImControlChoice> {
    using Description = gr::Doc<R""(UI control that picks one of `choices`, or of the controlled property's enum values, and sets it on the settings target_map determines.
A choice's `choice_values` entry is sent instead: to target_map, or without target_map as "BlockA,BlockB:setting" keys to values, one message per block, every entry setting the same keys.
`value` is applied at start.)"">;

    std::string              value;
    std::vector<std::string> choices;
    gr::property_map         choice_values;
    const std::string        compatible_type_map = "enum";

    GR_MAKE_REFLECTABLE(ImControlChoice, value, choices, choice_values, compatible_type_map);

    bool _appliedAtStart = false;

    explicit ImControlChoice(gr::property_map initParameters = {}) : ImControl<ImControlChoice>(std::move(initParameters)) {}

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        ImControl<ImControlChoice>::settingsChanged(oldSettings, newSettings);
        if (const std::string problem = keyedChoicesProblem(); !problem.empty()) {
            gr::log::warning("{}: {}", this->unique_name, problem);
        }
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto                      parsedConfig    = DrawableBlockConfig::fromMap(config);
        const bool                      explicitChoices = !choices.empty();
        const std::vector<std::string>& offered         = explicitChoices ? choices : parsedConfig.aggregateControlledPropertyHints.choices;
        const auto&                     style           = LookAndFeel::instance().dashboardStyle;
        const float                     inputWidth      = [&] {
            if (offered.empty()) {
                return style.toolbarControlInputWidth;
            }
            return std::max(style.toolbarControlMinimumWidth, comboWidthForText(std::ranges::max(offered | std::views::transform([](const std::string& choice) { return ImGui::CalcTextSize(choice.c_str()).x; }))));
        }();
        SizeAndLayout layout    = prepareDraw(parsedConfig, inputWidth, style.toolbarControlMinimumWidth, labelWidth());
        layout.hasAnyConnection = layout.hasAnyConnection || explicitChoices;
        if (!explicitChoices) {
            loadValueIfConnectionChanged(value, parsedConfig);
        } else if (!value.empty() && !std::exchange(_appliedAtStart, true)) {
            apply(value);
        }
        drawWithLabel(layout, [&](float width) {
            const std::string id = widgetId();
            // disabled inside the flowgraph editor: the node editor cannot draw combo boxes in nodes;
            // a fix would need a two-pass draw, in node editor space and normally, see https://github.com/thedmd/imgui-node-editor/issues/48
            const bool    insideNodeEditor = parsedConfig.drawingContext == DrawableBlockConfig::DrawingContext::FlowgraphView;
            imw::Disabled nothingToChooseOrCannotOpen(offered.empty() || insideNodeEditor);
            ImGui::SetNextItemWidth(width);
            if (auto combo = ComboBox(id.c_str(), value.c_str(), 0)) {
                for (const std::string& choice : offered) {
                    if (selectable(choice.c_str(), choice == value)) {
                        value = choice;
                        stageValue(value);
                        apply(value);
                    }
                }
            }
        });
        return gr::work::Status::OK;
    }

    void apply(const std::string& choice) {
        if (choices.empty()) {
            sendToTargets(choice);
            return;
        }
        const auto entry = choice_values.find(std::pmr::string(choice));
        if (entry == choice_values.end()) {
            if (!target_map.empty()) {
                sendToTargets(choice);
            }
            return;
        }
        if (!target_map.empty()) {
            sendToTargets(entry->second);
        } else if (const auto valuesByTarget = entry->second.get_if<gr::property_map>()) {
            sendByTarget(*valuesByTarget);
        }
    }

    [[nodiscard]] std::string keyedChoicesProblem() const {
        if (!target_map.empty() || choices.empty()) {
            return {};
        }
        std::optional<std::set<std::string, std::less<>>> firstKeys;
        for (const std::string& choice : choices) {
            const auto entry = choice_values.find(std::pmr::string(choice));
            if (entry == choice_values.end()) {
                return std::format("choice '{}' has no choice_values entry", choice);
            }
            const auto valuesByTarget = entry->second.get_if<gr::property_map>();
            if (!valuesByTarget) {
                return std::format("choice '{}': without target_map, its choice_values entry maps 'block:setting' keys to values", choice);
            }
            std::set<std::string, std::less<>> keys;
            for (const auto& [key, _] : *valuesByTarget) {
                if (const auto targets = TargetMap::fromString(std::string_view(key)); !targets) {
                    return std::format("choice '{}': key '{}': {}", choice, std::string_view(key), targets.error());
                }
                keys.emplace(key);
            }
            if (!firstKeys) {
                firstKeys = std::move(keys);
            } else if (keys != *firstKeys) {
                return std::format("choice '{}' sets other keys than '{}', so settings of one choice would outlive it", choice, choices.front());
            }
        }
        return {};
    }
};

} // namespace gr::od

#endif
