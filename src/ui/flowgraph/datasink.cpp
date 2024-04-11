#include "datasink.hpp"

#include <gnuradio-4.0/HistoryBuffer.hpp>

#include <fmt/format.h>

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

BlockType *g_btype = nullptr;

} // namespace

DataSink::DataSink(std::string_view name)
    : Block(name, "opendigitizer::DataSink", g_btype)
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
        using Sink   = opendigitizer::DSSink<typename T::value_type>;
        auto wrapper = std::make_unique<gr::BlockWrapper<Sink>>();
        auto sink    = static_cast<Sink *>(wrapper->raw());
        auto reader  = std::make_shared<decltype(sink->dataSetBuffer->new_reader())>(sink->dataSetBuffer->new_reader());
        updaterFun   = [this, r = reader]() mutable {
            auto d = r->get(r->available());
            if (d.empty()) {
                return;
            }
            data        = d.back();
            std::ignore = d.consume(d.size());
        };
        return wrapper;
    } else {
        using Sink   = opendigitizer::PlotSink<T>;
        auto wrapper = std::make_unique<gr::BlockWrapper<Sink>>();
        auto sink    = static_cast<Sink *>(wrapper->raw());
        auto reader  = std::make_shared<decltype(sink->dataBuffer->new_reader())>(sink->dataBuffer->new_reader());

        // TODO this should depend on plot range
        constexpr auto kChunkSize = 65536UZ;

        updaterFun                = [this, r = reader, buffer = gr::HistoryBuffer<T>(kChunkSize)]() mutable {
            const auto available = r->available();
            auto       d         = r->get(available);
            buffer.push_back_bulk(d.begin(), d.end());
            std::ignore = d.consume(d.size());
            // TODO avoid copy
            data = std::vector<T>(buffer.begin(), buffer.end());
        };

        return wrapper;
    }
}

std::unique_ptr<gr::BlockModel> DataSink::createGRBlock() {
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
