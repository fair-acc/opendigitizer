#ifndef OPENDIGITIZER_TOUNCERTAINVALUE_HPP
#define OPENDIGITIZER_TOUNCERTAINVALUE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/meta/UncertainValue.hpp>

namespace gr::od {

template<typename T>
struct ToUncertainValue : gr::Block<ToUncertainValue<T>> {
    using Description = gr::Doc<"Pairs each value with its standard deviation, e.g. a moving mean with a moving standard deviation, as the UncertainValue a chart draws with error bars or an error surface.">;

    gr::PortIn<T>                      value;
    gr::PortIn<T>                      uncertainty;
    gr::PortOut<gr::UncertainValue<T>> out;

    GR_MAKE_REFLECTABLE(ToUncertainValue, value, uncertainty, out);

    [[nodiscard]] constexpr gr::UncertainValue<T> processOne(T mean, T standardDeviation) const noexcept { return {mean, standardDeviation}; }
};

} // namespace gr::od

#endif // OPENDIGITIZER_TOUNCERTAINVALUE_HPP
