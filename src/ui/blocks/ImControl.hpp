#ifndef OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP
#define OPENDIGITIZER_UI_BLOCKS_IMCONTROL_HPP

#include "../common/ImguiWrap.hpp"
#include "TargetMap.hpp"
#include "ToolbarBlock.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Logger.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace DigitizerUi {

namespace control {
constexpr inline float kImControlTextInputWidth = 110.f;
constexpr inline float kImControlMinimumWidth   = 60.f;
} // namespace control

/*
 * CRTP wrapper which deals with drawing a control into the toolbar, and also
 * inherits from gr::Drawable/Category::Toolbar. The combination of the
 * presence of the `target_map` key and the UI category will cause the toolbar
 * to recognize this block as something that should be drawn in it, and the
 * flowgraph to recognize this block as something that can be connected to the
 * properties of other blocks. This also includes some ImGui-specific utilities.
 */
template<typename TDerived>
struct ImControl : gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> {
    using DrawableBlock = gr::Block<TDerived, gr::Drawable<gr::UICategory::Toolbar, "ImGui">>;

    // information about how big a control should be (its placement in the
    // toolbar) and how its internal components should be laid out within that
    // area
    struct SizeAndLayout {
        float width     = 0.f;
        bool  showLabel = true;
    };

    std::string target_map; // of the form "BlockA,BlockB:property;BlockC:property;*:other_property"
    std::string label = "value";

    GR_MAKE_REFLECTABLE(ImControl, target_map, label);

private:
    std::vector<TargetEntry> _targets;
    std::string              _parsedTargetMap;

public:
    explicit ImControl(gr::property_map initParameters = {}) : DrawableBlock(std::move(initParameters)) {}

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    [[nodiscard]] float labelWidth() const { return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x; }

    /// Setup at the start of draw(). It applies changed settings, publishes the width constraints onto the imgui
    /// stack, and returns how to lay things out given config (the toolbar layout constraints) and the other
    /// arguments (this widget's layout constraints)
    /// @param labelPart is 0 for a widget such as a button, something that does not have a separate label component
    [[nodiscard]] SizeAndLayout prepareDraw(const gr::property_map& config, float naturalWidget, float minimumWidget, float labelPart) {
        this->applyChangedSettings(); // no work() applies them
        const float maxWidth  = config.value_or<float>("max_width", std::numeric_limits<float>::max());
        const bool  showLabel = config.value_or<bool>("show_label", true);
        toolbar::publishWidths(this->unique_name, {.natural = naturalWidget + labelPart, .minimum = minimumWidget + labelPart, .label = labelPart});
        return {.width = std::clamp(maxWidth - (showLabel ? labelPart : 0.f), minimumWidget, naturalWidget), .showLabel = showLabel};
    }

    void drawInIdScope(auto drawContent) {
        const std::string scopeName(this->unique_name);
        IMW::ChangeStrId  scope(scopeName.c_str());
        drawContent();
    }

    /// Draw something with a label before it and support shrinking it into a tooltip if the label wouldn't fit
    void drawLabelled(const SizeAndLayout& layout, auto drawWidget) {
        drawInIdScope([&] {
            if (layout.showLabel) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label.c_str());
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            }
            {
                IMW::Group widgets;
                drawWidget(layout.width);
            }
            if (!layout.showLabel) {
                ImGui::SetItemTooltip("%s", label.c_str());
            }
        });
    }

    /// Indiscriminately send a settings update message to every block identified by the target_map.
    /// It is their job to refuse messages with an incorrect / not matching type
    void sendToTargets(const auto& payload) {
        if (_parsedTargetMap != target_map) {
            parseTargets();
        }
        for (const TargetEntry& target : _targets) {
            for (const std::string& block : target.blocks) {
                toolbar::sendSettings(this->msgOut, block, gr::property_map{{std::pmr::string(target.property), payload}});
            }
        }
    }

private:
    void parseTargets() {
        _parsedTargetMap = target_map;
        auto parsed      = parseTargetMap(target_map);
        if (!parsed) {
            gr::log::warning("{}: invalid target_map '{}': {}", this->unique_name, target_map, parsed.error());
            _targets.clear();
            return;
        }
        _targets = std::move(*parsed);
        if (std::ranges::any_of(_targets, &TargetEntry::allBlocks)) {
            // TODO: GR4 support
            gr::log::warning("{}: target_map '{}' tries to use glob-selector '*' but that is not implemented yet", this->unique_name, target_map);
        }
    }
};

} // namespace DigitizerUi

#endif
