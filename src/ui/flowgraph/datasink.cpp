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
            auto d = p->reader.get();
            vec.resize(d.size());
            std::copy(d.rbegin(), d.rend(), vec.begin());

            data = vec;
        };
        return wrapper;
    }
}

std::unique_ptr<gr::BlockModel> DataSink::createGraphNode() {
    auto *c = inputs()[0].connections[0];
    if (!c) {
        return nullptr;
    }

    auto type = c->src.block->outputs()[c->src.index].type;
    /*
        if (type == DataType::DataSetFloat32) {
            using Sink = DSSink<float>;
            auto wrapper = std::make_unique<gr::node_wrapper<Sink>>();

            dataType = DataType::DataSetFloat32;
            data = EmptyDataSet{};
            auto *node = static_cast<Sink *>(wrapper->raw());

            updaterFun = [this, node]() mutable {
                data = node->dataset;
            };
            return wrapper;
        }*/

    return type.asType([this]<typename T>() {
        return this->template createNode<T>();
    });
}

// void DataSink::processData() {
//     const auto &in = inputs()[0];
//     if (!in.connections.empty()) {
//         hasData  = true;
//         auto *c  = in.connections[0];
//         dataType = c->ports[0]->type;
//         data     = static_cast<Block::OutputPort *>(c->ports[0])->dataSet;
//     } else {
//         hasData = false;
//     }
// }

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

// void DataSinkSource::processData() {
//     auto &out      = outputs()[0];
//     auto  sinkName = std::string_view(&name[11], &name[name.size()]);
//     auto *sink     = static_cast<DataSink *>(flowGraph()->findSinkBlock(sinkName));
//     out.dataSet    = sink->data;
//     out.type       = sink->dataType;
// }

void DataSinkSource::registerBlockType() {
    auto t = std::make_unique<BlockType>("sink_source", "Sink Source", "", true);
    t->outputs.resize(1);
    auto &out = t->outputs[0];
    out.name  = "out";
    out.type  = "";
    // t->createBlock = [](std::string_view name) {
    //     return std::make_unique<DataSinkSource>(name);
    // };
    g_btypeSource = t.get();

    BlockType::registry().addBlockType(std::move(t));
}

} // namespace DigitizerUi
