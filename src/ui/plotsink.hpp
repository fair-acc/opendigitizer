#ifndef PLOTSINK_H
#define PLOTSINK_H

#include <imgui.h>

#include "flowgraph.hpp"

#include <gnuradio-4.0/Graph.hpp>

namespace DigitizerUi {

class PlotSink final : public Block {
public:
    explicit PlotSink(std::string_view name);

    std::unique_ptr<gr::BlockModel> createGRBlock() override;
    static void                     registerBlockType();
    gr::BlockModel                 *grBlock = nullptr;

private:
    template<typename T>
    std::unique_ptr<gr::BlockModel> createNode();
};

} // namespace DigitizerUi

#endif
