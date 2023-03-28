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

static BlockType *btype() {
    static auto *t = []() {
        static BlockType t("sink");
        t.inputs.resize(1);
        auto &in = t.inputs[0];
        in.name  = "in";
        in.type  = "";
        return &t;
    }();
    return t;
}

} // namespace

DataSink::DataSink(std::string_view name)
    : Block(name, "sink", btype())
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

} // namespace DigitizerUi
