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
    ImVec4                          color;

    void                            draw(bool visible) {
        if (!grBlock) {
            return;
        }
        if (visible) {
            grBlock->draw();
        } else {
            // Consume data to not block the flowgraph
            std::ignore = grBlock->work(std::numeric_limits<std::size_t>::max());
        }
    }

private:
    template<typename T>
    std::unique_ptr<gr::BlockModel> createNode();
    gr::BlockModel                 *grBlock = nullptr;
};

} // namespace DigitizerUi

#endif
