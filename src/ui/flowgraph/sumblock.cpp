#include "sumblock.h"

namespace DigitizerUi {

SumBlock::SumBlock(std::string_view name, BlockType *type)
    : Block(name, type->name, type) {
}

void SumBlock::processData() {
    auto &in = inputs();
    if (in[0].connections.empty() || in[1].connections.empty()) {
        return;
    }

    auto *p0          = static_cast<DigitizerUi::Block::OutputPort *>(in[0].connections[0]->ports[0]);
    auto *p1          = static_cast<DigitizerUi::Block::OutputPort *>(in[1].connections[0]->ports[0]);
    auto  val0        = p0->dataSet.asFloat32();
    auto  val1        = p1->dataSet.asFloat32();

    bool  val0biggest = val0.size() > val1.size();
    auto &biggest     = val0biggest ? val0 : val1;
    auto &other       = val0biggest ? val1 : val0;

    m_data.resize(std::max(val0.size(), val1.size()));

    memcpy(m_data.data(), biggest.data(), m_data.size() * 4);
    for (int i = 0; i < other.size(); ++i) {
        m_data[i] += other[i];
    }
    outputs()[0].dataSet = m_data;
}

} // namespace DigitizerUi
