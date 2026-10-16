#ifndef OPENDIGITIZER_TEST_UICONTROLCOMPATIBILITYTARGET_HPP
#define OPENDIGITIZER_TEST_UICONTROLCOMPATIBILITYTARGET_HPP

#include <gnuradio-4.0/Block.hpp>

#include <cstdint>
#include <limits>

namespace opendigitizer::test {

struct UiControlCompatibilityTarget : gr::Block<UiControlCompatibilityTarget> {
    enum class Shape { Sin, Cos, Square };
    enum class Color { red, green, blue };

    using Description = gr::Doc<"testing block to exercise different types and limits, especially for UI control block connections">;

    gr::Annotated<float, "low range", gr::Limits<0.f, 1.f>>           low_range                           = 0.5f;
    gr::Annotated<float, "high range", gr::Limits<10.f, 20.f>>        high_range                          = 15.f;
    gr::Annotated<float, "overlapping range", gr::Limits<0.5f, 15.f>> overlapping_both_high_and_low_range = 5.f;
    double                                                            float64_number                      = 0.;
    std::int32_t                                                      int32_number                        = 0;
    Shape                                                             shape                               = Shape::Sin;
    Shape                                                             other_shape                         = Shape::Cos;
    Color                                                             color                               = Color::red;
    gr::Annotated<std::uint32_t, "color unit", gr::Unit<"Color">>     color_unit_number                   = 0xFF0000U;
    gr::Annotated<std::uint32_t, "rgb unit", gr::Unit<"RGB">>         rgb_unit_number                     = 0x00FF00U;
    gr::Annotated<std::uint32_t, "frequency", gr::Unit<"Hz">>         hz_unit_number                      = 0U;
    std::uint32_t                                                     no_unit_number                      = 0U;
    gr::Annotated<float, "float color", gr::Unit<"Color">>            color_unit_float                    = 0.f;

    GR_MAKE_REFLECTABLE(UiControlCompatibilityTarget, low_range, high_range, overlapping_both_high_and_low_range, float64_number, int32_number, shape, other_shape, color, color_unit_number, rgb_unit_number, hz_unit_number, no_unit_number, color_unit_float);

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }
};

} // namespace opendigitizer::test

#endif
