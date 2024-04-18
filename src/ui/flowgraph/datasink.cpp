#include "datasink.hpp"

#include <gnuradio-4.0/HistoryBuffer.hpp>

#include <fmt/format.h>

namespace DigitizerUi {

namespace {

template<typename T>
inline T randomRange(T min, T max) {
    T scale = static_cast<T>(rand()) / static_cast<T>(RAND_MAX);
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

BlockType *g_btype = nullptr;

} // namespace

DataSink::DataSink(std::string_view name_)
    : Block(name_, "opendigitizer::DataSink", g_btype)
    , color(randomColor()) {
}

template<typename T>
std::unique_ptr<gr::BlockModel> DataSink::createNode() {
    return std::make_unique<gr::BlockWrapper<opendigitizer::PlotSink<T>>>();
}

std::unique_ptr<gr::BlockModel> DataSink::createGRBlock() {
    if (inputs()[0].connections.empty()) {
        grBlock = nullptr;
        return nullptr;
    }

    auto *c    = inputs()[0].connections[0];
    auto  outType = c->src.block->outputs()[c->src.index].type;

    auto  block   = outType.asType([this]<typename T>() {
        return this->template createNode<T>();
    });
    grBlock     = block.get();
    return block;
}

void DataSink::registerBlockType() {
    auto t = std::make_unique<BlockType>("opendigitizer::DataSink");
    t->inputs.resize(1);
    auto &in       = t->inputs[0];
    in.name        = "in";
    in.type        = "";
    t->createBlock = [](std::string_view name) {
        return std::make_unique<DataSink>(name);
    };
    g_btype = t.get();

    BlockType::registry().addBlockType(std::move(t));
}

} // namespace DigitizerUi
