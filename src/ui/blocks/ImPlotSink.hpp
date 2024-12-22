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
struct TagData {
    double           timestamp;
    gr::property_map map;
};

inline void drawAndPruneTags(std::deque<TagData>& tagValues, double minX, double maxX, const ImVec4& color) {
    std::erase_if(tagValues, [=](const auto& tag) { return static_cast<double>(tag.timestamp) < std::min(minX, maxX); });

    if (tagValues.empty()) {
        return;
    }

    auto getStringOrDefault = [](const gr::property_map& map, const std::string& key, const std::string& defaultValue) -> std::string {
        if (auto it = map.find(key); it != map.end()) {
            if (auto strPtr = std::get_if<std::string>(&it->second)) {
                return *strPtr;
            }
        }
        return defaultValue;
    };

    const float  fontHeight = ImGui::GetFontSize();
    const double yMax       = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO).Y.Max;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    float lastTextPixelX = -std::numeric_limits<float>::infinity();
    for (const auto& tag : tagValues) {
        const double x         = tag.timestamp;
        const float  xPixelPos = ImPlot::PlotToPixels(x, 0.0f).x;

        ImPlot::SetNextLineStyle(color);
        ImPlot::PlotInfLines("TagLines", &x, 1, ImPlotInfLinesFlags_None);

        // suppress tag labels if it is too close to the previous one
        if (xPixelPos - lastTextPixelX > 2.0f * fontHeight || lastTextPixelX == -std::numeric_limits<float>::infinity()) {
            const std::string triggerLabel     = getStringOrDefault(tag.map, "trigger_name", "TRIGGER");
            const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());

            ImPlot::PlotText(triggerLabel.c_str(), x, yMax, {-fontHeight + 2.0f, 1.0f * triggerLabelSize.x}, ImPlotTextFlags_Vertical);

            if (auto metaInfo = tag.map.find("trigger_meta_info"); metaInfo != tag.map.end()) {
                if (auto mapPtr = std::get_if<gr::property_map>(&metaInfo->second)) {
                    auto              extractCtx = [](const std::string& s) { return s.substr(s.rfind('/') + 1); };
                    const std::string triggerCtx = extractCtx(getStringOrDefault(*mapPtr, "context", ""));
                    if (!triggerCtx.empty()) {
                        const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                        ImPlot::PlotText(triggerCtx.c_str(), x, yMax, {5.0f, 1.0f * triggerCtxLabelSize.x}, ImPlotTextFlags_Vertical);
                    }
                }
            }

            lastTextPixelX = xPixelPos;
        }
    }
    ImGui::PopStyleColor();
}

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
using ImPlotSinkBase = gr::Block<TBlock, gr::BlockingIO<false>, gr::SupportedTypes<float, double>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>;

template<typename T>
struct ImPlotSink : public ImPlotSinkBase<ImPlotSink<T>> {
    gr::PortIn<T> in;
    uint32_t      color = 0xff0000;
    ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    gr::Size_t  required_size = 2048U; // TODO: make this a multi-consumer/vector property
    std::string signal_name;
    std::string signal_quantity;
    std::string signal_unit;
    float       signal_min  = std::numeric_limits<float>::lowest();
    float       signal_max  = std::numeric_limits<float>::max();
    float       sample_rate = 1000.0f;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, signal_quantity, signal_unit, signal_min, signal_max, sample_rate);

    gr::HistoryBuffer<double> _xValues{required_size};
    gr::HistoryBuffer<double> _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
        if (_xValues.capacity() != required_size) {
            _xValues = gr::HistoryBuffer<double>(required_size); // TODO: copy old data to new one
            _tagValues.clear();
        }
        if (_yValues.capacity() != required_size) {
            _yValues = gr::HistoryBuffer<double>(required_size); // TODO: copy old data to new one
            _tagValues.clear();
        }
    }

    constexpr void processOne(const T& input) noexcept {
        if constexpr (std::is_arithmetic_v<T>) {
            in.max_samples  = static_cast<std::size_t>(2.f * sample_rate / 25.f);
            const double Ts = 1.0 / static_cast<double>(sample_rate);
            _xValues.push_back(_xValues[1] + 2 * Ts);
        }
        _yValues.push_back(input);

        if (this->inputTagsPresent()) {
            // received tag
            _tagValues.push_back({_xValues[0], this->mergedInputTag().map});
            this->_mergedInputTag.map.clear(); // TODO: provide proper API for clearing tags
        }
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();
        const auto&                             label  = signal_name.empty() ? this->name.value : signal_name;
        if (_yValues.empty()) {
            // plot one single dummy value so that the sink shows up in the plot legend
            double v = {};
            ImPlot::PlotLine(label.c_str(), &v, 1);
        } else {
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
            // TODO: remove reverse workaround ... newest value (time > 0) should be always on the right
            std::vector reversedX(_xValues.rbegin(), _xValues.rend());
            std::vector reversedY(_yValues.rbegin(), _yValues.rend());
            ImPlot::SetNextLineStyle(lineColor);
            ImPlot::PlotLine(label.c_str(), reversedX.data(), reversedY.data(), static_cast<int>(reversedY.size()));

            if (config.contains("draw_tag")) {
                const bool* drawTag = std::get_if<bool>(&config.at("draw_tag"));
                if (!drawTag || !*drawTag) {
                    return gr::work::Status::OK;
                }
                lineColor.w *= 0.75f; // semi-transparent tags
                drawAndPruneTags(_tagValues, reversedX.front(), reversedX.back(), lineColor);
            }
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

inline static auto registerImPlotSink        = gr::registerBlock<opendigitizer::ImPlotSink, float, double>(gr::globalBlockRegistry());
inline static auto registerImPlotSinkDataSet = gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float, double>(gr::globalBlockRegistry());
#endif
