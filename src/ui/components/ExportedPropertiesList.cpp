#include "ExportedPropertiesList.hpp"
#include "Block.hpp"

#include "../common/LookAndFeel.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace DigitizerUi::components {
void ExportedPropertyDragDropPayload::payloadSource(const std::string& block, const std::string& property) {
    ExportedPropertyDragDropPayload payload{};
    if (property.size() > sizeof(payload.propertyName) - 1) {
        std::println("WARNING: property name {} too large for drag and drop payload", property);
    }
    if (block.size() > sizeof(payload.blockName) - 1) {
        std::println("WARNING: block name {} too large for drag and drop payload", block);
    }
    using diff_t = decltype(property.begin() - property.end());

    const auto propertyLen = std::min(property.size(), sizeof(payload.propertyName) - 1);
    std::ranges::copy_n(property.begin(), static_cast<diff_t>(propertyLen), payload.propertyName.begin());

    const auto blockLen = std::min(block.size(), sizeof(payload.blockName) - 1);
    std::ranges::copy_n(block.begin(), static_cast<diff_t>(blockLen), payload.blockName.begin());

    ImGui::SetDragDropPayload(kType, &payload, sizeof(payload));
}

/// The goal of this function is to draw a block property without +/- increment
/// operators for numbers, dropdown symbols for combo boxes, or text focus for
/// text inputs, etc.
void ExportedPropertyList::drawPropertyUninteractive(const char* blockName, const char* propertyName, UiGraphBlock::SettingsControlType type, const gr::pmt::Value& currentPropertyValue) {
    const auto id = std::format("{}{}##", blockName ? blockName : "unknown", propertyName ? propertyName : "unknown");
    drawPropertyUninteractiveNoLabel(id.c_str(), type, currentPropertyValue);
    // still have preview of color before color properties
    if (blockName) {
        ImGui::SameLine();
        ImGui::TextUnformatted(blockName, ImGui::FindRenderedTextEnd(blockName));
    }
    if (propertyName) {
        ImGui::SameLine();
        ImGui::TextUnformatted(propertyName, ImGui::FindRenderedTextEnd(propertyName));
    }
}

void ExportedPropertyList::drawPropertyUninteractiveNoLabel(const char* id, UiGraphBlock::SettingsControlType type, const gr::pmt::Value& currentPropertyValue) {
    gr::pmt::ValueVisitor([type, id]<typename T>(const T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            bool dummy = value;
            ImGui::Checkbox(id, &dummy);
            return;
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, std::pmr::string>) {
            ImGui::TextUnformatted(std::string{value}.c_str());
        } else if constexpr (std::is_floating_point_v<T>) {
            ImGui::Text("%f", static_cast<double>(value));
        } else if constexpr (std::is_integral_v<T>) {
            if constexpr (std::unsigned_integral<T> && sizeof(T) >= 4) {
                if (type == UiGraphBlock::SettingsControlType::Color) {
                    // draw the color button but ignore interaction with it
                    std::ignore = ImGui::ColorButton(id, ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(static_cast<std::uint32_t>(value))));
                    return;
                }
            }
            ImGui::Text("%lld", static_cast<long long>(value));
        }
    }).visit(currentPropertyValue);
}

enum class ValidExportedPropertyResult {
    DrawnButNoAction,
    Filtered,
    OpenContextMenu,
};

/// Actually draws the exported property, assumes iterators are valid
static ValidExportedPropertyResult drawValidExportedProperty(                       //
    UiGraphBlock*                                                  block,           //
    bool                                                           isDisabled,      //
    decltype(UiGraphBlock::blockSettings)::iterator                settingIterator, //
    decltype(UiGraphBlock::blockSettingsMetaInformation)::iterator metaIterator,    //
    const ExportedPropertyList::Params&                            parameters       //
) {
    const gr::pmt::Value& current               = settingIterator->second;
    const auto&           meta                  = metaIterator->second;
    const auto            propertyUiControlType = meta.controlType(settingIterator->first, current);

    // filter properties not matching type
    if (parameters.typeFilter && parameters.typeFilter != propertyUiControlType) {
        return ValidExportedPropertyResult::Filtered;
    }

    IMW::Group property;

    IMW::ChangeStrId id(std::format("{}{}", block->blockName, settingIterator->first).c_str());

    // draw drag and drop button
    if (parameters.flags & ExportedPropertyList::Flags::DragDropHandle) {
        {
            IMW::Font font(LookAndFeel::instance().fontIconsSolidLarge);
            ImGui::Button("\u{f58e}");
        }
        ImGui::SetItemTooltip("Drag to control window...");

        if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            ExportedPropertyDragDropPayload::payloadSource(block->blockName, std::string{settingIterator->first});
            ImGui::Text("Property \"%s\"...", settingIterator->first.c_str());
        }
        ImGui::SameLine();
    }

    constexpr const char* moreOptionsLabel      = "...";
    const auto            moreOptionsButtonSize = IMW::CalcButtonSize(moreOptionsLabel);

    if (parameters.flags & ExportedPropertyList::Flags::SelectableProperties) {
        // in this case you can't interact with the controls of the property, so avoid drawing ones that don't add information.
        // passing in nullptr for blockname since we are in a section which is already labeled with the blockname
        ExportedPropertyList::drawPropertyUninteractive(nullptr, settingIterator->first.c_str(), propertyUiControlType, current);
    } else {
        IMW::Disabled disabled(isDisabled);
        const auto    labelSize = ImGui::CalcTextSize(settingIterator->first.c_str());
        ImGui::SetNextItemWidth(-(labelSize + moreOptionsButtonSize + ImGui::GetStyle().ItemSpacing * 2.f).x);
        if (auto newValue = editBlockProperty(settingIterator->first.c_str(), std::string{settingIterator->first}, current, meta)) {
            block->setSetting(settingIterator->first, std::move(newValue));
        }
    }

    if (parameters.flags & ExportedPropertyList::Flags::ContextMenuButton) {
        ImGui::SameLine();
        const auto avail = ImGui::GetContentRegionAvail().x;
        if (avail > moreOptionsButtonSize.x) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - moreOptionsButtonSize.x));
        }
        if (ImGui::Button(moreOptionsLabel)) {
            return ValidExportedPropertyResult::OpenContextMenu;
        }
    }
    return ValidExportedPropertyResult::DrawnButNoAction;
}

ExportedPropertyList::ExportedPropertyAction ExportedPropertyList::drawExportedProperty(UiGraphBlock* block, const std::string& exportedPropertyName, const ExportedPropertyList::Params& parameters) {
    auto       action         = ExportedPropertyAction::None;
    const auto matchPredicate = [block, &exportedPropertyName](const ExportedPropertyPair& pair) { //
        return pair.blockName == block->blockName && pair.propertyName == exportedPropertyName;
    };
    const bool isDisabled = std::ranges::find_if(parameters.disabledList, matchPredicate) != std::end(parameters.disabledList);
    // filter properties which do not have the filter string, if it is present
    static constexpr auto stringToLowercase = [](std::string_view string) { //
        return string | std::views::transform([](unsigned char c) { return std::tolower(c); }) | std::ranges::to<std::string>();
    };
    static constexpr auto has = [](const std::string& a, const auto& pattern) { //
        return stringToLowercase(a).find(stringToLowercase(pattern)) != std::string::npos;
    };
    if (!filter.empty() && !has(exportedPropertyName, filter) && !has(block->blockName, filter) && !has(block->blockUniqueName, filter)) {
        return ExportedPropertyAction::None;
    }

    auto propertyIter = block->blockSettings.find(exportedPropertyName);
    auto metaIter     = block->blockSettingsMetaInformation.find(exportedPropertyName);
    if (propertyIter == std::end(block->blockSettings) || metaIter == std::end(block->blockSettingsMetaInformation)) {
        return ExportedPropertyAction::None;
    }

    using enum ValidExportedPropertyResult;
    switch (drawValidExportedProperty(block, isDisabled, propertyIter, metaIter, parameters)) {
    case DrawnButNoAction: break;
    case Filtered: return ExportedPropertyAction::None; break;
    case OpenContextMenu: action = ExportedPropertyAction::OpenContextMenu; break;
    }

    if (!isDisabled && (parameters.flags & Flags::SelectableProperties)) {
        if (ImGui::IsItemHovered()) {
            ImGui::GetCurrentWindow()->DrawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), rgbToImGuiABGR(0x63e69f, 0x99));
        }
        // create a button over the previous item
        ImGui::SetCursorScreenPos(ImGui::GetItemRectMin());
        if (ImGui::InvisibleButton(propertyIter->first.c_str(), ImGui::GetItemRectSize())) {
            action = ExportedPropertyAction::SelectThis;
        }
    } else {
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            action = ExportedPropertyAction::OpenContextMenu;
        }
    }
    return action;
}

ExportedPropertyList::Result ExportedPropertyList::draw(UiGraphModel& graphModel, const Params& parameters) {
    {
        IMW::Font       font(LookAndFeel::instance().fontIconsSolidLarge);
        constexpr auto* searchLabel = "\u{f002}";
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (ImGui::CalcTextSize(searchLabel).x + ImGui::GetStyle().ItemInnerSpacing.x));
        ImGui::InputText(searchLabel, std::addressof(this->filter));
    }

    IMW::Child child("propertiesScrollable", ImVec2{}, 0, 0);

    bool                 openContextMenu = false;
    ExportedPropertyPair selected{};

    const auto allExportedPropertiesByBlockName = graphModel.recursiveGatherExportedProperties();
    for (const auto& [blockName, exportedPropertiesPtr] : allExportedPropertiesByBlockName) {
        auto* block = graphModel.recursiveFindBlockByName(blockName).block;
        if (!block) {
            continue;
        }

        if (ImGui::TreeNode(block->blockUniqueName.c_str(), "%s", block->blockName.c_str())) {
            for (const auto& [exportedPropertyName, _] : *exportedPropertiesPtr) {
                using enum ExportedPropertyAction;
                switch (this->drawExportedProperty(block, exportedPropertyName, parameters)) {
                case None: break;
                case SelectThis: {
                    selected = {.blockName = block->blockName, .propertyName = exportedPropertyName};
                } break;
                case OpenContextMenu: {
                    this->contextMenuTarget = {.blockName = block->blockName, .propertyName = exportedPropertyName};
                    openContextMenu         = true;
                } break;
                }
            }
            ImGui::TreePop();
        }
    }

    if (allExportedPropertiesByBlockName.empty()) {
        constexpr auto* emptyLabel = "No properties yet exported...";
        const auto      desired    = ImGui::CalcTextSize(emptyLabel).x;
        const auto      available  = ImGui::GetContentRegionAvail().x;
        if (desired < available) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - desired) / 2.f);
        }
        ImGui::TextUnformatted(emptyLabel);
    }

    if (openContextMenu) {
        ImGui::OpenPopup(ExportedPropertyList::kContextMenuPopupID);
        isContextMenuOpen = true;
    }

    if (auto popup = IMW::Popup(ExportedPropertyList::kContextMenuPopupID, 0)) {
        Result     out;
        const bool addWindow = ImGui::Button("New Window");
        const bool unexport  = ImGui::Button("Un-export");
        if (addWindow) {
            out.action         = Action::AddWindow;
            out.targetProperty = std::move(this->contextMenuTarget);
            ImGui::CloseCurrentPopup();
        } else if (unexport) {
            out.action         = Action::Unexport;
            out.targetProperty = std::move(this->contextMenuTarget);
            ImGui::CloseCurrentPopup();
        }
        return out;
    }

    return {
        .action         = Action::Selected,
        .targetProperty = std::move(selected),
    };
}
} // namespace DigitizerUi::components
