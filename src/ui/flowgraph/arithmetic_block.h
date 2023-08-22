#pragma once

#include "../flowgraph.h"
#include <span>
#include <vector>

namespace DigitizerUi {

class ArithmeticBlock : public Block {
public:
    explicit ArithmeticBlock(std::string_view name, BlockType *type);
    static void        registerBlockType();

    // void               processData() override;
    std::unique_ptr<fair::graph::node_model> createGraphNode() final;
    void               sub(std::span<const float> val0, std::span<const float> val1, float cval);
    void               div(std::span<const float> val0, std::span<const float> val1, float cval);
    void               add(std::span<const float> biggest, std::span<const float> other, float cval);
    void               mul(std::span<const float> input1, std::span<const float> input2, float cin);

    std::vector<float> m_data;
};

} // namespace DigitizerUi
