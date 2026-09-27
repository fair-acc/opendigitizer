#ifndef OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP
#define OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace DigitizerUi {

struct DataTypeStyle {
    std::uint32_t color;
    bool          unsignedMarker = false;
    bool          datasetMarker  = false;
};

[[nodiscard]] const DataTypeStyle& styleForDataType(std::string_view type);
[[nodiscard]] std::uint32_t        darkenOrLighten(std::uint32_t color);
[[nodiscard]] float                pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight);
[[nodiscard]] bool                 drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& type); // true if hovered

} // namespace DigitizerUi

#endif // OPENDIGITIZER_COMPONENTS_DATATYPESTYLE_HPP
