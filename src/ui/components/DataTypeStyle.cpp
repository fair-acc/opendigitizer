#include "DataTypeStyle.hpp"

#include "../common/LookAndFeel.hpp"
#include "../utils/TransparentStringHash.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <print>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace DigitizerUi {

uint32_t darkenOrLighten(uint32_t color) {
    if (LookAndFeel::instance().style == LookAndFeel::Style::Light) {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int shift = 8 * i;
            r |= uint32_t(((color >> shift) & 0xff) * 0.5) << shift;
        }
        return r;
    } else {
        uint32_t r = color & 0xff000000;
        for (int i = 0; i < 3; ++i) {
            int      shift   = 8 * i;
            uint32_t channel = (color >> shift) & 0xff;
            channel          = 0xff - ((0xff - channel) / 2);
            r |= channel << shift;
        }
        return r;
    }
}

float pinLocalPositionY(std::size_t index, std::size_t numPins, float blockHeight, float pinHeight) {
    const float spacing = blockHeight / (static_cast<float>(numPins) + 1);
    // ImFloor here is to mimic what imgui node editor is doing internally, so our rectangles line up with the highlight rects they draw
    return ImFloor(spacing * (static_cast<float>(index) + 1) - (pinHeight / 2));
}

std::uint32_t withStyleAlpha(std::uint32_t color, float styleAlpha) {
    constexpr std::uint32_t kAlphaClearMask = 0x00ffffff;
    const std::uint32_t     alpha           = styleAlpha < 0.9f ? static_cast<std::uint32_t>(styleAlpha * 255) : 0xff;
    return (color & kAlphaClearMask) | (alpha << 24);
}

bool drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& type) {
    const auto& style = styleForDataType(type);
    style.drawShape(drawList, pinPosition + pinSize * 0.5f, std::min(pinSize.x, pinSize.y), withStyleAlpha(style.color(), ImGui::GetStyle().Alpha));
    ImGui::SetCursorPos(pinPosition);

    return ImGui::IsMouseHoveringRect(pinPosition, pinPosition + pinSize);
}

void drawPinForNodeEditor(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float alpha, void* userData) {
    const auto& style = *static_cast<const DataTypeStyle*>(userData);
    style.drawShape(drawList, (min + max) * 0.5f, std::min(max.x - min.x, max.y - min.y), withStyleAlpha(style.color(), alpha));
}

void drawHoveredPinForNodeEditor(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float /*alpha*/, void* userData) {
    const auto& style = *static_cast<const DataTypeStyle*>(userData);
    style.drawShapeSolid(drawList, (min + max) * 0.5f, std::min(max.x - min.x, max.y - min.y), LookAndFeel::getColorU32ImGui(&Palette::flowgraphPinRect));
}

std::uint32_t DataTypeStyle::color() const { return rgbToImGuiABGR(colorARGB & 0x00ffffff, static_cast<std::uint8_t>(colorARGB >> 24)); }

const DataTypeStyle& styleForDataType(std::string_view type) {
    using DataTypeStyleMap = std::unordered_map<std::string, DataTypeStyle, opendigitizer::TransparentStringHash, std::equal_to<>>;

    // If there is a style for T, then there should also be a matching style for DataSet<T> unless another one is already specified
    auto withDataSetStyles = [](DataTypeStyleMap map) {
        const std::vector<std::pair<std::string, DataTypeStyle>> explicitStyles(map.begin(), map.end());
        for (const auto& [name, style] : explicitStyles) {
            if (!name.starts_with("gr::DataSet<"sv)) {
                map.try_emplace("gr::DataSet<"s + name + ">"s, style);
            }
        }
        return map;
    };

    static const auto styles = withDataSetStyles({
        {"float32"s, {0xffF57C00, &drawDot, &drawSolidDotPort}},                        //
        {"float64"s, {0xff00BCD4, &drawRingedDot, &drawSolidDotPort}},                  //
                                                                                        //
        {"int8"s, {0xffD500F9, &drawTriangle, &drawSolidTrianglePort}},                 //
        {"int16"s, {0xffFFEB3B, &drawSquare, &drawSolidSquarePort}},                    //
        {"int32"s, {0xff009688, &drawPentagon, &drawSolidPentagonPort}},                //
        {"int64"s, {0xffCDDC39, &drawHexagon, &drawSolidHexagonPort}},                  //
        {"uint8"s, {0xffD500F9, &drawHollowTriangle, &drawSolidTrianglePort}},          //
        {"uint16"s, {0xffFFEB3B, &drawHollowSquare, &drawSolidSquarePort}},             //
        {"uint32"s, {0xff009688, &drawHollowPentagon, &drawSolidPentagonPort}},         //
        {"uint64"s, {0xffCDDC39, &drawHollowHexagon, &drawSolidHexagonPort}},           //
                                                                                        //
        {"complex<float32>"s, {0xff2196F3, &drawDotPair, &drawSolidDotPairPort}},       //
        {"complex<float64>"s, {0xff795548, &drawRingedDotPair, &drawSolidDotPairPort}}, //
                                                                                        //
        {"std::complex<int8>"s, {0xff9C27B0}},                                          //
        {"std::complex<int16>"s, {0xffFFC107}},                                         //
        {"std::complex<int32>"s, {0xff4CAF50}},                                         //
        {"std::complex<int64>"s, {0xff8BC34A}},                                         //

        {"gr::Tensor<float32>"s, {0xffFF6905, &drawGridWithCornerRing, &drawSolidCornerRingPort}},         //
        {"gr::Tensor<float64>"s, {0xff009DA8, &drawGridWithCornerDoubleRing, &drawSolidCornerRingPort}},   //
        {"gr::DataSet<float32>"s, {0xffF50057, &drawWavesWithCornerRing, &drawSolidCornerRingPort}},       //
        {"gr::DataSet<float64>"s, {0xff8BC34A, &drawWavesWithCornerDoubleRing, &drawSolidCornerRingPort}}, //
                                                                                                           //
        {"event"s, {0xffAD55EE, &drawDiamond, &drawSolidDiamondPort}},                                     //
        {"gr::Message"s, {0xffDBDBDB}},                                                                    //

        {"Bits"s, {0xffEA80FC}},          //
        {"BusConnection"s, {0xffffffff}}, //
        {"Wildcard"s, {0xffffffff}},      //
        {"Untyped"s, {0xffffffff}},       //
    });

    auto it = styles.find(type);
    if (it == styles.cend()) {
        static const DataTypeStyle none{0x00000000};
        return none;
    }
    return it->second;
}

} // namespace DigitizerUi
