#include "datasink.h"

namespace DigitizerUi {

namespace {

template<typename T>
inline T randomRange(T min, T max) {
    T scale = rand() / (T) RAND_MAX;
    return min + scale * (max - min);
}

ImVec4 randomColor() {
    ImVec4 col;
    col.x = randomRange(0.0f, 1.0f);
    col.y = randomRange(0.0f, 1.0f);
    col.z = randomRange(0.0f, 1.0f);
    col.w = 1.0f;
    return col;
}

BlockType *g_btype       = nullptr;
BlockType *g_btypeSource = nullptr;

} // namespace

DataSink::DataSink(std::string_view name)
    : Block(name, "sink", g_btype)
    , color(randomColor()) {
}

void DataSink::processData() {
    const auto &in = inputs()[0];
    if (!in.connections.empty()) {
        hasData  = true;
        auto *c  = in.connections[0];
        dataType = c->ports[0]->type;
        data     = static_cast<Block::OutputPort *>(c->ports[0])->dataSet;
    } else {
        hasData = false;
    }
}

void DataSink::registerBlockType(FlowGraph *fg) {
    auto t = std::make_unique<BlockType>("sink");
    t->inputs.resize(1);
    auto &in       = t->inputs[0];
    in.name        = "in";
    in.type        = "";
    t->createBlock = [](std::string_view name) {
        return std::make_unique<DataSink>(name);
    };
    g_btype = t.get();

    fg->addBlockType(std::move(t));
}

DataSinkSource::DataSinkSource(std::string_view name)
    : Block(name, "sink_source", g_btypeSource) {
}

void DataSinkSource::processData() {
    auto &out      = outputs()[0];
    auto  sinkName = std::string_view(&name[11], &name[name.size()]);
    auto *sink     = static_cast<DataSink *>(flowGraph()->findSinkBlock(sinkName));
    out.dataSet    = sink->data;
    out.type       = sink->dataType;
}

void DataSinkSource::registerBlockType(FlowGraph *fg) {
    auto t = std::make_unique<BlockType>("sink_source");
    t->outputs.resize(1);
    auto &out      = t->outputs[0];
    out.name       = "out";
    out.type       = "";
    t->createBlock = [](std::string_view name) {
        return std::make_unique<DataSinkSource>(name);
    };
    g_btypeSource = t.get();

    fg->addBlockType(std::move(t));
}

} // namespace DigitizerUi
