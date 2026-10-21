#ifndef OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP
#define OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP

#include "PortShapes.hpp"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace DigitizerUi {

struct DataTypeStyle {
    std::uint32_t     colorARGB;
    PortShapeFunction drawShape      = drawRectanglePort;
    PortShapeFunction drawShapeSolid = drawSolidRectanglePort;

    [[nodiscard]] std::uint32_t color() const; // ImGui ABGR
};

[[nodiscard]] const DataTypeStyle& styleForDataType(std::string_view type);
[[nodiscard]] std::uint32_t        darkenOrLighten(std::uint32_t color);
[[nodiscard]] std::uint32_t        withStyleAlpha(std::uint32_t color, float styleAlpha);
[[nodiscard]] float                pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight);
[[nodiscard]] bool                 drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& type); // true if hovered

// ax::NodeEditor::PinDrawCallback implementations. userData should be a const DataTypeStyle*
void drawPinForNodeEditor(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float alpha, void* userData);
void drawHoveredPinForNodeEditor(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float alpha, void* userData);

} // namespace DigitizerUi

#endif // OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP
