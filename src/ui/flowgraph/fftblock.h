#ifndef FFTBLOCK_H
#define FFTBLOCK_H

#include <vector>

#include "../flowgraph.h"

namespace DigitizerUi {

class FFTBlock : public Block {
public:
    explicit FFTBlock(std::string_view name, BlockType *type);
    // void               processData() override;
    std::unique_ptr<fair::graph::node_model> createGraphNode() final;

    std::vector<float> m_data;
};

} // namespace DigitizerUi

#endif
