#ifndef OPENDIGITIZER_TOOLBAR_BLOCK_H
#define OPENDIGITIZER_TOOLBAR_BLOCK_H

#include <expected>

#include <format>
#include <optional>

#include <gnuradio-4.0/Block.hpp>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include "../components/StyledStandardWidgets.hpp"
#include "TargetMap.hpp"

namespace DigitizerUi {
namespace toolbar {
[[nodiscard]] inline bool drawItem(const std::string& uniqueName, const std::string& label, const std::string& icon, const std::string& tooltip, auto widget) {
    IMW::ChangeStrId id(uniqueName.c_str());
    bool             changed = false;
    if (icon.empty()) {
        changed = widget(label.c_str());
    } else {
        IMW::Font font(LookAndFeel::instance().fontIconsSolid);
        changed = widget(icon.c_str());
    }

    (void)tooltip;
    // TODO: fix tooltips not drawing correctly in the flowgraph. this is the same issue as ImControlChoice being disabled when drawn in the flowgraph.
    // if (!tooltip.empty()) {
    //     ImGui::SetItemTooltip("%s", tooltip.c_str());
    // }
    return changed;
}

struct ItemWidths {
    float natural = 0.f;
    float minimum = 0.f;
    float label   = 0.f;
};

inline void publishWidths(const std::string& uniqueName, const ItemWidths& widths) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    storage->SetFloat(ImGui::GetID((uniqueName + "##toolbarNatural").c_str()), widths.natural);
    storage->SetFloat(ImGui::GetID((uniqueName + "##toolbarMinimum").c_str()), widths.minimum);
    storage->SetFloat(ImGui::GetID((uniqueName + "##toolbarLabel").c_str()), widths.label);
}

[[nodiscard]] inline std::optional<ItemWidths> publishedWidths(const std::string& uniqueName) {
    const ImGuiStorage* storage = ImGui::GetStateStorage();
    const float         natural = storage->GetFloat(ImGui::GetID((uniqueName + "##toolbarNatural").c_str()), -1.f);
    if (natural < 0.f) {
        return std::nullopt;
    }
    return ItemWidths{.natural = natural, .minimum = storage->GetFloat(ImGui::GetID((uniqueName + "##toolbarMinimum").c_str())), .label = storage->GetFloat(ImGui::GetID((uniqueName + "##toolbarLabel").c_str()))};
}

inline void sendSettings(gr::MsgPortOutBuiltin& port, const std::string& targetBlock, gr::property_map settings) {
    if (targetBlock.empty()) { // an empty service name would address every block
        return;
    }
    gr::emit<gr::message::Command::Set>(port, std::move(settings), {.endpoint = gr::block::property::kSetting, .serviceName = targetBlock});
}

/// sets `payload` on every "BlockA,BlockB:setting" entry of `targetMap`; an ill-formed map sends nothing
inline void sendToTargets(gr::MsgPortOutBuiltin& port, std::string_view targetMap, const auto& payload) {
    const auto targets = TargetMap::fromString(targetMap);
    if (!targets) {
        return;
    }
    for (const TargetEntry& target : targets->entries()) {
        if (target.isGlob()) {
            continue; // TODO: GR4 support
        }
        sendSettings(port, target.blockTarget, gr::property_map{{std::pmr::string(target.propertyName), payload}});
    }
}
} // namespace toolbar
} // namespace DigitizerUi

#endif
