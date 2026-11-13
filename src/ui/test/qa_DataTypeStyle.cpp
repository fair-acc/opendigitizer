#include "components/DataTypeStyle.hpp"

#include <gnuradio-4.0/Tag.hpp>

#include <boost/ut.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace {
[[nodiscard]] double relativeLuminance(std::uint32_t argb) {
    const auto linear = [](std::uint32_t channel) {
        const double c = static_cast<double>(channel & 0xffU) / 255.;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return (0.2126 * linear(argb >> 16)) + (0.7152 * linear(argb >> 8)) + (0.0722 * linear(argb));
}

[[nodiscard]] double contrastRatio(std::uint32_t a, std::uint32_t b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

constexpr std::array kTypedPorts{"float32", "float64", "complex<float32>", "complex<float64>", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "std::complex<int8>", "std::complex<int16>", "std::complex<int32>", "std::complex<int64>", "gr::Tensor<float32>", "gr::Tensor<float64>", "gr::DataSet<float32>", "gr::DataSet<float64>", "event", "Bits"};
} // namespace

const boost::ut::suite<"DataTypeStyle"> dataTypeStyleTests = [] {
    using namespace boost::ut;
    using DigitizerUi::styleForDataType;

    "every event-related type is drawn as the purple event diamond"_test = [] {
        const std::string eventPortType(gr::meta::type_name<gr::property_map_view>());
        for (const std::string_view type : {std::string_view(eventPortType), std::string_view("event"), std::string_view("gr::Message")}) {
            expect(DigitizerUi::isEventDataType(type)) << type;
            expect(eq(styleForDataType(type).colorARGB, 0xffAD55EEU)) << type;
            expect(styleForDataType(type).drawShape == &DigitizerUi::drawDiamond) << type;
        }
        expect(!DigitizerUi::isEventDataType("float32"));
    };

    "unknown and untyped ports share an opaque colour that no data type uses"_test = [] {
        const std::uint32_t untyped = styleForDataType("not a type").colorARGB;
        expect(eq(untyped >> 24U, 0xffU)) << "opaque";
        for (const std::string_view type : {"BusConnection", "Wildcard", "Untyped"}) {
            expect(eq(styleForDataType(type).colorARGB, untyped)) << type;
        }
        for (const std::string_view type : kTypedPorts) {
            expect(neq(styleForDataType(type).colorARGB, untyped)) << type;
        }
    };

    "the untyped colour stands out on the lightest and the darkest flow-graph background of every theme"_test = [] {
        constexpr double    kGraphicalObjectContrast = 3.; // WCAG 2.1 SC 1.4.11
        const std::uint32_t untyped                  = styleForDataType("not a type").colorARGB;
        for (const std::uint32_t background : {0xffFFFFFFU, 0xffEAEAEAU, 0xff202020U, 0xff393939U}) {
            expect(ge(contrastRatio(untyped, background), kGraphicalObjectContrast)) << std::format("{:08X}", background);
        }
    };
};

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
