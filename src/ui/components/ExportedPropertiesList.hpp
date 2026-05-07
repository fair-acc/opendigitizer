#ifndef OPENDIGITIZER_UI_COMPONENTS_EXPORTED_PROPERTIES_LIST_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_EXPORTED_PROPERTIES_LIST_HPP_

#include "../GraphModel.hpp"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <string>

namespace DigitizerUi {
namespace components {
struct ExportedPropertyPair {
    std::string blockName;
    std::string propertyName;

    constexpr explicit operator bool() const { return !blockName.empty() || !propertyName.empty(); }
};

class ExportedPropertyList {
    // keep this across frames
    std::string          filter;
    ExportedPropertyPair contextMenuTarget;
    bool                 isContextMenuOpen = false;

    constexpr static const char* kContextMenuPopupID = "exportedPropertyListContextMenu##";

public:
    enum class Flags : std::uint8_t {
        None = 0x0,
        // properties are just selectable, buttons on the properties are not interactable
        SelectableProperties = 0x1,
        // enable drag and drop handle on the left hand side of properties
        DragDropHandle = 0x2,
        // add a "..." button on the right hand side, which opens the context menu
        ContextMenuButton = 0x4,
    };

    struct Params {
        Flags flags = {};
        // filter the displayed exported properties to only ones that match this type
        std::optional<UiGraphBlock::SettingsControlType> typeFilter;
        // items in this vector will not be interactable
        std::span<const ExportedPropertyPair> disabledList;
    };

    enum class Action {
        Selected,
        AddWindow,
        Unexport,
    };

    struct Result {
        Action               action = {};
        ExportedPropertyPair targetProperty;

        constexpr explicit operator bool() const { return bool(targetProperty); }
    };

    /// Draw the UI and send the necessary signals to modify properties.
    [[nodiscard]] Result draw(UiGraphModel& graphModel, const Params& parameters);

    static void drawPropertyUninteractive(const char* blockName, const char* propertyName, UiGraphBlock::SettingsControlType type, const gr::pmt::Value& currentPropertyValue);
    static void drawPropertyUninteractiveNoLabel(const char* id, UiGraphBlock::SettingsControlType type, const gr::pmt::Value& currentPropertyValue);

private:
    enum class ExportedPropertyAction {
        None,
        OpenContextMenu,
        SelectThis,
    };

    /// Draw a single property and return whether the user wants to select the property or open the context menu for this property
    [[nodiscard]] ExportedPropertyAction drawExportedProperty(UiGraphBlock* block, const std::string& exportedPropertyName, const ExportedPropertyList::Params& parameters);
};

constexpr ExportedPropertyList::Flags operator|(ExportedPropertyList::Flags a, ExportedPropertyList::Flags b) { //
    return static_cast<ExportedPropertyList::Flags>(std::to_underlying(a) | std::to_underlying(b));
}
constexpr bool operator&(ExportedPropertyList::Flags a, ExportedPropertyList::Flags b) { //
    return static_cast<bool>(std::to_underlying(a) & std::to_underlying(b));
}

struct ExportedPropertyDragDropPayload {
    static constexpr const char kType[] = "OD_EXPORTED_PROPERTY_DND";
    static_assert(sizeof(kType) < 32); // imgui internal buffer limit

    std::array<char, 256> propertyName = {};
    std::array<char, 256> blockName    = {};

    constexpr explicit operator ExportedPropertyPair() const {
        return {
            .blockName    = std::string{std::string_view(blockName.data(), strnlen(blockName.data(), blockName.size()))},
            .propertyName = std::string{std::string_view(propertyName.data(), strnlen(propertyName.data(), propertyName.size()))},
        };
    }

    /// Should be called within an if(IMW::DragDropSource()) { ... } block
    static void payloadSource(const std::string& block, const std::string& property);

    [[nodiscard]] static ExportedPropertyPair getCurrentPayload() {
        if (const auto* payload = ImGui::GetDragDropPayload(); payload && payload->Data) {
            return ExportedPropertyPair(*static_cast<ExportedPropertyDragDropPayload*>(payload->Data));
        }
        return {};
    }
};

} // namespace components
} // namespace DigitizerUi
#endif
