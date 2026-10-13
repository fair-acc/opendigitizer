#include "Block.hpp"
#include "BlockNeighboursPreview.hpp"

#include <algorithm>
#include <format>

#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <misc/cpp/imgui_stdlib.h>

#include "../GraphModel.hpp"
#include "../blocks/TargetMap.hpp"
#include "../common/LookAndFeel.hpp"
#include "../components/Dialog.hpp"
#include "Keypad.hpp"

using namespace std::string_literals;

namespace {
std::size_t countOtherBlocksWithProperty(DigitizerUi::UiGraphModel& model, const DigitizerUi::UiGraphBlock& block, const std::string& property) {
    std::size_t count = 0UZ;
    forEachBlockRecursive(model.rootBlock, [&](DigitizerUi::UiGraphBlock& candidate) {
        if (std::addressof(candidate) != std::addressof(block) && candidate.blockSettings.contains(property)) {
            ++count;
        }
    });
    return count;
}

/// find all UI control blocks and return a list of connections they may have to the given block's property
std::vector<DigitizerUi::components::UiControlConnection> uiControlConnectionsFor(DigitizerUi::UiGraphBlock& block, const std::string& property) {
    using namespace DigitizerUi;
    using namespace DigitizerUi::components;

    std::vector<UiControlConnection> connections;
    if (!block.ownerGraph) {
        return connections;
    }
    const auto propertyNamesToListOfUiControlConnections  = block.ownerGraph->uiControlledProperties(block);
    const auto uiControlConnectionsForOurPropertyIterator = propertyNamesToListOfUiControlConnections.find(property);
    if (uiControlConnectionsForOurPropertyIterator == propertyNamesToListOfUiControlConnections.end()) {
        return connections;
    }
    for (DigitizerUi::UiGraphBlock* control : uiControlConnectionsForOurPropertyIterator->second) {
        const auto targets = control->uiControlTargetMap();
        if (!targets) {
            continue;
        }
        for (const TargetEntry& entry : targets->entries()) {
            switch (entry.relationshipToTarget(block.blockName, property)) {
            case TargetRelationship::TargetingSpecifically:
                connections.push_back({
                    .controlUniqueName = control->blockUniqueName,
                    .controlName       = control->blockName,
                    .allBlocks         = false,
                    .keep              = true,
                });
                break;
            case TargetRelationship::TargetingViaGlob:
                connections.push_back({
                    .controlUniqueName = control->blockUniqueName,
                    .controlName       = control->blockName,
                    .allBlocks         = true,
                    .keep              = true,
                });
                break;
            case TargetRelationship::NotTargeting: break;
            }
        }
    }
    return connections;
}

} // namespace

namespace DigitizerUi::components {

bool ModifyUiConnectionsPopup::draw(UiGraphModel* graphModel) {
    UiGraphBlock* block = graphModel ? graphModel->recursiveFindBlockByUniqueName(this->blockUniqueName).block : nullptr;
    if (!block) {
        return false;
    }

    constexpr const char* dialogName = "Modify Connections";
    if (!std::exchange(this->opened, true)) {
        ImGui::OpenPopup(dialogName);
    }

    enum class Action { none, apply, cancel };
    Action action = Action::none;

    if (auto popup = IMW::ModalPopup(dialogName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            action = Action::cancel;
            ImGui::CloseCurrentPopup();
        }
        {
            const std::string globWarning = std::format("Disconnecting this connection will also disconnect {} other controls", countOtherBlocksWithProperty(*graphModel, *block, this->property));

            // reserve room for the warning under every glob connection, so the popup keeps its size when a warning appears
            const ImGuiStyle& style              = ImGui::GetStyle();
            const auto        nGlobConnections   = static_cast<float>(std::ranges::count(this->connections, true, &UiControlConnection::allBlocks));
            const float       listContentsHeight = (static_cast<float>(this->connections.size()) * ImGui::GetFrameHeightWithSpacing()) + (nGlobConnections * ImGui::GetTextLineHeightWithSpacing());
            const float       maximumListHeight  = ImGui::GetMainViewport()->Size.y * 0.5F;
            const ImVec2      listSize{
                std::max(380.F, ImGui::CalcTextSize(globWarning.c_str()).x + (style.WindowPadding.x * 2.F) + style.ScrollbarSize),
                std::clamp(listContentsHeight + (style.WindowPadding.y * 2.F), 160.F, std::max(160.F, maximumListHeight)),
            };

            IMW::Child list("##connections", listSize, true, 0);
            int        index = 0;
            for (auto& connection : this->connections) {
                ImGui::Checkbox(std::format("##connected{}", index++).c_str(), &connection.keep);
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::TextUnformatted(connection.controlName.c_str());
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                {
                    IMW::Font icon(LookAndFeel::instance().fontIconsSolid);
                    ImGui::TextUnformatted("\uf061"); // arrow pointing from the control to its selector
                }
                ImGui::SameLine(0.F, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::Text("\"%s:%s\"", connection.allBlocks ? "*" : block->blockName.c_str(), this->property.c_str());
                if (!connection.keep && connection.allBlocks) {
                    ImGui::TextDisabled("%s", globWarning.c_str());
                }
            }
        }
        const float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.F;
        if (ImGui::Button("Done", ImVec2(halfWidth, 0.F))) {
            action = Action::apply;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(halfWidth, 0.F))) {
            action = Action::cancel;
            ImGui::CloseCurrentPopup();
        }
    }

    if (action == Action::apply && graphModel) {
        for (const auto& connection : this->connections) {
            if (!connection.keep) {
                graphModel->removeUiControlConnection(connection.controlUniqueName, connection.allBlocks ? std::string_view("*") : std::string_view(block->blockName), this->property);
            }
        }
    }
    if (action != Action::none || (this->opened && !ImGui::IsPopupOpen(dialogName))) {
        return false;
    }
    return true;
}

bool JumpToControlPopup::draw(UiGraphModel* graphModel, std::function<void(const std::string&)> focusBlockCallback) {
    UiGraphBlock* block = graphModel ? graphModel->recursiveFindBlockByUniqueName(this->blockUniqueName).block : nullptr;
    if (!block) {
        return false;
    }

    constexpr const char* popupName = "Jump to Control";
    if (!std::exchange(this->opened, true)) {
        ImGui::OpenPopup(popupName);
    }

    if (auto popup = IMW::Popup(popupName, 0)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::TextUnformatted(popupName);
        ImGui::Separator();
        {
            IMW::Child list("##controls", ImVec2(240.F, 140.F), true, 0);
            const auto controlled    = graphModel->uiControlledProperties(*block);
            const auto controllersIt = controlled.find(this->property);
            if (controllersIt != controlled.end()) {
                for (UiGraphBlock* control : controllersIt->second) {
                    // using a button instead of a selectable will prevent the selection from closing the popup
                    if (ImGui::Button(control->blockName.c_str(), ImVec2{0.f, ImGui::GetFrameHeightWithSpacing()}) && focusBlockCallback) {
                        focusBlockCallback(control->blockUniqueName);
                    }
                }
            }
        }
        const float doneWidth = 100.F;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.F, (ImGui::GetContentRegionAvail().x - doneWidth) / 2.F));
        if (ImGui::Button("Done", ImVec2(doneWidth, 0.F))) {
            ImGui::CloseCurrentPopup();
        }
    }
    if (this->opened && !ImGui::IsPopupOpen(popupName)) {
        return false;
    }
    return true;
}

std::optional<ModifyUiConnectionsPopup> unlinkUiControlledProperty(UiGraphBlock& block, const std::string& property) {
    auto connections = uiControlConnectionsFor(block, property);
    if (connections.size() == 1UZ && !connections.front().allBlocks) {
        block.ownerGraph->removeUiControlConnection(connections.front().controlUniqueName, block.blockName, property);
        return std::nullopt;
    }
    if (connections.empty()) {
        return std::nullopt;
    }
    return ModifyUiConnectionsPopup{.blockUniqueName = block.blockUniqueName, .property = property, .connections = std::move(connections)};
}

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

        propertyEditResult = BlockSettingsControls(panelContext, block);

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

    // don't close the panel while the mouse is hovering it, edits are made, or one of its dialogs is open
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || InputKeypad<>::isVisible() || panelContext.modifyConnectionsDialog || panelContext.jumpToControlPopup) {
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

    if (panelContext.modifyConnectionsDialog && !panelContext.modifyConnectionsDialog->draw(panelContext.graphModel)) {
        panelContext.modifyConnectionsDialog.reset();
    }
    if (panelContext.jumpToControlPopup && !panelContext.jumpToControlPopup->draw(panelContext.graphModel, panelContext.focusBlockRequest)) {
        panelContext.jumpToControlPopup.reset();
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
                auto temp = static_cast<double>(num);
                if (ImGui::SliderScalar(label, ImGuiDataType_Double, &temp, &*meta.minValue, &*meta.maxValue, "%g", {})) {
                    out = static_cast<T>(temp);
                }
            } else if constexpr (std::integral<T>) {
                auto       temp     = static_cast<std::int64_t>(num);
                const auto minValue = static_cast<std::int64_t>(*meta.minValue);
                const auto maxValue = static_cast<std::int64_t>(*meta.maxValue);
                if (ImGui::SliderScalar(label, ImGuiDataType_S64, &temp, &minValue, &maxValue, "%lld", {})) {
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

/// Drawn in place of the editor of a property which is controlled by a UI control block
static void drawUiControlledPropertyActions(BlockControlsPanelContext& panelContext, UiGraphBlock& block, const std::string& key) {
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    {
        IMW::Font iconFont(LookAndFeel::instance().fontIconsSolid);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("\u{f023}"); // lock
    }
    ImGui::SameLine(0.f, spacing);
    if (ImGui::Button(std::format("Unlink##unlink_{}", key).c_str())) {
        panelContext.modifyConnectionsDialog = unlinkUiControlledProperty(block, key);
    }
    ImGui::SameLine(0.f, spacing);
    if (ImGui::Button(std::format("Jump to control##jumpToControl_{}", key).c_str())) {
        const auto                         connections = uiControlConnectionsFor(block, key);
        std::set<std::string, std::less<>> uniqueControls;
        for (const auto& connection : connections) {
            uniqueControls.insert(connection.controlUniqueName);
        }
        if (uniqueControls.size() == 1UZ) {
            if (panelContext.focusBlockRequest) {
                panelContext.focusBlockRequest(*uniqueControls.begin());
            }
        } else if (!uniqueControls.empty()) {
            panelContext.jumpToControlPopup = JumpToControlPopup{.blockUniqueName = block.blockUniqueName, .property = key};
        }
    }
}

/// Returns a value if the row was successfully drawn
static std::optional<BlockPropertyEditResult> drawSettingRow(BlockControlsPanelContext& panelContext, const std::string& key, UiGraphBlock& block, const gr::pmt::Value& value, bool uiControlled) {
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
    auto labelResult = std::format_to_n(label, sizeof(label) - 1, "##parameter_{}", key);
    *labelResult.out = '\0';

    ImGui::BeginGroup();
    if (uiControlled) {
        drawUiControlledPropertyActions(panelContext, block, key);
    } else {
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x));
        if (auto newValue = editBlockProperty(label, key, value, metaInfo); newValue.has_value()) {
            block.setSetting(key, std::move(newValue));
        }
    }
    ImGui::EndGroup();

    if (ImGui::IsItemHovered()) {
        if (metaInfo.minValue && metaInfo.maxValue) {
            ImGui::SetTooltip("%s [%.4g \x{e2}\x{80}\x{93} %.4g]", key.c_str(), *metaInfo.minValue, *metaInfo.maxValue);
        } else {
            ImGui::SetTooltip("%s", key.c_str());
        }
    }

    return BlockPropertyEditResult{};
}

BlockPropertyEditResult BlockSettingsControls(BlockControlsPanelContext& panelContext, UiGraphBlock* block, const ImVec2& /*size*/) {
    InputKeypad<>::clearIfNewBlock(block->blockUniqueName);
    BlockPropertyEditResult result{};
    const auto              controlledProperties = block->ownerGraph ? block->ownerGraph->uiControlledProperties(*block) : UiGraphModel::ControlledPropertyMap{};
    // don't allow editing properties which are being edited by a UI control block
    const auto isPropertyLocked = [&controlledProperties](const std::string& key) {
        const auto controls = controlledProperties.find(key);
        return controls != controlledProperties.end() && std::ranges::any_of(controls->second, [](const UiGraphBlock* control) { return !control->isViewOnlyUiControl(); });
    };
    const auto drawSettingsTable = [&](bool visibleOnly) {
        IMW::StyleColor rowBg(ImGuiCol_TableRowBgAlt, LookAndFeel::instance().palette().rowBgAlt);

        if (auto table = IMW::Table(visibleOnly ? "settings_visible" : "settings_more", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg, ImVec2(0, 0), 0.0f)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

            for (const auto& [key, value] : block->blockSettings) {
                std::string keyStr(key);
                auto        metaIt   = block->blockSettingsMetaInformation.find(keyStr);
                bool        isMarked = metaIt != block->blockSettingsMetaInformation.end() && metaIt->second.isVisible;
                if (isMarked != visibleOnly) {
                    continue;
                }
                if (auto rowResult = drawSettingRow(panelContext, keyStr, *block, value, isPropertyLocked(keyStr)); rowResult && *rowResult) {
                    result = std::move(*rowResult);
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
