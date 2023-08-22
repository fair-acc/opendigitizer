#include "arithmetic_block.h"

template<typename T>
struct MathNode : public fair::graph::node<MathNode<T>> {
    fair::graph::IN<T>  in1{};
    fair::graph::IN<T>  in2{};

    fair::graph::OUT<T> out{};

    constexpr T
    process_one(T a, T b) {
        return a + b;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (MathNode<T>), in1, in2, out);

namespace DigitizerUi {

ArithmeticBlock::ArithmeticBlock(std::string_view name, BlockType *type)
    : Block(name, type->name, type) {
    MathNode<float> ss;
    ss.settings().update_active_parameters();
    m_parameters = ss.settings().get();
}

std::unique_ptr<fair::graph::node_model> ArithmeticBlock::createGraphNode() {
    return std::make_unique<fair::graph::node_wrapper<MathNode<float>>>();
}

// void ArithmeticBlock::processData() {
//     auto &in = inputs();
//
//     if (in[0].connections.empty() && in[1].connections.empty()) {
//         return; // nothing to do
//     }
//
//     auto                   cval        = std::get<float>(getParameterValue("constant"));
//     auto                   op          = std::get<std::string>(getParameterValue("op"));
//
//     std::span<const float> val0        = in[0].connections.empty() ? std::span<const float>{} : static_cast<DigitizerUi::Block::OutputPort *>(in[0].connections[0]->ports[0])->dataSet.asFloat32();
//     std::span<const float> val1        = in[1].connections.empty() ? std::span<const float>{} : static_cast<DigitizerUi::Block::OutputPort *>(in[1].connections[0]->ports[0])->dataSet.asFloat32();
//
//     bool                   val0biggest = val0.size() > val1.size();
//     auto                  &biggest     = val0biggest ? val0 : val1;
//     auto                  &other       = val0biggest ? val1 : val0;
//     m_data.resize(biggest.size());
//
//     if (op == "add") {
//         add(biggest, other, cval);
//     } else if (op == "mul") {
//         mul(biggest, other, cval);
//     } else if (op == "sub") {
//         sub(val0, val1, cval);
//     } else if (op == "div") {
//         div(val0, val1, cval);
//     }
//     outputs()[0].dataSet = m_data;
// }

void ArithmeticBlock::sub(std::span<const float> val0, std::span<const float> val1, float cval) {
    if (val0.empty()) {
        std::fill(m_data.begin(), m_data.end(), cval);
    } else {
        std::copy(val0.begin(), val0.end(), m_data.data()); // strengthened
    }

    if (val1.empty()) {
        std::for_each(m_data.begin(), m_data.end(), [=](auto &d) { d -= cval; });
    } else {
        for (size_t i = 0; i < val1.size(); i++) {
            m_data[i] -= val1[i];
        }
    }
}

void ArithmeticBlock::div(std::span<const float> val0, std::span<const float> val1, float cval) {
    if (val0.empty()) {
        std::fill(m_data.begin(), m_data.end(), cval);
    } else {
        std::copy(val0.begin(), val0.end(), m_data.data()); // strengthened
    }

    if (val1.empty()) {
        std::for_each(m_data.begin(), m_data.end(), [=](auto &d) { d /= cval; });
    } else {
        for (size_t i = 0; i < val1.size(); i++) {
            m_data[i] /= val1[i];
        }
    }
}

void ArithmeticBlock::add(std::span<const float> biggest, std::span<const float> other, float cval) {
    std::memcpy(m_data.data(), biggest.data(), biggest.size());
    if (other.empty()) {
        std::for_each(m_data.begin(), m_data.end(), [=](float &i) { i += cval; });
    } else {
        for (int i = 0; i < other.size(); ++i) {
            m_data[i] += other[i];
        }
    }
}
void ArithmeticBlock::mul(std::span<const float> biggest, std::span<const float> other, float cval) {
    std::memcpy(m_data.data(), biggest.data(), biggest.size());
    if (other.empty()) {
        std::for_each(m_data.begin(), m_data.end(), [=](float &i) { i *= cval; });
    } else {
        for (int i = 0; i < other.size(); ++i) {
            m_data[i] *= other[i];
        }
    }
}

void ArithmeticBlock::registerBlockType() {
    auto t         = std::make_unique<DigitizerUi::BlockType>("Arithmetic");
    t->createBlock = [t = t.get()](std::string_view name) {
        return std::make_unique<DigitizerUi::ArithmeticBlock>(name, t);
    };
    t->parameters.reserve(3);
    t->parameters.emplace_back(DigitizerUi::BlockType::Parameter{
            .id    = "op",
            .label = "operation",
            .impl{ DigitizerUi::BlockType::EnumParameter{
                    .size = 4,
                    .options{ "add", "sub", "mul", "div" },
                    .optionsLabels{ "add", "sub", "mul", "div" },
                    .defaultValue{ "add" } } } });
    t->parameters.emplace_back(DigitizerUi::BlockType::Parameter{
            .id    = "constant",
            .label = "constant input",
            .impl{ DigitizerUi::BlockType::NumberParameter<float>{
                    1.0f } } });

    t->inputs.resize(2);
    t->inputs[0].name = "in1";
    t->inputs[0].type = "float";

    t->inputs[1].name = "in2";
    t->inputs[1].type = "float";

    t->outputs.resize(1);
    t->outputs[0].name = "out";
    t->outputs[0].type = "float";

    BlockType::registry().addBlockType(std::move(t));
}

} // namespace DigitizerUi
