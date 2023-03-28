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

    auto *p0   = static_cast<DigitizerUi::Block::OutputPort *>(in[0].connections[0]->ports[0]);
    auto *p1   = static_cast<DigitizerUi::Block::OutputPort *>(in[1].connections[0]->ports[0]);
    auto  val0 = p0->dataSet.asFloat32();
    auto  val1 = p1->dataSet.asFloat32();

    if (val0.size() != val1.size()) {
        return;
    }

    m_data.resize(val0.size());
    memcpy(m_data.data(), val0.data(), m_data.size() * 4);
    for (int i = 0; i < m_data.size(); ++i) {
        m_data[i] += val1[i];
    }
    outputs()[0].dataSet = m_data;
}

} // namespace DigitizerUi
