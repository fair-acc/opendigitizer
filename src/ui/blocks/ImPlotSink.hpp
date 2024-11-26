#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <limits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

#include "../common/ImguiWrap.hpp"

#include <implot.h>

#include "meta.hpp"

namespace opendigitizer {

struct ImPlotSinkManager {
private:
    ImPlotSinkManager() {}
    ImPlotSinkManager(const ImPlotSinkManager&)            = delete;
    ImPlotSinkManager& operator=(const ImPlotSinkManager&) = delete;

    struct SinkModel {
        std::string uniqueName;
        SinkModel(std::string uniqueName) : uniqueName(std::move(uniqueName)) {}

        virtual ~SinkModel() {}
        virtual gr::work::Status draw() noexcept = 0;
    };

    std::unordered_map<std::string, std::unique_ptr<SinkModel>> _knownSinks;

    template<typename Block>
    struct SinkWrapper : SinkModel {
        SinkWrapper(Block* block) : SinkModel(block->unique_name) {}
        gr::work::Status draw() noexcept override { return block->draw(); }

        Block* block;
    };

public:
    static ImPlotSinkManager& instance() {
        static ImPlotSinkManager s_instance;
        return s_instance;
    }

    template<typename TBlock>
    void registerPlotSink(TBlock* block) {
        _knownSinks[block->unique_name] = std::make_unique<SinkWrapper<TBlock>>(block);
    }

    template<typename TBlock>
    void unregisterPlotSink(TBlock* block) {
        _knownSinks.erase(block->unique_name);
    }
};

template<typename TBlock>
using ImPlotSinkBase = gr::Block<TBlock, gr::BlockingIO<false>, gr::SupportedTypes<float, double>, gr::Drawable<gr::UICategory::ChartPane, "Dear ImGui">>;

template<typename T>
struct ImPlotSink : public ImPlotSinkBase<ImPlotSink<T>> {
    gr::PortIn<T> in;
    uint32_t      color = 0xff0000; ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    std::string   signal_name;
    std::string   signal_unit;
    float         signal_min = std::numeric_limits<float>::lowest();
    float         signal_max = std::numeric_limits<float>::max();

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, signal_name, signal_unit, signal_min, signal_max);

public:
    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    gr::HistoryBuffer<T> data = gr::HistoryBuffer<T>{65536};

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        data.push_back_bulk(input);
        std::ignore = input.consume(input.size());
        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();
        const auto&                             label  = signal_name.empty() ? this->name.value : signal_name;
        if (data.empty()) {
            // Plot one single dummy value so that the sink shows up in the plot legend
            T v = {};
            ImPlot::PlotLine(label.c_str(), &v, 1);
        } else {
            // ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4((color << 8) | 0xff));
            // ImPlot::HideNextItem(false, ImPlotCond_Always);
            const auto span = std::span(data.begin(), data.end());
            //  TODO should we limit this to the last N (N might be UI-dependent) samples?
            ImPlot::PlotLine(label.c_str(), span.data(), static_cast<int>(span.size()));
        }

        return gr::work::Status::OK;
    }
};

template<typename T>
struct ImPlotSinkDataSet : public ImPlotSinkBase<ImPlotSinkDataSet<T>> {
    gr::PortIn<gr::DataSet<T>> in;
    uint32_t                   color = 0xff0000; ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    std::string                signal_name;
    std::string                signal_unit;
    float                      signal_min    = std::numeric_limits<float>::lowest();
    float                      signal_max    = std::numeric_limits<float>::max();
    gr::Size_t                 dataset_index = std::numeric_limits<gr::Size_t>::max();

    GR_MAKE_REFLECTABLE(ImPlotSinkDataSet, in, color, signal_name, signal_unit, signal_min, signal_max, dataset_index);

public:
    ImPlotSinkDataSet(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSinkDataSet<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSinkDataSet() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    gr::DataSet<T> data{};

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        data        = input.back();
        std::ignore = input.consume(input.size());
        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();
        if (data.extents.empty()) {
            return gr::work::Status::OK;
        }
        const auto n = data.extents[1];
        if (dataset_index == std::numeric_limits<gr::Size_t>::max()) {
            for (std::int32_t i = 0; i < data.extents[0]; ++i) {
                ImPlot::PlotLine(data.signal_names[static_cast<std::size_t>(i)].c_str(), data.signal_values.data() + n * i, n);
            }
        } else {
            if (dataset_index >= data.extents[0]) {
                dataset_index = 0U;
            }
            ImPlot::PlotLine(data.signal_names[static_cast<std::size_t>(dataset_index)].c_str(), data.signal_values.data() + n * dataset_index, n);
        }
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

auto registerImPlotSink        = gr::registerBlock<opendigitizer::ImPlotSink, float, double>(gr::globalBlockRegistry());
auto registerImPlotSinkDataSet = gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float, double>(gr::globalBlockRegistry());
#endif
