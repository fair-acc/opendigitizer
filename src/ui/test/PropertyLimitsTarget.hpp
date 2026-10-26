#ifndef OPENDIGITIZER_TEST_PROPERTYLIMITSTARGET_HPP
#define OPENDIGITIZER_TEST_PROPERTYLIMITSTARGET_HPP

#include <gnuradio-4.0/Block.hpp>

#include <cstdint>
#include <limits>

namespace opendigitizer::test {

struct DoubleToNumericLimitTarget : gr::Block<DoubleToNumericLimitTarget> {
    using Description = gr::Doc<"block to test having extremely large numerical limits for doubles">;

    gr::PortIn<float>  in;
    gr::PortOut<float> out;

    gr::Annotated<double, "double up to numeric limit", gr::Visible, gr::Limits<0., std::numeric_limits<double>::max()>> value = 1.;

    GR_MAKE_REFLECTABLE(DoubleToNumericLimitTarget, in, out, value);

    [[nodiscard]] constexpr float processOne(float input) const noexcept { return input; }
};

struct Int64ToNumericLimitTarget : gr::Block<Int64ToNumericLimitTarget> {
    using Description = gr::Doc<"block with extremely large int64 limits to exercise conversion to and from double representation for limits">;

    gr::PortIn<float>  in;
    gr::PortOut<float> out;

    gr::Annotated<std::int64_t, "positive int64", gr::Visible, gr::Limits<std::int64_t{0}, std::numeric_limits<std::int64_t>::max()>> value = 1;

    GR_MAKE_REFLECTABLE(Int64ToNumericLimitTarget, in, out, value);

    [[nodiscard]] constexpr float processOne(float input) const noexcept { return input; }
};

struct UnlimitedDoubleTarget : gr::Block<UnlimitedDoubleTarget> {
    using Description = gr::Doc<"testing block with a double setting without limits">;

    gr::PortIn<float>  in;
    gr::PortOut<float> out;

    gr::Annotated<double, "double value", gr::Visible> value = 1.;

    GR_MAKE_REFLECTABLE(UnlimitedDoubleTarget, in, out, value);

    [[nodiscard]] constexpr float processOne(float input) const noexcept { return input; }
};

} // namespace opendigitizer::test

#endif
