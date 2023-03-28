#ifndef SUMBLOCK_H
#define SUMBLOCK_H

#include <vector>

#include "../flowgraph.h"

namespace DigitizerUi {

class SumBlock : public Block {
public:
    explicit SumBlock(std::string_view name, BlockType *type);
    void               processData() override;

    std::vector<float> m_data;
};

} // namespace DigitizerUi

#endif
