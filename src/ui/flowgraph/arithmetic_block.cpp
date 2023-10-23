#include "arithmetic_block.h"

#include <type_traits>

#include "../flowgraph.h"

template<typename T>
    requires std::is_arithmetic_v<T>
struct MathNode : public gr::Block<MathNode<T>> {
    gr::PortIn<T>                           in1{};
    gr::PortIn<T>                           in2{};

    gr::PortOut<T>                          out{};

    gr::Annotated<std::string, "operation"> operation = std::string("+");

    constexpr T
    processOne(T a, T b) {
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

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (MathNode<T>), in1, in2, out, operation);

namespace DigitizerUi {

void ArithmeticBlock::registerBlockType() {
    BlockType::registry().addBlockType<MathNode>("Arithmetic");
}

} // namespace DigitizerUi
