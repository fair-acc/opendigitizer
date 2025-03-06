#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <limits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

#include "../Dashboard.hpp"
#include "../common/ImguiWrap.hpp"

#include <implot.h>

#include "conversion.hpp"
#include "meta.hpp"

namespace opendigitizer {
struct TagData {
    double           timestamp;
    gr::property_map map;
};

template<typename T>
T getValueOrDefault(const gr::property_map& map, const std::string& key, const T& defaultValue) {
    if (auto it = map.find(key); it != map.end()) {
        if (auto ptr = std::get_if<T>(&it->second)) {
            return *ptr;
        }
    }
    return defaultValue;
}

inline void drawAndPruneTags(std::deque<TagData>& tagValues, double minX, double maxX, DigitizerUi::AxisScale axisScale, const ImVec4& color) {
    using DigitizerUi::AxisScale;
    using enum DigitizerUi::AxisScale;

    std::erase_if(tagValues, [=](const auto& tag) { return static_cast<double>(tag.timestamp) < std::min(minX, maxX); });
    if (tagValues.empty()) {
        return;
    }

    auto transformX = [axisScale, &minX, &maxX](double xPos) -> double {
        switch (axisScale) {
        case Linear:
        case Log10:
        case SymLog: return xPos - minX;
        case LinearReverse: return xPos - maxX;
        case Time:
        default: return xPos;
        }
    };

    const float  fontHeight = ImGui::GetFontSize();
    const double yMax       = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO).Y.Max;
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    float lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(minX, maxX)), 0.0f).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(minX, maxX)), 0.0f).x;
    for (const auto& tag : tagValues) {
        double      xTagPosition = transformX(tag.timestamp);
        const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0f).x;

        ImPlot::SetNextLineStyle(color);
        ImPlot::PlotInfLines("TagLines", &xTagPosition, 1, ImPlotInfLinesFlags_None);

        // suppress tag labels if it is too close to the previous one or close to the extremities
        if ((xPixelPos - lastTextPixelX) > 2.0f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
            const std::string triggerLabel     = getValueOrDefault<std::string>(tag.map, "trigger_name", "TRIGGER");
            const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());

            ImPlot::PlotText(triggerLabel.c_str(), xTagPosition, yMax, {-fontHeight + 2.0f, 1.0f * triggerLabelSize.x}, ImPlotTextFlags_Vertical);

            if (auto metaInfo = tag.map.find("trigger_meta_info"); metaInfo != tag.map.end()) {
                if (auto mapPtr = std::get_if<gr::property_map>(&metaInfo->second)) {
                    auto              extractCtx = [](const std::string& s) { return s.substr(s.rfind('/') + 1); };
                    const std::string triggerCtx = extractCtx(getValueOrDefault<std::string>(*mapPtr, "context", ""));
                    if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                        const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                        ImPlot::PlotText(triggerCtx.c_str(), xTagPosition, yMax, {5.0f, 1.0f * triggerCtxLabelSize.x}, ImPlotTextFlags_Vertical);
                    }
                }
            }

            lastTextPixelX = xPixelPos;
        }
    }
    ImGui::PopStyleColor();
}

inline void setAxisFromConfig(const gr::property_map& config) {
    static constexpr ImAxis xAxes[] = {ImAxis_X1, ImAxis_X2, ImAxis_X3};
    static constexpr ImAxis yAxes[] = {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3};
    ImPlot::SetAxis(xAxes[std::clamp(getValueOrDefault<std::size_t>(config, "xAxisID", 0UZ), 0UZ, 2UZ)]);
    ImPlot::SetAxis(yAxes[std::clamp(getValueOrDefault<std::size_t>(config, "yAxisID", 0UZ), 0UZ, 2UZ)]);
}

struct ImPlotSinkModel {
    std::string uniqueName;

    ImPlotSinkModel(std::string _uniqueName) : uniqueName(std::move(_uniqueName)) {}

    virtual ~ImPlotSinkModel() {}

    virtual gr::work::Status draw(const gr::property_map& config = {}) noexcept = 0;

    virtual std::string name() const              = 0;
    virtual void        setName(std::string name) = 0;

    virtual gr::SettingsBase& settings() const = 0;

    virtual gr::work::Result work(std::size_t count) = 0;

    virtual void* raw() const = 0;

    ImVec4 color() {
        static const auto defaultColor = ImVec4(0.3, 0.3, 0.3, 1);
        auto              maybeColor   = settings().get("color");
        if (!maybeColor) {
            return defaultColor;
        }
        const auto colorVariant = maybeColor.value();

        const auto* colorValueInt32 = std::get_if<std::uint32_t>(&colorVariant);
        if (colorValueInt32) {
            return ImGui::ColorConvertU32ToFloat4(*colorValueInt32);
        }

        const auto colorValueVectorF = std::get_if<std::vector<float>>(&colorVariant);
        if (colorValueVectorF && colorValueVectorF->size() == 4) {
            return ImVec4((*colorValueVectorF)[0], (*colorValueVectorF)[1], (*colorValueVectorF)[2], (*colorValueVectorF)[3]);
        }

        return defaultColor;
    }

    bool isVisible = true;
};

struct ImPlotSinkManager {
private:
    ImPlotSinkManager() {}

    ImPlotSinkManager(const ImPlotSinkManager&) = delete;

    ImPlotSinkManager& operator=(const ImPlotSinkManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<ImPlotSinkModel>> _knownSinks;

    template<typename TBlock>
    struct SinkWrapper : ImPlotSinkModel {
        SinkWrapper(TBlock* _block) : ImPlotSinkModel(_block->unique_name), block(_block) {}

        gr::work::Status draw(const gr::property_map& config = {}) noexcept override { return block->draw(config); }

        std::string name() const override { return block->name; }
        void        setName(std::string name) override { block->name = std::move(name); }

        virtual gr::SettingsBase& settings() const { return block->settings(); }

        gr::work::Result work(std::size_t count) override { return block->work(count); }

        void* raw() const override { return static_cast<void*>(block); }

        TBlock* block = nullptr;
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

    template<typename Pred>
    ImPlotSinkModel* findSink(Pred pred) const {
        auto it = std::ranges::find_if(_knownSinks, [&](const auto& kvp) { return pred(*kvp.second.get()); });
        if (it == _knownSinks.cend()) {
            return nullptr;
        }
        return it->second.get();
    }

    template<typename Fn>
    void forEach(Fn function) {
        for (const auto& [_, sinkPtr] : _knownSinks) {
            function(*sinkPtr.get());
        }
    }
};

template<typename TBlock>
using ImPlotSinkBase = gr::Block<TBlock, gr::BlockingIO<false>, gr::SupportedTypes<float, double>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>;

template<typename T>
struct ImPlotSink : public ImPlotSinkBase<ImPlotSink<T>> {
    gr::PortIn<T> in;
    std::uint32_t color         = 0xff0000; ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    gr::Size_t    required_size = 2048U;    // TODO: make this a multi-consumer/vector property
    std::string   signal_name;
    std::string   signal_quantity;
    std::string   signal_unit;
    float         signal_min  = std::numeric_limits<float>::lowest();
    float         signal_max  = std::numeric_limits<float>::max();
    float         sample_rate = 1000.0f;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, signal_quantity, signal_unit, signal_min, signal_max, sample_rate);

    double                    _xUtcOffset   = 0.;
    std::uint64_t             _utcOffsetIdx = 0U; // index since last clock update
    gr::HistoryBuffer<double> _xValues{required_size};
    gr::HistoryBuffer<double> _xUtcValues{required_size};
    gr::HistoryBuffer<double> _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& /*new_settings*/) {
        if (_xValues.capacity() != required_size) {
            _xValues.resize(required_size);
            _xUtcValues.resize(required_size);
            _tagValues.clear();
        }
        if (_yValues.capacity() != required_size) {
            _yValues.resize(required_size);
            _tagValues.clear();
        }
    }

    constexpr void processOne(const T& input) noexcept {
        if (this->inputTagsPresent()) { // received tag
            const gr::property_map& tag = this->_mergedInputTag.map;

            if (tag.contains("trigger_time")) {
                const auto offset  = static_cast<double>(getValueOrDefault<float>(tag, "trigger_offset", 0.f));
                const auto utcTime = static_cast<double>(getValueOrDefault<uint64_t>(tag, "trigger_time", 0U)) + offset;
                if (utcTime > 0.0 || (utcTime + offset) > 0.0) {
                    _xUtcOffset   = (utcTime + offset) * 1e-9;
                    _utcOffsetIdx = 0U;
                }
                _tagValues.push_back({.timestamp = _xUtcOffset, .map = this->mergedInputTag().map});
            }
            this->_mergedInputTag.map.clear(); // TODO: provide proper API for clearing tags
        }

        if (_utcOffsetIdx == 0) {
            if (_xUtcOffset == 0.0) {
                using namespace std::chrono;
                auto now      = system_clock::now().time_since_epoch();
                _xUtcOffset   = duration<double, std::nano>(now).count() * 1e-9;
                _utcOffsetIdx = 0U;
            }
            _xUtcValues.push_back(_xUtcOffset);
        } else {
            if constexpr (std::is_arithmetic_v<T>) {
                const double Ts = 1.0 / static_cast<double>(sample_rate);
                _xUtcValues.push_back(_xUtcValues.back() + Ts);
            }
        }
        if constexpr (std::is_arithmetic_v<T>) {
            const double Ts = 1.0 / static_cast<double>(sample_rate);
            if (_xValues.size() == 0UZ) {
                _xValues.push_back(0.0);
            } else {
                _xValues.push_back(_xValues.back() + Ts);
            }
        }
        _yValues.push_back(input);
        _utcOffsetIdx++;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        using DigitizerUi::AxisScale;
        using enum DigitizerUi::AxisScale;
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();

        setAxisFromConfig(config);
        std::string scaleStr = config.contains("scale") ? std::get<std::string>(config.at("scale")) : "Linear";
        auto        trim     = [](const std::string& str) {
            auto start = std::ranges::find_if_not(str, [](unsigned char ch) { return std::isspace(ch); });
            auto end   = std::ranges::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
            return (start < end) ? std::string(start, end) : std::string{};
        };
        const AxisScale    axisScale = magic_enum::enum_cast<AxisScale>(trim(scaleStr)).value_or(AxisScale::Linear);
        const std::string& label     = signal_name.empty() ? this->name.value : signal_name;
        if (_yValues.empty()) {
            // plot one single dummy value so that the sink shows up in the plot legend
            double v = {};
            ImPlot::PlotLine(label.c_str(), &v, 1);
        } else {
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
            ImPlot::SetNextLineStyle(lineColor);

            // draw tags before data (data is drawn on top)
            if (getValueOrDefault<bool>(config, "draw_tag", false)) {
                lineColor.w *= 0.35f; // semi-transparent tags
                drawAndPruneTags(_tagValues, _xUtcValues.front(), _xUtcValues.back(), axisScale, lineColor);
            }

            switch (axisScale) {
            case Time: {
                ImPlot::PlotLine(label.c_str(), _xUtcValues.get_span(0).data(), _yValues.get_span(0).data(), static_cast<int>(_xValues.size()));
            } break;
            case LinearReverse: {
                // like Linear but normalised to the newest _xValues.back() thus the axis range is [xValues.front() - xValues.back(), 0]
                ImPlot::PlotLineG(
                    label.c_str(),
                    [](int idx, void* user_data) -> ImPlotPoint {
                        auto   self     = static_cast<ImPlotSink<T>*>(user_data);
                        double xShifted = self->_xValues[cast_to_unsigned(idx)] - self->_xValues.back();
                        double y        = self->_yValues[cast_to_unsigned(idx)];
                        return ImPlotPoint(xShifted, y);
                    },
                    this,                                // user_data pointer passed to the lambda
                    static_cast<int>(_xUtcValues.size()) // number of points
                );
            } break;
            case Linear:
            default: {
                ImPlot::PlotLineG(
                    label.c_str(),
                    [](int idx, void* user_data) -> ImPlotPoint {
                        auto   self     = static_cast<ImPlotSink<T>*>(user_data);
                        double xShifted = self->_xUtcValues[idx] - self->_xUtcValues.front();
                        double y        = self->_yValues[idx];
                        return ImPlotPoint(xShifted, y);
                    },
                    this,                                // user_data pointer passed to the lambda
                    static_cast<int>(_xUtcValues.size()) // number of points
                );
                // ImPlot::PlotLine(label.c_str(), _xValues.get_span(0).data(), _yValues.get_span(0).data(), static_cast<int>(_xValues.size()));
            } break;
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
    std::string                signal_quantity;
    std::string                signal_unit;
    float                      signal_min    = std::numeric_limits<float>::lowest();
    float                      signal_max    = std::numeric_limits<float>::max();
    gr::Size_t                 dataset_index = std::numeric_limits<gr::Size_t>::max();

    GR_MAKE_REFLECTABLE(ImPlotSinkDataSet, in, color, signal_name, signal_quantity, signal_unit, signal_min, signal_max, dataset_index);

public:
    ImPlotSinkDataSet(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSinkDataSet<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSinkDataSet() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    gr::DataSet<T> data{};

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        data        = input.back();
        std::ignore = input.consume(input.size());
        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& /*config*/ = {}) noexcept {
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
            if (dataset_index >= cast_to_unsigned(data.extents[0])) {
                dataset_index = 0U;
            }
            ImPlot::PlotLine(data.signal_names[static_cast<std::size_t>(dataset_index)].c_str(), data.signal_values.data() + cast_to_unsigned(n) * dataset_index, n);
        }
        return gr::work::Status::OK;
    }
};
} // namespace opendigitizer

inline static auto registerImPlotSink        = gr::registerBlock<opendigitizer::ImPlotSink, float>(gr::globalBlockRegistry());
inline static auto registerImPlotSinkDataSet = gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float>(gr::globalBlockRegistry());
#endif
