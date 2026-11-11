#ifndef OPENDIGITIZER_ARITHMETIC_HPP
#define OPENDIGITIZER_ARITHMETIC_HPP

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::Arithmetic, [ float, double ]);

template<typename T>
requires std::is_arithmetic_v<T>
struct Arithmetic : gr::Block<Arithmetic<T>> {
    using Description = gr::Doc<"Combines two streams sample by sample with `operation`: +, -, * or /.">;

    enum class Operation : std::uint8_t { add, subtract, multiply, divide };
    static constexpr std::array<std::string_view, 4> kOperationSymbols{"+", "-", "*", "/"};
    static_assert(std::to_underlying(Operation::divide) + 1U == kOperationSymbols.size());

    gr::PortIn<T> in1;
    gr::PortIn<T> in2;

    gr::PortOut<T> out;

    gr::Annotated<std::string, "operation", gr::Doc<"+, -, * or /">> operation = std::string("+");

    GR_MAKE_REFLECTABLE(Arithmetic, in1, in2, out, operation);

    Operation _operation = Operation::add;

    [[nodiscard]] constexpr T processOne(T a, T b) const noexcept {
        switch (_operation) {
        case Operation::add: return a + b;
        case Operation::subtract: return a - b;
        case Operation::multiply: return a * b;
        case Operation::divide: return a / b;
        }
        std::unreachable();
    }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (!newSettings.contains("operation")) {
            return;
        }
        const auto symbol = std::ranges::find(kOperationSymbols, std::string_view(operation.value));
        if (symbol == kOperationSymbols.end()) {
            const std::string_view kept = kOperationSymbols[std::to_underlying(_operation)];
            this->emitErrorMessage("Arithmetic::settingsChanged()", gr::Error(std::format("operation '{}': use +, -, * or /; keeping '{}'", operation.value, kept)));
            operation = std::string(kept);
            return;
        }
        _operation = static_cast<Operation>(symbol - kOperationSymbols.begin());
    }
};

} // namespace opendigitizer

inline const auto kArithmeticRegistered = gr::registerBlock<opendigitizer::Arithmetic, float, double>(gr::globalBlockRegistry());

#endif
