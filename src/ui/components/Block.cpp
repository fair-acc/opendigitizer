#include "Block.hpp"
#include "BlockNeighboursPreview.hpp"

#include <algorithm>
#include <format>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <misc/cpp/imgui_stdlib.h>

#include "../GraphModel.hpp"
#include "../common/LookAndFeel.hpp"
#include "../components/Dialog.hpp"
#include "Keypad.hpp"

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
        ImGui::Text("Do you want to remove '%s' context?", context.c_str());
        return components::DialogButtons() == components::DialogButton::Ok;
    }
    return false;
}

static void blockContextComboBox(UiGraphBlock* block) {
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
}

static BlockPropertyEditResult BlockControlsPanelImpl(BlockControlsPanelContext& panelContext, bool verticalLayout) {
    // guaranteed to be non-null by check in BlockControlsPanel()
    UiGraphBlock* block = panelContext.selectedBlock();

    using namespace DigitizerUi;
    const float lineHeight = [&] {
        IMW::Font font(LookAndFeel::instance().fontIconsSolid);
        return ImGui::GetTextLineHeightWithSpacing() * 1.5f;
    }();

    if (!verticalLayout) {
        BlockNeighboursPreview(panelContext, ImGui::GetContentRegionAvail());
        ImGui::SameLine();
    }

    const auto itemSpacing = ImGui::GetStyle().ItemSpacing;

    auto minpos = ImGui::GetCursorPos();
    auto size   = ImGui::GetContentRegionAvail();

    BlockPropertyEditResult propertyEditResult;
    {
        IMW::Child settings("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x, size.y), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(block->blockName.c_str());
        // std::string_view typeName = block->blockTypeName;

        blockContextComboBox(block);

        auto typeParams = block->ownerGraph ? block->ownerGraph->availableParametrizationsFor(block->blockTypeName) : UiGraphModel::AvailableParametrizationsResult{};

        if (typeParams.availableParametrizations && typeParams.availableParametrizations->size() > 1) {
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

        if (verticalLayout) {
            BlockNeighboursPreview(panelContext, ImGui::GetContentRegionAvail());
        }

        propertyEditResult = BlockSettingsControls(block);

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            panelContext.resetTime();
        }
    }

    ImGui::SetCursorPos(minpos);

    return propertyEditResult;
}

BlockControlsPanelResult BlockControlsPanel(BlockControlsPanelContext& panelContext, const ImVec2& pos, const ImVec2& frameSize, bool verticalLayout) {
    if (!panelContext.selectedBlock()) {
        return {};
    }

    auto size = frameSize;
    if (panelContext.closeTime < std::chrono::system_clock::now()) {
        panelContext = {};
        return {};
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(frameSize);
    IMW::Window blockControlsPanel("BlockControlsPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking);

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

    IMW::TabBar tabBar("blockControlsPanel##", ImGuiTabBarFlags_None);

    const ImGuiTabItemFlags  selectedBlockFlags = std::exchange(panelContext.wantsSelectedBlockTab, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
    BlockControlsPanelResult result;
    if (auto item = IMW::TabItem("Selected Block", nullptr, selectedBlockFlags)) {
        result = {
            .allExportedPropertiesPageResult = {},
            .blockEditPaneResult             = BlockControlsPanelImpl(panelContext, verticalLayout),
        };
    }
    if (auto item = IMW::TabItem("All Exported Properties", nullptr, ImGuiTabItemFlags_None)) {
        using Flags       = ExportedPropertyList::Flags;
        const auto params = ExportedPropertyList::Params{.flags = Flags::DragDropHandle | Flags::ContextMenuButton};
        result            = {
                       .allExportedPropertiesPageResult = panelContext.propertyList.draw(*panelContext.graphModel, params),
                       .blockEditPaneResult             = {},
        };
    }
    return result;
}

constexpr ImGuiColorEditFlags colorFieldEditorFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha; // ColorEdit3 implies NoAlpha

gr::pmt::Value editBlockProperty(const char* label, const std::string& propertyName, const gr::pmt::Value& currentPropertyValue, const UiGraphBlock::SettingsMetaInformation& meta) {
    gr::pmt::Value out;
    using Type = UiGraphBlock::SettingsControlType;
    switch (meta.controlType(propertyName, currentPropertyValue)) {
    case Type::Color: {
        gr::pmt::ValueVisitor([label, &out]<typename T>(const T& value) {
            if constexpr (std::unsigned_integral<T> && sizeof(T) >= 4) {
                ImVec4 col = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(static_cast<std::uint32_t>(value)));
                float  rgb[3]{col.x, col.y, col.z};
                if (ImGui::ColorEdit3(label, rgb, colorFieldEditorFlags)) {
                    out = static_cast<T>((static_cast<std::uint32_t>(rgb[0] * 255.0f) << 16) | (static_cast<std::uint32_t>(rgb[1] * 255.0f) << 8) | static_cast<std::uint32_t>(rgb[2] * 255.0f));
                }
            }
        }).visit(currentPropertyValue);
        break;
    }
    case Type::TextInput: {
        assert(meta.enumValues.empty());
        std::string current = currentPropertyValue.value_or(std::string{});
        if (ImGui::InputText(label, &current)) {
            out = current;
        }
        break;
    }
    case Type::Combo: {
        assert(!meta.enumValues.empty());
        std::string current = currentPropertyValue.value_or(std::string{});
        if (ImGui::BeginCombo(label, current.c_str())) {
            for (const auto& enumName : meta.enumValues) {
                const bool isCurrent = enumName == current;
                if (ImGui::Selectable(enumName.c_str(), isCurrent)) {
                    out = enumName;
                }
                if (isCurrent) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        break;
    }
    case Type::Checkbox: {
        bool temp = currentPropertyValue.value_or(false);
        if (ImGui::Checkbox(label, &temp)) {
            out = temp;
        }
        break;
    }
    case Type::Slider: {
        assert(meta.minValue && meta.maxValue);
        assert(currentPropertyValue.is_arithmetic());
        gr::pmt::ValueVisitor([&out, &meta, label]<typename T>(const T& num) {
            if constexpr (std::floating_point<T>) {
                double temp = num;
                if (ImGui::SliderScalar(label, ImGuiDataType_Double, &temp, &*meta.minValue, &*meta.maxValue, "%d", {})) {
                    out = static_cast<T>(temp);
                }
            } else if constexpr (std::integral<T>) {
                auto       temp     = static_cast<std::int64_t>(num);
                const auto minValue = static_cast<std::int64_t>(*meta.minValue);
                const auto maxValue = static_cast<std::int64_t>(*meta.maxValue);
                if (ImGui::SliderScalar(label, ImGuiDataType_S64, &temp, &minValue, &maxValue, "%d", {})) {
                    out = static_cast<T>(temp);
                }
            }
        }).visit(currentPropertyValue);
        break;
    }
    case Type::Keypad: {
        assert(!meta.minValue || !meta.maxValue);
        assert(currentPropertyValue.is_arithmetic());
        gr::pmt::ValueVisitor([&propertyName, label, &meta, &out]<typename T>(const T& num) {
            // sizeof(T) >= 4 is just here to reduce template instantiations of InputKeypad<>::edit
            if constexpr (std::is_arithmetic_v<T> && sizeof(T) >= 4) {
                T temp = num;
                if (InputKeypad<>::edit(propertyName, label, &temp, meta.unit)) {
                    out = static_cast<T>(temp);
                }
            } else {
                assert(false && "unexpected type for keypad edited property");
            }
        }).visit(currentPropertyValue);
        break;
    }
    }
    return out;
}

IMW::WidgetSize calcEditorSize(const char* label, const std::string& propertyName, const gr::pmt::Value& value, const UiGraphBlock::SettingsMetaInformation& meta) {
    using enum UiGraphBlock::SettingsControlType;
    switch (meta.controlType(propertyName, value)) {
    case Checkbox: return IMW::CalcCheckboxSize(label);
    case Color: return IMW::CalcColorEditorSize(label, colorFieldEditorFlags);
    // could calculate no. of digits needed by slider with logarithms, but it is okay to clip off big numbers
    case Slider: return IMW::CalcSliderSize(label, 5);
    case Keypad: {
        if (value.is_floating_point()) {
            return InputKeypad<>::calcSize<float>(label, meta.unit);
        } else if (value.is_integral()) {
            return InputKeypad<>::calcSize<std::uint32_t>(label, meta.unit);
        }
        return InputKeypad<>::calcSize<std::string>(label, meta.unit);
    }
    case Combo: return IMW::CalcComboSize(label, value.value_or(std::string{}).c_str(), 0);
    case TextInput: return IMW::CalcTextInputSize(label, value.value_or(std::string{}).c_str());
    }
    return {};
}

/// Function drawSettingRow() became too complex, so this had to be created to
/// split it in two. These are UI measurements which are shared between drawing
/// the edit widget on the left and the export and (+/-) button on the right.
struct BlockSettingRowExportButtonsParams {
    bool                                                 shouldWrapButtons{};
    bool                                                 isExported{};
    ImVec2                                               cursorStart{};
    float                                                regionAvailable{};
    float                                                exportButtonWidth{};
    float                                                assignButtonWidth{};
    float                                                spacing{};
    const std::string&                                   exportButtonLabel;
    const std::string&                                   assignButtonLabel;
    const std::string&                                   propertyKey;
    UiGraphBlock&                                        block;
    decltype(UiGraphBlock::exportedProperties)::iterator exportedPropertyIter{};
};

static BlockPropertyEditResult drawExportButtons(const BlockSettingRowExportButtonsParams& params) {
    if (!params.shouldWrapButtons) {
        ImGui::SameLine(0.f, 0.f);
    }
    ImGui::SetCursorPosX(params.cursorStart.x + params.regionAvailable - params.exportButtonWidth);
    if (ImGui::Button(params.exportButtonLabel.c_str())) {
        if (params.isExported) {
            params.block.exportedProperties.erase(params.exportedPropertyIter);
        } else {
            params.block.exportedProperties.try_emplace(params.propertyKey);
        }
    }
    ImGui::SameLine(0.f, 0.f);
    ImGui::SetCursorPosX(params.cursorStart.x + params.regionAvailable - (params.exportButtonWidth + params.assignButtonWidth + params.spacing));
    if (ImGui::Button(params.assignButtonLabel.c_str())) {
        // re-search in case user can press un-export and (+) at the same time and invalidated the iterator already
        const auto newExportedIter = params.block.exportedProperties.find(params.propertyKey);
        if (newExportedIter != std::end(params.block.exportedProperties) && newExportedIter->second.windowId.has_value()) {
            return BlockPropertyEditResult{
                .type     = BlockPropertyEditResult::Type::RemoveFromExistingWindow,
                .block    = std::addressof(params.block),
                .property = params.propertyKey,
            };
        } else {
            return BlockPropertyEditResult{
                .type     = BlockPropertyEditResult::Type::AddNewWindow,
                .block    = std::addressof(params.block),
                .property = params.propertyKey,
            };
        }
    }
    return BlockPropertyEditResult{};
}

/// Returns a value if the row was successfully drawn
static std::optional<BlockPropertyEditResult> drawSettingRow(const std::string& key, UiGraphBlock& block, const gr::pmt::Value& value, int rowIndex) {
    if (!value.is_string() && !value.is_floating_point() && !value.is_integral()) {
        return {}; // unsupported type
    }

    auto          id = ImGui::GetID(key.c_str());
    IMW::ChangeId rowId{int(id)};

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    auto metaInfoIter = block.blockSettingsMetaInformation.find(std::string(key));
    if (metaInfoIter == std::end(block.blockSettingsMetaInformation)) {
        assert(false);
        return {};
    }
    auto& metaInfo = metaInfoIter->second;
    ImGui::TextUnformatted(metaInfo.description.c_str());
    if (metaInfo.minValue && metaInfo.maxValue) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%.4g \x{e2}\x{80}\x{93} %.4g)", *metaInfo.minValue, *metaInfo.maxValue);
    }

    ImGui::TableSetColumnIndex(1);
    char label[64];
    auto labelResult = std::format_to_n(label, sizeof(label) - 1, "##parameter_{}", rowIndex);
    *labelResult.out = '\0';

    auto        exportedPropertyIter = block.exportedProperties.find(key);
    const bool  isExported           = exportedPropertyIter != block.exportedProperties.end();
    const char* visibleExportText    = isExported ? "Un-Export" : "Export";
    const auto  exportButtonLabel    = std::format("{}##{}", visibleExportText, key);

    const bool  isAssigned              = isExported && exportedPropertyIter->second.windowId.has_value();
    const char* visibleAssignButtonText = isAssigned ? "-" : "+";
    const auto  assignButtonLabel       = std::format("{}##assignButton{}", visibleAssignButtonText, key);

    BlockSettingRowExportButtonsParams params{
        .isExported           = isExported,
        .cursorStart          = ImGui::GetCursorPos(),
        .regionAvailable      = ImGui::GetContentRegionAvail().x,
        .exportButtonWidth    = IMW::CalcButtonSize(visibleExportText).x,
        .assignButtonWidth    = IMW::CalcButtonSize(visibleAssignButtonText).x,
        .spacing              = ImGui::GetStyle().ItemSpacing.x,
        .exportButtonLabel    = exportButtonLabel,
        .assignButtonLabel    = assignButtonLabel,
        .propertyKey          = key,
        .block                = block,
        .exportedPropertyIter = exportedPropertyIter,
    };

    const auto  editorMinWidth      = calcEditorSize(label, key, value, metaInfo).min.x;
    const float regionBeforeButtons = params.regionAvailable - params.exportButtonWidth - params.assignButtonWidth - (params.spacing * 2.f);
    params.shouldWrapButtons        = regionBeforeButtons < editorMinWidth,

    ImGui::SetNextItemWidth(std::max(1.f, params.shouldWrapButtons ? params.regionAvailable : regionBeforeButtons));
    if (auto newValue = editBlockProperty(label, key, value, metaInfo); newValue.has_value()) {
        block.setSetting(key, std::move(newValue));
    }

    if (ImGui::IsItemHovered()) {
        if (metaInfo.minValue && metaInfo.maxValue) {
            ImGui::SetTooltip("%s [%.4g \x{e2}\x{80}\x{93} %.4g]", key.c_str(), *metaInfo.minValue, *metaInfo.maxValue);
        } else {
            ImGui::SetTooltip("%s", key.c_str());
        }
    }

    return drawExportButtons(params);
}

BlockPropertyEditResult BlockSettingsControls(UiGraphBlock* block, const ImVec2& /*size*/) {
    InputKeypad<>::clearIfNewBlock(block->blockUniqueName);
    BlockPropertyEditResult result{};
    const auto              drawSettingsTable = [&](bool visibleOnly) {
        IMW::StyleColor rowBg(ImGuiCol_TableRowBgAlt, LookAndFeel::instance().palette().rowBgAlt);

        if (auto table = IMW::Table(visibleOnly ? "settings_visible" : "settings_more", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg, ImVec2(0, 0), 0.0f)) {
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
                if (auto rowResult = drawSettingRow(keyStr, *block, value, rowIndex)) {
                    rowIndex += 1;
                    if (*rowResult) {
                        result = std::move(*rowResult);
                    }
                }
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

    // reset-all button — delegates to GR4's Block::settings().resetDefaults()
    {
        IMW::Font iconFont(LookAndFeel::instance().fontIconsSolid);
        ImGui::TextUnformatted("\uf2ea");
    }
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    if (ImGui::SmallButton("Reset all") && block->ownerGraph) {
        gr::Message message;
        message.serviceName = block->blockUniqueName;
        message.endpoint    = gr::block::property::kResetDefaults;
        message.cmd         = gr::message::Command::Set;
        block->ownerGraph->sendMessage(std::move(message));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reset all settings to defaults");
    }
    return result;
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
        wantsSelectedBlockTab     = true;
    }
}

} // namespace DigitizerUi::components
