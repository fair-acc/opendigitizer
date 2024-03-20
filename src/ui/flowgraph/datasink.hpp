#ifndef DATASINK_H
#define DATASINK_H

#include <imgui.h>
#include <mutex>

#include "../flowgraph.hpp"

namespace DigitizerUi {

class DataSink final : public Block {
public:
    explicit DataSink(std::string_view name);

    std::unique_ptr<gr::BlockModel> createGraphNode() final;
    static void                     registerBlockType();

    void                            update();

    bool                            hasData = false;
    DataType                        dataType;
    DataSet                         data;

    std::mutex                      m_mutex;

    ImVec4                          color;

private:
    template<typename T>
    std::unique_ptr<gr::BlockModel> createNode();

    std::function<void()>           updaterFun;
};

class DataSinkSource final : public Block {
public:
    explicit DataSinkSource(std::string_view name);

    std::unique_ptr<gr::BlockModel> createGraphNode() final;
    void                            setup(gr::Graph &graph) final;

    static void                     registerBlockType();

private:
};

} // namespace DigitizerUi

#endif
