#ifndef DATASINK_H
#define DATASINK_H

#include <imgui.h>

#include <gnuradio-4.0/Block.hpp>

#include "../flowgraph.hpp"

namespace DigitizerUi {

class DataSink final : public Block {
public:
    explicit DataSink(std::string_view name);

    std::unique_ptr<gr::BlockModel> createGRBlock() override;
    static void                     registerBlockType();

    void                            update();

    DataType                        dataType;
    DataSet                         data;

    ImVec4                          color;

private:
    template<typename T>
    std::unique_ptr<gr::BlockModel> createNode();

    std::function<void()>           updaterFun;
};

} // namespace DigitizerUi

namespace opendigitizer {

template<typename T>
struct PlotSink : public gr::Block<opendigitizer::PlotSink<T>, gr::Drawable<gr::UICategory::ChartPane, "Dear ImGui">> {
    gr::PortIn<T> in;

    void          processOne(T value) {
        // TODO handle tags
        dataWriter.publish([&](auto &out) { out[0] = value; }, 1);
    }

    using BufferType                       = gr::CircularBuffer<T>;
    std::shared_ptr<BufferType> dataBuffer = std::make_shared<BufferType>(200000);

    gr::work::Status
    draw() noexcept {
        return gr::work::Status::OK;
    }

private:
    decltype(dataBuffer->new_writer()) dataWriter = dataBuffer->new_writer();
};

template<typename T>
struct DSSink : public gr::Block<opendigitizer::DSSink<T>, gr::Drawable<gr::UICategory::ChartPane, "Dear ImGui">> {
    gr::PortIn<gr::DataSet<T>> in;

    void                       processOne(gr::DataSet<T> ds) {
        writer.publish([&](auto &out) { out[0] = ds; }, 1);
    }

    using BufferType                          = gr::CircularBuffer<gr::DataSet<T>>;
    std::shared_ptr<BufferType> dataSetBuffer = std::make_shared<BufferType>(1024);

    gr::work::Status
    draw() noexcept {
        return gr::work::Status::OK;
    }

private:
    decltype(dataSetBuffer->new_writer()) writer = dataSetBuffer->new_writer();
};

} // namespace opendigitizer

ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::PlotSink, in);
ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::DSSink, in);

#endif
