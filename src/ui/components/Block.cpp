#include "Block.hpp"
#include "BlockNeighboursPreview.hpp"
#include "Keypad.hpp"

#include <algorithm>
#include <format>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
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
    UiGraphBlock* block = panelContext.selectedBlock();
    if (!block) {
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

    const auto itemSpacing = ImGui::GetStyle().ItemSpacing;

    size = ImGui::GetContentRegionAvail();

    // don't close the panel while the mouse is hovering it or edits are made.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || InputKeypad<>::isVisible()) {
        panelContext.resetTime();
    }

    auto duration = float(std::chrono::duration_cast<std::chrono::milliseconds>(panelContext.closeTime - std::chrono::system_clock::now()).count()) / float(std::chrono::duration_cast<std::chrono::milliseconds>(LookAndFeel::instance().editPaneCloseDelay).count());
    {
        // IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
        IMW::StyleColor color(ImGuiCol_PlotHistogram, ImGui::GetStyle().Colors[ImGuiCol_Button]);
        ImGui::ProgressBar(1.f - duration, {size.x, 3});
    }

    if (!verticalLayout) {
        BlockNeighboursPreview(panelContext, ImGui::GetContentRegionAvail());
        ImGui::SameLine();
    }

    auto minpos = ImGui::GetCursorPos();
    size        = ImGui::GetContentRegionAvail();

    {
        IMW::Child settings("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x, size.y), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(block->blockName.c_str());
        // std::string_view typeName = block->blockTypeName;

        const auto& activeContext = block->activeContext;

        const std::string activeContextLabel = activeContext.context.empty() ? "Default" : activeContext.context;
        if (auto combo = IMW::Combo("##contextNameCombo", activeContextLabel.c_str(), 0)) {
            for (const auto& contextNameAndTime : block->contexts) {
                const bool        selected = activeContext.context == contextNameAndTime.context;
                const std::string label    = contextNameAndTime.context.empty() ? "Default" : contextNameAndTime.context;
                if (ImGui::Selectable(label.c_str(), selected)) {
                    block->setActiveContext(contextNameAndTime);
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

        drawAddContextPopup(block);
        if (drawRemoveContextPopup(activeContext.context)) {
            block->removeContext(activeContext);
        }

        auto typeParams = block->ownerGraph ? block->ownerGraph->availableParametrizationsFor(block->blockTypeName) : UiGraphModel::AvailableParametrizationsResult{};

        if (typeParams.availableParametrizations) {
            if (typeParams.availableParametrizations->size() > 1) {
                if (auto combo = IMW::Combo("##baseTypeCombo", typeParams.parametrization.c_str(), 0)) {
                    for (const auto& availableParametrization : *typeParams.availableParametrizations) {
                        if (ImGui::Selectable(availableParametrization.c_str(), availableParametrization == typeParams.parametrization)) {
                            gr::Message message;
                            assert(!panelContext.targetGraph.empty());
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::scheduler::property::kReplaceBlock;
                            message.data     = gr::property_map{{"uniqueName", block->blockUniqueName},  //
                                    {"type", std::move(typeParams.baseType) + availableParametrization}, //
                                    {"_targetGraph", panelContext.targetGraph}};
                            block->ownerGraph->sendMessage(std::move(message));
                        }
                        if (availableParametrization == typeParams.parametrization) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                }
            }
        }

        if (verticalLayout) {
            BlockNeighboursPreview(panelContext, ImGui::GetContentRegionAvail());
        }

        BlockSettingsControls(block);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            panelContext.resetTime();
        }
    }

    ImGui::SetCursorPos(minpos);
}

void BlockSettingsControls(UiGraphBlock* block, const ImVec2& /*size*/) {
    constexpr auto editorFieldWidth = 150;

    const auto isColourField = [](const UiGraphBlock::SettingsMetaInformation& meta, std::string_view key) {
        auto containsColo = [](std::string_view s) {
            for (std::size_t i = 0; i + 3 < s.size(); ++i) {
                if ((s[i] == 'c' || s[i] == 'C') && (s[i + 1] == 'o' || s[i + 1] == 'O') && (s[i + 2] == 'l' || s[i + 2] == 'L') && (s[i + 3] == 'o' || s[i + 3] == 'O')) {
                    return true;
                }
            }
            return false;
        };
        return containsColo(meta.description) || containsColo(key);
    };

    const auto sendSetSettingMessage = [block](std::string_view keyToUpdate, auto updatedValue) {
        gr::Message message;
        message.serviceName = block->blockUniqueName;
        message.endpoint    = gr::block::property::kSetting;
        message.cmd         = gr::message::Command::Set;
        message.data        = gr::property_map{{std::pmr::string(keyToUpdate), updatedValue}};
        block->ownerGraph->sendMessage(std::move(message));
    };

    InputKeypad<>::clearIfNewBlock(block->blockUniqueName);

    const auto drawSettingRow = [&](const std::string& key, const gr::pmt::Value& value, int& rowIndex) {
        bool isEditable = false;
        gr::pmt::ValueVisitor([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string> || std::floating_point<T> || std::integral<T>) {
                isEditable = true;
            }
        }).visit(value);
        if (!isEditable) {
            return;
        }

        auto          id = ImGui::GetID(key.c_str());
        IMW::ChangeId rowId{int(id)};

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        auto& meta = block->blockSettingsMetaInformation[std::string(key)];
        ImGui::TextUnformatted(meta.description.c_str());

        ImGui::TableSetColumnIndex(1);
        char label[64];
        auto labelResult = std::format_to_n(label, sizeof(label) - 1, "##parameter_{}", rowIndex);
        *labelResult.out = '\0';

        const auto getUnit = [&meta]() -> std::string_view { return meta.unit; };

        gr::pmt::ValueVisitor([&]<typename TArg>(const TArg& arg) {
            using T = std::decay_t<TArg>;
            if constexpr (std::same_as<T, bool>) {
                bool temp = arg;
                if (ImGui::Checkbox(label, &temp)) {
                    sendSetSettingMessage(key, temp);
                }
                IMW::detail::setItemTooltip(key.c_str());
            } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view> || std::same_as<T, std::pmr::string>) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                std::string temp(arg);
                if (ImGui::InputText(label, &temp)) {
                    sendSetSettingMessage(key, std::move(temp));
                }
                IMW::detail::setItemTooltip(key.c_str());
            } else if constexpr (std::unsigned_integral<T> && sizeof(T) >= 4) {
                if (isColourField(meta, key)) {
                    ImVec4 col = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(static_cast<std::uint32_t>(arg)));
                    float  rgb[3]{col.x, col.y, col.z};
                    if (ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_NoInputs)) {
                        auto newColor = static_cast<T>((static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f));
                        sendSetSettingMessage(key, newColor);
                    }
                    IMW::detail::setItemTooltip(key.c_str());
                } else {
                    ImGui::SetNextItemWidth(editorFieldWidth);
                    int temp = static_cast<int>(arg);
                    if (meta.minValue && meta.maxValue) {
                        if (ImGui::SliderInt(label, &temp, static_cast<int>(*meta.minValue), static_cast<int>(*meta.maxValue))) {
                            sendSetSettingMessage(key, static_cast<T>(temp));
                        }
                    } else if (InputKeypad<>::edit(key.c_str(), label, &temp, getUnit())) {
                        sendSetSettingMessage(key, static_cast<T>(temp));
                    }
                    IMW::detail::setItemTooltip(key.c_str());
                }
            } else if constexpr (std::floating_point<T>) {
                ImGui::SetNextItemWidth(editorFieldWidth);
                float temp = static_cast<float>(arg);
                if (meta.minValue && meta.maxValue) {
                    const float minV = static_cast<float>(*meta.minValue);
                    const float maxV = static_cast<float>(*meta.maxValue);
                    if (ImGui::SliderFloat(label, &temp, minV, maxV)) {
                        sendSetSettingMessage(key, static_cast<T>(temp));
                    }
                } else if (InputKeypad<>::edit(key.c_str(), label, &temp, getUnit())) {
                    sendSetSettingMessage(key, static_cast<T>(temp));
                }
                IMW::detail::setItemTooltip(key.c_str());
            } else if constexpr (std::integral<T>) {
                ImGui::SetNextItemWidth(editorFieldWidth);
                int temp = static_cast<int>(arg);
                if (meta.minValue && meta.maxValue) {
                    if (ImGui::SliderInt(label, &temp, static_cast<int>(*meta.minValue), static_cast<int>(*meta.maxValue))) {
                        sendSetSettingMessage(key, static_cast<T>(temp));
                    }
                } else if (InputKeypad<>::edit(key.c_str(), label, &temp, getUnit())) {
                    sendSetSettingMessage(key, static_cast<T>(temp));
                }
                IMW::detail::setItemTooltip(key.c_str());
            }
        }).visit(value);

        ++rowIndex;
    };

    const auto drawSettingsTable = [&](bool visibleOnly) {
        if (auto table = IMW::Table(visibleOnly ? "settings_visible" : "settings_more", 2, ImGuiTableFlags_SizingFixedFit, ImVec2(0, 0), 0.0f)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

            int rowIndex = 0;
            for (const auto& [key, value] : block->blockSettings) {
                std::string keyStr(key);
                auto        metaIt   = block->blockSettingsMetaInformation.find(keyStr);
                bool        isMarked = metaIt != block->blockSettingsMetaInformation.end() && metaIt->second.isVisible;
                if (isMarked != visibleOnly) {
                    continue;
                }
                drawSettingRow(keyStr, value, rowIndex);
            }
        }
    };

    bool hasVisibleSettings = std::ranges::any_of(block->blockSettings, [&](const auto& kv) {
        auto it = block->blockSettingsMetaInformation.find(std::string(kv.first));
        return it != block->blockSettingsMetaInformation.end() && it->second.isVisible;
    });

    if (hasVisibleSettings) {
        IMW::TabBar tabBar("settings_tabs", 0);
        if (auto tab = IMW::TabItem("Settings", nullptr, 0)) {
            drawSettingsTable(true);
        }
        if (auto tab = IMW::TabItem("more...", nullptr, 0)) {
            drawSettingsTable(false);
        }
    } else {
        drawSettingsTable(false);
    }
}

BlockControlsPanelContext::BlockControlsPanelContext() {
    blockClickedCallback = [this](UiGraphBlock* clickedBlock) {
        // Guaranteed by the caller
        assert(clickedBlock && clickedBlock != selectedBlock());

        this->graphModel->selectedBlock = clickedBlock;
        resetTime();
    };
}

void BlockControlsPanelContext::resetTime() { closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay; }

UiGraphBlock* BlockControlsPanelContext::selectedBlock() const { return graphModel ? graphModel->selectedBlock : nullptr; }

void BlockControlsPanelContext::setSelectedBlock(UiGraphBlock* block, UiGraphModel* model) {
    graphModel = model;
    if (graphModel) {
        graphModel->selectedBlock = block;
    }
}

} // namespace DigitizerUi::components
