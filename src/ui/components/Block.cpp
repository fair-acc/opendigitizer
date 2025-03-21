#include "Block.hpp"
#include "Keypad.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "../common/LookAndFeel.hpp"
#include "../components/Dialog.hpp"

#include "../App.hpp"

using namespace std::string_literals;

namespace DigitizerUi::components {

constexpr const char* addContextPopupId    = "Add Context";
constexpr const char* removeContextPopupId = "Remove Context";

void drawAddContextPopup(UiGraphBlock* block) {
    ImGui::SetNextWindowSize({600, 120}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup(addContextPopupId, nullptr, 0)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name:");
        ImGui::SameLine();
        static std::string name;
        if (ImGui::IsWindowAppearing()) {
            name = {};
        }
        ImGui::InputText("##contextName", &name);

        const bool okEnabled = !name.empty();

        if (components::DialogButtons(okEnabled) == components::DialogButton::Ok) {
            block->addContext(UiGraphBlock::ContextTime{
                .context = name,
                .time    = 1,
            });
        }
    }
}

bool drawRemoveContextPopup(const std::string& context) {
    ImGui::SetNextWindowSize({600, 100}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup(removeContextPopupId, nullptr, 0)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Do you wanto to remove '%s' context?", context.c_str());
        return components::DialogButtons() == components::DialogButton::Ok;
    }
    return false;
}

void BlockControlsPanel(BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout) {
    if (!panelContext.block) {
        return;
    }
    using namespace DigitizerUi;

    auto size = frameSize;
    if (panelContext.closeTime < std::chrono::system_clock::now()) {
        panelContext = {};
        return;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(frameSize);
    IMW::Window blockControlsPanel("BlockControlsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

    const float lineHeight = [&] {
        IMW::Font font(LookAndFeel::instance().fontIconsSolid);
        return ImGui::GetTextLineHeightWithSpacing() * 1.5f;
    }();

    auto resetTime = [&]() { panelContext.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay; };

    const auto itemSpacing = ImGui::GetStyle().ItemSpacing;

    size = ImGui::GetContentRegionAvail();

    // don't close the panel while the mouse is hovering it or edits are made.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || InputKeypad<>::isVisible()) {
        resetTime();
    }

    auto duration = float(std::chrono::duration_cast<std::chrono::milliseconds>(panelContext.closeTime - std::chrono::system_clock::now()).count()) / float(std::chrono::duration_cast<std::chrono::milliseconds>(LookAndFeel::instance().editPaneCloseDelay).count());
    {
        // IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
        IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        ImGui::ProgressBar(1.f - duration, {size.x, 3});
    }

    auto minpos = ImGui::GetCursorPos();
    size        = ImGui::GetContentRegionAvail();

    {
        IMW::Child settings("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x - lineHeight - itemSpacing.x, size.y), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(panelContext.block->blockName.c_str());
        // std::string_view typeName = panelContext.block->blockTypeName;

        const auto& activeContext = panelContext.block->activeContext;

        const std::string activeContextLabel = activeContext.context.empty() ? "Default" : activeContext.context;
        if (auto combo = IMW::Combo("##contextNameCombo", activeContextLabel.c_str(), 0)) {
            for (const auto& contextNameAndTime : panelContext.block->contexts) {
                const bool        selected = activeContext.context == contextNameAndTime.context;
                const std::string label    = contextNameAndTime.context.empty() ? "Default" : contextNameAndTime.context;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    panelContext.block->setActiveContext(contextNameAndTime);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        {
            ImGui::SameLine();
            IMW::Disabled disableDueDefaultAction(activeContext.context.empty());
            IMW::Font     _(LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf146")) {
                ImGui::OpenPopup(removeContextPopupId);
            }
        }
        IMW::detail::setItemTooltip("Remove context");

        {
            ImGui::SameLine();
            IMW::Font _(LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf0fe")) {
                ImGui::OpenPopup(addContextPopupId);
            }
        }
        IMW::detail::setItemTooltip("Add new context");

        drawAddContextPopup(panelContext.block);
        if (drawRemoveContextPopup(activeContext.context)) {
            panelContext.block->removeContext(activeContext);
        }

        auto typeParams = panelContext.block->ownerGraph->availableParametrizationsFor(panelContext.block->blockTypeName);

        if (typeParams.availableParametrizations) {
            if (typeParams.availableParametrizations->size() > 1) {
                if (auto combo = IMW::Combo("##baseTypeCombo", typeParams.parametrization.c_str(), 0)) {
                    for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                        if (ImGui::Selectable(availableParametrization.c_str(), availableParametrization == typeParams.parametrization)) {
                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::graph::property::kReplaceBlock;
                            message.data     = gr::property_map{
                                    {"uniqueName"s, panelContext.block->blockUniqueName},                 //
                                    {"type"s, std::move(typeParams.baseType) + availableParametrization}, //
                            };
                            panelContext.block->ownerGraph->sendMessage(std::move(message));
                        }
                        if (availableParametrization == typeParams.parametrization) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
            }
        }

        BlockSettingsControls(panelContext.block, verticalLayout);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            resetTime();
        }
    }

    ImGui::SetCursorPos(minpos);
}

void BlockSettingsControls(UiGraphBlock* block, bool /*verticalLayout*/, const ImVec2& /*size*/) {
    constexpr auto editorFieldWidth = 150;
    if (auto table = IMW::Table("settings_table", 2, ImGuiTableFlags_SizingFixedFit, ImVec2(0, 0), 0.0f)) {
        // Setup columns without headers
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        int i = 0;
        for (const auto& [key, value] : block->blockSettings) {
            // Do we know how to edit this type?
            if (!std::visit(gr::meta::overloaded{//
                                [&](float) { return true; },
                                [&]([[maybe_unused]] auto&& val) {
                                    using T = std::decay_t<decltype(val)>;
                                    if constexpr (std::integral<T>) {
                                        return true;
                                    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                        return true;
                                    }
                                    return false;
                                }},
                    value)) {
                continue;
            };

            auto          id = ImGui::GetID(key.c_str());
            IMW::ChangeId rowId{int(id)};

            ImGui::TableNextRow();

            // Column 1: Label
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key.c_str());

            // Column 2: Input
            ImGui::TableSetColumnIndex(1);
            char label[64];
            snprintf(label, sizeof(label), "##parameter_%d", i);

            auto sendSetSettingMessage = [block](auto blockUniqueName, auto keyToUpdate, auto updatedValue) {
                gr::Message message;
                message.serviceName = blockUniqueName;
                message.endpoint    = gr::block::property::kSetting;
                message.cmd         = gr::message::Command::Set;
                message.data        = gr::property_map{{keyToUpdate, updatedValue}};
                block->ownerGraph->sendMessage(std::move(message));
            };

            const auto getUnit = [block, key]() -> std::string_view {
                const auto it = block->blockMetaInformation.find(key + "::unit");
                if (it != block->blockMetaInformation.end()) {
                    if (const auto unitPtr = std::get_if<std::string>(&it->second); unitPtr) {
                        return *unitPtr;
                    }
                }
                return {};
            };

            std::visit(gr::meta::overloaded{[&](float val) {
                                                ImGui::SetNextItemWidth(editorFieldWidth);
                                                float temp = val;
                                                if (InputKeypad<>::edit(key.c_str(), label, &temp, getUnit())) {
                                                    sendSetSettingMessage(block->blockUniqueName, key, temp);
                                                }
                                            },
                           [&](auto&& val) {
                               using T = std::decay_t<decltype(val)>;
                               if constexpr (std::integral<T>) {
                                   ImGui::SetNextItemWidth(editorFieldWidth);
                                   int temp = int(val);
                                   if (InputKeypad<>::edit(key.c_str(), label, &temp, getUnit())) {
                                       sendSetSettingMessage(block->blockUniqueName, key, temp);
                                   }
                               } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                   ImGui::SetNextItemWidth(-FLT_MIN); // Stretch to available width
                                   std::string temp(val);
                                   if (ImGui::InputText(label, &temp)) {
                                       sendSetSettingMessage(block->blockUniqueName, key, std::move(temp));
                                   }
                                   IMW::detail::setItemTooltip(key.c_str());
                               }
                           }},
                value);

            ++i;
        }
    }
}

} // namespace DigitizerUi::components
