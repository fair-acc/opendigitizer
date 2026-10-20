#include "DataTypeStyle.hpp"

#include "../common/LookAndFeel.hpp"
#include "../utils/TransparentStringHash.hpp"

#include <imgui_internal.h>

#include <print>
#include <string>
#include <unordered_map>

using namespace std::string_literals;

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

bool drawPin(ImDrawList* drawList, ImVec2 pinPosition, ImVec2 pinSize, const std::string& type) {
    const auto& style = styleForDataType(type);

    std::uint32_t alphaClearMask = 0x00ffffff;
    std::uint32_t alphaSetMask   = 0xff000000;
    if (ImGui::GetStyle().Alpha < 0.9f) {
        alphaSetMask = static_cast<std::uint32_t>(ImGui::GetStyle().Alpha * 255);
        alphaSetMask <<= 3 * 8;
    }

    const auto color = (style.color & alphaClearMask) | alphaSetMask;
    drawList->AddRectFilled(pinPosition, pinPosition + pinSize, color, LookAndFeel::instance().flowgraph.portRounding);
    drawList->AddRect(pinPosition, pinPosition + pinSize, darkenOrLighten(color), LookAndFeel::instance().flowgraph.portRounding);
    ImGui::SetCursorPos(pinPosition);

    return ImGui::IsMouseHoveringRect(pinPosition, pinPosition + pinSize);
}

const DataTypeStyle& styleForDataType(std::string_view type) {
    using DataTypeStyleMap = std::unordered_map<std::string, DataTypeStyle, opendigitizer::TransparentStringHash, std::equal_to<>>;

    auto withDataSetColors = [](DataTypeStyleMap&& map) {
        DataTypeStyleMap result;
        while (map.begin() != map.end()) {
            auto it          = map.begin();
            auto datasetName = "gr::DataSet<"s + it->first + ">"s;

            result[datasetName]               = it->second;
            result[datasetName].datasetMarker = true;

            result.insert(map.extract(it));
        }

        return result;
    };

    static auto styleForDataTypeLight = withDataSetColors({
        {"float32"s, {rgbToImGuiABGR(0xF57C00)}}, //
        {"float64"s, {rgbToImGuiABGR(0x00BCD4)}}, //

        {"int8"s, {rgbToImGuiABGR(0xD500F9)}},                  //
        {"int16"s, {rgbToImGuiABGR(0xFFEB3B)}},                 //
        {"int32"s, {rgbToImGuiABGR(0x009688)}},                 //
        {"int64"s, {rgbToImGuiABGR(0xCDDC39)}},                 //
        {"uint8"s, {rgbToImGuiABGR(0xD500F9), true}},           //
        {"uint16"s, {rgbToImGuiABGR(0xFFEB3B), true}},          //
        {"uint32"s, {rgbToImGuiABGR(0x009688), true}},          //
        {"uint64"s, {rgbToImGuiABGR(0xCDDC39), true}},          //
                                                                //
        {"std::complex<float32>"s, {rgbToImGuiABGR(0x2196F3)}}, //
        {"std::complex<float64>"s, {rgbToImGuiABGR(0x795548)}}, //
                                                                //
        {"std::complex<int8>"s, {rgbToImGuiABGR(0x9C27B0)}},    //
        {"std::complex<int16>"s, {rgbToImGuiABGR(0xFFC107)}},   //
        {"std::complex<int32>"s, {rgbToImGuiABGR(0x4CAF50)}},   //
        {"std::complex<int64>"s, {rgbToImGuiABGR(0x8BC34A)}},   //

        {"gr::DataSet<float32>"s, {rgbToImGuiABGR(0xF57C00), false, true}}, //
        {"gr::DataSet<float64>"s, {rgbToImGuiABGR(0x00BCD4), false, true}}, //
                                                                            //
        {"gr::Message"s, {rgbToImGuiABGR(0xDBDBDB)}},                       //

        {"Bits"s, {rgbToImGuiABGR(0xEA80FC)}},          //
        {"BusConnection"s, {rgbToImGuiABGR(0xffffff)}}, //
        {"Wildcard"s, {rgbToImGuiABGR(0xffffff)}},      //
        {"Untyped"s, {rgbToImGuiABGR(0xffffff)}},       //
    });

    static auto styleForDataTypeDark = withDataSetColors({
        {"float32"s, {0xff0a83ff}}, //
        {"float64"s, {0xffff432b}}, //

        {"int8"s, {0xff2aff06}},         //
        {"int16"s, {0xff0014c4}},        //
        {"int32"s, {0xffff6977}},        //
        {"int64"s, {0xff3223c6}},        //
        {"uint8"s, {0xff2aff06, true}},  //
        {"uint16"s, {0xff0014c4, true}}, //
        {"uint32"s, {0xffff6977, true}}, //
        {"uint64"s, {0xff3223c6, true}}, //

        {"std::complex<float32>"s, {0xffde690c}}, //
        {"std::complex<float64>"s, {0xff86aab8}}, //

        {"std::complex<int8>"s, {0xff63d84f}},  //
        {"std::complex<int16>"s, {0xff003ef8}}, //
        {"std::complex<int32>"s, {0xffb350af}}, //
        {"std::complex<int64>"s, {0xff743cb5}}, //

        {"gr::DataSet<float64>"s, {0xffff432b}}, //
        {"gr::DataSet<float32>"s, {0xff0a83ff}}, //

        {"gr::Message"s, {0xff242424}}, //

        {"Bits"s, {0xff158003}},          //
        {"BusConnection"s, {0xff000000}}, //
        {"Wildcard"s, {0xff000000}},      //
        {"Untyped"s, {0xff000000}},       //

    });

    auto& map = LookAndFeel::instance().style == LookAndFeel::Style::Light ? styleForDataTypeLight : styleForDataTypeDark;
    auto  it  = map.find(type);
    if (it == map.cend()) {
        std::print("Warning: Color not defined for {}\n", type);
        static DataTypeStyle none{0x00000000};
        return none;
    } else {
        return it->second;
    }
}

} // namespace DigitizerUi
