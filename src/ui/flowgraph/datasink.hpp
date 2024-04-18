#ifndef DATASINK_H
#define DATASINK_H

#include <imgui.h>
#include <implot.h>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

#include "../flowgraph.hpp"

namespace DigitizerUi {

class DataSink final : public Block {
public:
    explicit DataSink(std::string_view name);

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

namespace opendigitizer {

template<typename T>
struct PlotSink : public gr::Block<opendigitizer::PlotSink<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "Dear ImGui">> {
    gr::PortIn<T> in;
    std::conditional_t<DigitizerUi::meta::is_dataset_v<T>, T, gr::HistoryBuffer<T>> data = [] {
        if constexpr (DigitizerUi::meta::is_dataset_v<T>) {
            return T{};
        } else {
            return gr::HistoryBuffer<T>{ 65536 };
        }
    }();

    gr::work::Status processBulk(gr::ConsumableSpan auto &input) noexcept {
        if constexpr (DigitizerUi::meta::is_dataset_v<T>) {
            data = input.back();
        } else {
            data.push_back_bulk(input);
        }
        std::ignore = input.consume(input.size());
        return gr::work::Status::OK;
    }

    gr::work::Status
    draw() noexcept {
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();
        if constexpr (std::is_floating_point_v<T>) { // PlotLine() doesn't support std::complex
            // TODO use signal_name property if set
            if (data.empty()) {
                // Plot one single dummy value so that the sink shows up in the plot legend
                float v = 0;
                ImPlot::PlotLine(this->name.value.c_str(), &v, 1);
            } else {
                const auto span = std::span(data.begin(), data.end());
                //  TODO should we limit this to the last N (N might be UI-dependent) samples?
                ImPlot::PlotLine(this->name.value.c_str(), span.data(), static_cast<int>(span.size()));
            }
        } else if constexpr (DigitizerUi::meta::is_dataset_v<T>) {
            if (data.extents.empty()) {
                return gr::work::Status::OK;
            }

            for (std::int32_t i = 0; i < data.extents[0]; ++i) {
                const auto n = data.extents[1];
                ImPlot::PlotLine(data.signal_names[static_cast<std::size_t>(i)].c_str(), data.signal_values.data() + n * i, n);
            }
        }
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::PlotSink, in);

#endif
