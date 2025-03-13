#ifndef OPENDIGITIZER_ARITHMETIC_HPP
#define OPENDIGITIZER_ARITHMETIC_HPP

#include <gnuradio-4.0/Block.hpp>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::Arithmetic, [ float, double ]);

template<typename T>
requires std::is_arithmetic_v<T>
struct Arithmetic : gr::Block<Arithmetic<T>> {
    gr::PortIn<T> in1;
    gr::PortIn<T> in2;

    gr::PortOut<T> out;

    gr::Annotated<std::string, "operation"> operation = std::string("+");

    GR_MAKE_REFLECTABLE(Arithmetic, in1, in2, out, operation);

    constexpr T processOne(T a, T b) {
        auto setting = this->settings().get("operation");
        if (setting.has_value()) {
            auto op = setting.value();
            if (op == "+") {
                return a + b;
            } else if (op == "-") {
                return a - b;
            } else if (op == "*") {
                return a * b;
            } else if (op == "/") {
                return a / b;
            }
        }
        return a + b;
    }
};

} // namespace opendigitizer

auto registerArithmetic = gr::registerBlock<opendigitizer::Arithmetic, float>(gr::globalBlockRegistry());

#endif
