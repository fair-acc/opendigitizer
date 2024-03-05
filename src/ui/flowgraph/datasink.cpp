#include "datasink.h"

#include <fmt/format.h>
#include <gnuradio-4.0/basic/DataSink.hpp>

template<typename T>
struct DSSink : gr::Block<DSSink<T>> {
    gr::PortIn<gr::DataSet<T>> in;

    void                       processOne(gr::DataSet<T> ds) {
        dataset = ds;
    }

    gr::DataSet<T> dataset;
};
ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (DSSink<T>), in);

template<typename T>
struct DSSinkSource : gr::Block<DSSinkSource<T>> {
    // This is just a forwarding block
    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    T              processOne(T ds) {
        return ds;
    }
};
ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (DSSinkSource<T>), in, out);

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

void DataSink::update() {
    if (updaterFun) {
        updaterFun();
    }
}

template<typename T>
std::unique_ptr<gr::BlockModel> DataSink::createNode() {
    dataType = DataType::of<T>();
    data     = EmptyDataSet{};

    if constexpr (meta::is_dataset_v<T>) {
        using Sink    = DSSink<typename T::value_type>;
        auto  wrapper = std::make_unique<gr::BlockWrapper<Sink>>();

        auto *node    = static_cast<Sink *>(wrapper->raw());
        updaterFun    = [this, node]() mutable {
            data = node->dataset;
        };
        return wrapper;
    } else {
        using Sink    = gr::basic::DataSink<T>;
        auto  wrapper = std::make_unique<gr::BlockWrapper<Sink>>();

        auto *sink    = static_cast<Sink *>(wrapper->raw());
        auto  p       = sink->getStreamingPoller(gr::basic::BlockingMode::NonBlocking);
        updaterFun    = [this, p, vec = std::vector<T>()]() mutable {
            auto d = p->reader.get(p->reader.available());
            vec.resize(d.size());
            std::copy(d.rbegin(), d.rend(), vec.begin());

            data = vec;
        };
        return wrapper;
    }
}

std::unique_ptr<gr::BlockModel> DataSink::createGraphNode() {
    if (inputs()[0].connections.empty()) {
        return nullptr;
    }

    auto *c    = inputs()[0].connections[0];
    auto  type = c->src.block->outputs()[c->src.index].type;

    return type.asType([this]<typename T>() {
        return this->template createNode<T>();
    });
}

void DataSink::registerBlockType() {
    auto t = std::make_unique<BlockType>("sink");
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

DataSinkSource::DataSinkSource(std::string_view name)
    : Block(name, "sink_source", g_btypeSource) {
}

std::unique_ptr<gr::BlockModel> DataSinkSource::createGraphNode() {
    auto  sinkName = std::string_view(&name[11], &name[name.size()]);
    auto *sink     = static_cast<DataSink *>(flowGraph()->findSinkBlock(sinkName));
    if (!sink) {
        fmt::print("{} no sink\n", name);
        return nullptr;
    }

    if (sink->inputs()[0].connections.empty()) {
        fmt::print("{} no conn\n", name);
        return nullptr;
    }

    auto *c           = sink->inputs()[0].connections[0];
    auto  type        = c->src.block->outputs()[c->src.index].type;

    outputs()[0].type = type;

    return type.asType([]<typename T>() -> std::unique_ptr<gr::BlockModel> {
        return std::make_unique<gr::BlockWrapper<DSSinkSource<T>>>();
    });
}

void DataSinkSource::setup(gr::Graph &graph) {
    auto  sinkName = std::string_view(&name[11], &name[name.size()]);
    auto *sink     = static_cast<DataSink *>(flowGraph()->findSinkBlock(sinkName));
    if (!sink) {
        fmt::print("no sink\n");
        return;
    }

    if (sink->inputs()[0].connections.empty()) {
        fmt::print("{} no conn\n", name);
        return;
    }

    auto *c = sink->inputs()[0].connections[0];
    if (c->src.block->graphNode() && graphNode()) {
        graph.connect(*c->src.block->graphNode(), c->src.index, *graphNode(), 0);
    }
}

void DataSinkSource::registerBlockType() {
    auto t = std::make_unique<BlockType>("sink_source", "Sink Source", "", true);
    t->outputs.resize(1);
    auto &out      = t->outputs[0];
    out.name       = "out";
    out.type       = "";
    t->createBlock = [](std::string_view name) {
        return std::make_unique<DataSinkSource>(name);
    };
    g_btypeSource = t.get();

    BlockType::registry().addBlockType(std::move(t));
}

} // namespace DigitizerUi
