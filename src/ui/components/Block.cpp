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

void setItemTooltip(auto&&... args) {
    if (ImGui::IsItemHovered()) {
        if constexpr (sizeof...(args) == 0) {
            ImGui::SetTooltip("");
        } else {
            ImGui::SetTooltip("%s", std::forward<decltype(args)...>(args...));
        }
    }
}

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
        if (ImGui::BeginCombo("##contextNameCombo", activeContextLabel.c_str())) {
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
            ImGui::EndCombo();
        }

        {
            ImGui::SameLine();
            IMW::Disabled disableDueDefaultAction(activeContext.context.empty());
            IMW::Font     _(LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf146")) {
                ImGui::OpenPopup(removeContextPopupId);
            }
        }
        setItemTooltip("Remove context");

        {
            ImGui::SameLine();
            IMW::Font _(LookAndFeel::instance().fontIconsSolid);
            if (ImGui::Button("\uf0fe")) {
                ImGui::OpenPopup(addContextPopupId);
            }
        }
        setItemTooltip("Add new context");

        drawAddContextPopup(panelContext.block);
        if (drawRemoveContextPopup(activeContext.context)) {
            panelContext.block->removeContext(activeContext);
        }

        auto typeParams = App::instance().flowgraphPage.graphModel().availableParametrizationsFor(panelContext.block->blockTypeName);

        if (typeParams.availableParametrizations) {
            if (typeParams.availableParametrizations->size() > 1) {
                if (ImGui::BeginCombo("##baseTypeCombo", typeParams.parametrization.c_str())) {
                    for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                        if (ImGui::Selectable(availableParametrization.c_str(), availableParametrization == typeParams.parametrization)) {
                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::graph::property::kReplaceBlock;
                            message.data     = gr::property_map{
                                    {"uniqueName"s, panelContext.block->blockUniqueName},                 //
                                    {"type"s, std::move(typeParams.baseType) + availableParametrization}, //
                            };
                            App::instance().sendMessage(message);
                        }
                        if (availableParametrization == typeParams.parametrization) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
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

void BlockSettingsControls(UiGraphBlock* block, bool verticalLayout, const ImVec2& /*size*/) {
    const auto availableSize = ImGui::GetContentRegionAvail();

    auto             storage = ImGui::GetStateStorage();
    IMW::ChangeStrId mainId("block_controls");

    const auto& style     = ImGui::GetStyle();
    const auto  indent    = style.IndentSpacing;
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    int i = 0;
    for (const auto& p : block->blockSettings) {
        auto id = ImGui::GetID(p.first.c_str());
        ImGui::PushID(int(id));
        auto* enabled = storage->GetBoolRef(id, true);

        {
            IMW::Group topGroup;
            const auto curpos = ImGui::GetCursorPos();

            {
                IMW::Group controlGroup;

                if (*enabled) {
                    char label[64];
                    snprintf(label, sizeof(label), "##parameter_%d", i);

                    auto sendSetSettingMessage = [](auto blockUniqueName, auto key, auto value) {
                        gr::Message message;
                        message.serviceName = blockUniqueName;
                        message.endpoint    = gr::block::property::kSetting;
                        message.cmd         = gr::message::Command::Set;
                        message.data        = gr::property_map{{key, value}};
                        App::instance().sendMessage(std::move(message));
                    };

                    const bool controlDrawn = std::visit( //
                        gr::meta::overloaded{             //
                            [&](float val) {
                                ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                ImGui::SetNextItemWidth(100);
                                if (InputKeypad<>::edit(label, &val)) {
                                    sendSetSettingMessage(block->blockUniqueName, p.first, val);
                                }
                                return true;
                            },
                            [&](auto&& val) {
                                using T = std::decay_t<decltype(val)>;
                                if constexpr (std::integral<T>) {
                                    auto v = int(val);
                                    ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                    ImGui::SetNextItemWidth(100);
                                    if (InputKeypad<>::edit(label, &v)) {
                                        sendSetSettingMessage(block->blockUniqueName, p.first, v);
                                    }
                                    return true;
                                } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
                                    ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
                                    std::string str(val);
                                    if (ImGui::InputText("##in", &str)) {
                                        sendSetSettingMessage(block->blockUniqueName, p.first, std::move(str));
                                    }
                                    return true;
                                }
                                return false;
                            }},
                        p.second);

                    if (!controlDrawn) {
                        ImGui::PopID();
                        continue;
                    }
                }
            }
            ImGui::SameLine(0, 0);

            auto        width = verticalLayout ? availableSize.x : ImGui::GetCursorPosX() - curpos.x;
            const auto* text  = *enabled || verticalLayout ? p.first.c_str() : "";
            width             = std::max(width, indent + ImGui::CalcTextSize(text).x + style.FramePadding.x * 2);

            {
                IMW::StyleColor buttonStyle(ImGuiCol_Button, style.Colors[*enabled ? ImGuiCol_ButtonActive : ImGuiCol_TabUnfocusedActive]);
                ImGui::SetCursorPos(curpos);

                float height = !verticalLayout && !*enabled ? availableSize.y : 0.f;
                if (ImGui::Button("##nothing", {width, height})) {
                    *enabled = !*enabled;
                }
            }

            setItemTooltip(p.first.c_str());

            ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
            ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, *enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            if (*enabled || verticalLayout) {
                ImGui::TextUnformatted(p.first.c_str());
            }
        }

        if (!verticalLayout) {
            ImGui::SameLine();
        }

        ImGui::PopID();
        ++i;
    }
}

} // namespace DigitizerUi::components
