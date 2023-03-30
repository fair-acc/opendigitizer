#include "datasource.h"

namespace DigitizerUi {

namespace {
BlockType *g_blockType = nullptr;
}

DataSource::DataSource(std::string_view name)
    : Block(name, "sine_source", g_blockType) {
    m_data.resize(8192);

    processData();
}

void DataSource::processData() {
    float freq = std::get<NumberParameter<float>>(parameters()[0]).value;
    for (int i = 0; i < m_data.size(); ++i) {
        m_data[i] = std::sin((m_offset + i) * freq);
    }
    outputs()[0].dataSet = m_data;
    m_offset += 1;
}

void DataSource::registerBlockType(FlowGraph *fg) {
    auto t = std::make_unique<BlockType>("sine_source");
    t->outputs.resize(1);
    t->outputs[0].name = "out";
    t->outputs[0].type = "float";
    t->parameters.push_back({ "frequency", "frequency", BlockType::NumberParameter<float>(0.1) });
    t->createBlock = [](std::string_view n) {
        static int created = 0;
        ++created;
        if (n.empty()) {
            std::string name = fmt::format("sine source {}", created);
            return std::make_unique<DataSource>(name);
        }
        return std::make_unique<DataSource>(n);
    };
    g_blockType = t.get();

    fg->addBlockType(std::move(t));
}

} // namespace DigitizerUi
