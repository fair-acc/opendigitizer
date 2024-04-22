#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include "meta.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

#include <imgui.h>
#include <implot.h>
#include <limits>

namespace opendigitizer {

template<typename T>
struct ImPlotSink : public gr::Block<ImPlotSink<T>, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "Dear ImGui">> {
    gr::PortIn<T>                                                      in;
    uint32_t                                                           color = 0xff0000; ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    std::string                                                        signal_name;
    std::string                                                        signal_unit;
    float                                                              signal_min = std::numeric_limits<float>::lowest();
    float                                                              signal_max = std::numeric_limits<float>::max();

public:
    std::conditional_t<meta::is_dataset_v<T>, T, gr::HistoryBuffer<T>> data       = [] {
        if constexpr (meta::is_dataset_v<T>) {
            return T{};
        } else {
            return gr::HistoryBuffer<T>{ 65536 };
        }
    }();

    gr::work::Status processBulk(gr::ConsumableSpan auto &input) noexcept {
        if constexpr (meta::is_dataset_v<T>) {
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
            const auto &label = signal_name.empty() ? this->name.value : signal_name;
            if (data.empty()) {
                // Plot one single dummy value so that the sink shows up in the plot legend
                float v = 0;
                ImPlot::PlotLine(label.c_str(), &v, 1);
            } else {
                ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4((color << 8) | 0xff));
                ImPlot::HideNextItem(false, ImPlotCond_Always);
                const auto span = std::span(data.begin(), data.end());
                //  TODO should we limit this to the last N (N might be UI-dependent) samples?
                ImPlot::PlotLine(label.c_str(), span.data(), static_cast<int>(span.size()));
            }
        } else if constexpr (meta::is_dataset_v<T>) {
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

ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::ImPlotSink, in, color, signal_name, signal_unit, signal_min, signal_max);

auto registerImPlotSink = gr::registerBlock<opendigitizer::ImPlotSink, float, double, gr::DataSet<float>, gr::DataSet<double>>(gr::globalBlockRegistry());

#endif
