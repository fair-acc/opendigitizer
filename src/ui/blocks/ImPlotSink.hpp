#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <limits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Tag.hpp>

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
T getValueOrDefault(const gr::property_map& map, const std::string& key, const T& defaultValue, std::source_location location = std::source_location::current()) {
    if (auto it = map.find(key); it != map.end()) {
        constexpr bool                strictChecks   = false;
        std::expected<T, std::string> convertedValue = pmtv::convert_safely<T, strictChecks>(it->second);
        if (!convertedValue) {
            throw gr::exception(fmt::format("failed to convert value for key {} - error: {}", key, convertedValue.error()), location);
        }
        return convertedValue.value_or(defaultValue);
    }
    return defaultValue;
}

inline void drawAndPruneTags(std::deque<TagData>& tagValues, double minX, double maxX, DigitizerUi::AxisScale axisScale, const ImVec4& color) {
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
    const auto   plotLimits = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const double yMax       = plotLimits.Y.Max;
    const double yRange     = std::abs(plotLimits.Y.Max - plotLimits.Y.Min);
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
            const std::string triggerLabel     = getValueOrDefault<std::string>(tag.map, gr::tag::TRIGGER_NAME.shortKey(), "TRIGGER");
            const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());

            if (triggerLabelSize.x < static_cast<float>(0.75 * yRange)) {
                ImPlot::PlotText(triggerLabel.c_str(), xTagPosition, yMax, {-fontHeight + 2.0f, 1.0f * triggerLabelSize.x}, ImPlotTextFlags_Vertical);
            } else {
                continue;
            }

            const std::string triggerCtx = getValueOrDefault<std::string>(tag.map, gr::tag::CONTEXT.shortKey(), "");
            if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                if (triggerCtxLabelSize.x < static_cast<float>(0.75 * yRange)) {
                    ImPlot::PlotText(triggerCtx.c_str(), xTagPosition, yMax, {5.0f, 1.0f * triggerCtxLabelSize.x}, ImPlotTextFlags_Vertical);
                } else {
                    continue;
                }
            }

            lastTextPixelX = xPixelPos;
        } // plot labels
    }
    ImGui::PopStyleColor();
}

template<typename T>
void drawDataSetTimingEvents(const gr::DataSet<T>& dataset, DigitizerUi::AxisScale axisScale, const ImVec4& color) {
    using enum DigitizerUi::AxisScale;
    if (dataset.timing_events.empty() || dataset.axisValues(0).empty()) {
        return;
    }

    // Let's assume axisValues(0) is our main X-axis
    // We'll do a front/back min/max for pruning
    const auto&  xAxisSpan = dataset.axisValues(0);
    const double xAxisMin  = static_cast<double>(xAxisSpan.front());
    const double xAxisMax  = static_cast<double>(xAxisSpan.back());

    // Prepare a small transform that replicates your "Time", "LinearReverse", etc. logic
    auto transformX = [axisScale, &xAxisMin, &xAxisMax](double xVal) {
        switch (axisScale) {
        case Linear:
        case Log10:
        case SymLog: return xVal - xAxisMin;
        case LinearReverse: return xVal - xAxisMax;
        case Time:
        default: return xVal; // pass through
        }
    };

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    float        lastTextPixelX = ImPlot::PlotToPixels(transformX(std::min(xAxisMin, xAxisMax)), 0.0f).x;
    float        lastAxisPixelX = ImPlot::PlotToPixels(transformX(std::max(xAxisMin, xAxisMax)), 0.0f).x;
    const float  fontHeight     = ImGui::GetFontSize();
    const auto   plotLimits     = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const double yMax           = plotLimits.Y.Max;
    const double yRange         = std::abs(plotLimits.Y.Max - plotLimits.Y.Min);
    for (std::size_t sig_i = 0; sig_i < dataset.timing_events.size(); ++sig_i) {
        auto& eventsForSig = dataset.timing_events[sig_i];

        for (auto& [xIndex, tagMap] : eventsForSig) {
            if (xIndex < 0 || static_cast<std::size_t>(xIndex) >= xAxisSpan.size()) {
                continue; // out-of-bounds
            }

            double      xVal         = static_cast<double>(xAxisSpan[static_cast<std::size_t>(xIndex)]);
            double      xTagPosition = transformX(xVal);
            const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0f).x;
            ImPlot::SetNextLineStyle(color);
            ImPlot::PlotInfLines("TagLines", &xTagPosition, 1, ImPlotInfLinesFlags_None);

            // suppress tag labels if it is too close to the previous one or close to the extremities
            if ((xPixelPos - lastTextPixelX) > 2.0f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
                const std::string triggerLabel     = getValueOrDefault<std::string>(tagMap, gr::tag::TRIGGER_NAME.shortKey(), "TRIGGER");
                const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());

                if (triggerLabelSize.x < static_cast<float>(0.75 * yRange)) {
                    ImPlot::PlotText(triggerLabel.c_str(), xTagPosition, yMax, {-fontHeight + 2.0f, 1.0f * triggerLabelSize.x}, ImPlotTextFlags_Vertical);
                } else {
                    continue;
                }

                const std::string triggerCtx = getValueOrDefault<std::string>(tagMap, gr::tag::CONTEXT.shortKey(), "");
                if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                    const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                    if (triggerCtxLabelSize.x < static_cast<float>(0.75 * yRange)) {
                        ImPlot::PlotText(triggerCtx.c_str(), xTagPosition, yMax, {5.0f, 1.0f * triggerCtxLabelSize.x}, ImPlotTextFlags_Vertical);
                    } else {
                        continue;
                    }
                }

                lastTextPixelX = xPixelPos;
            } // plot labels
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
        static const auto defaultColor = ImVec4(0.3f, 0.3f, 0.3f, 1.f);
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
using ImPlotSinkBase = gr::Block<TBlock, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>;

GR_REGISTER_BLOCK(opendigitizer::ImPlotSink, float, double, gr::DataSet<float>, gr::DataSet<double>)

template<typename T>
struct ImPlotSink : ImPlotSinkBase<ImPlotSink<T>> {
    using ValueType = gr::meta::fundamental_base_value_type_t<T>;

    gr::PortIn<T> in;
    uint32_t      color         = 0xff0000;                         ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    gr::Size_t    required_size = gr::DataSetLike<T> ? 10U : 2048U; // TODO: make this a multi-consumer/vector property
    std::string   signal_name;
    std::string   signal_quantity;
    std::string   signal_unit;
    float         signal_min     = std::numeric_limits<float>::lowest();
    float         signal_max     = std::numeric_limits<float>::max();
    float         sample_rate    = 1000.0f;
    gr::Size_t    dataset_index  = std::numeric_limits<gr::Size_t>::max();
    gr::Size_t    n_history      = 3U;
    float         history_offset = 0.01f;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, //
        dataset_index, n_history, history_offset);

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }();
    gr::HistoryBuffer<double> _xValues{required_size}; // needs to be 'double' because of required ns-level UTC timestamp precision
    gr::HistoryBuffer<T>      _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    void settingsChanged(const gr::property_map& /* oldSettings */, const gr::property_map& /* newSettings */) {
        if (_xValues.capacity() != required_size) {
            _xValues.resize(required_size);
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
                if (utcTime > 0.0 || (utcTime * 1e-9 + offset) > 0.0) {
                    _xUtcOffset = utcTime * 1e-9 + offset;
                }
                _tagValues.push_back({.timestamp = _xUtcOffset, .map = this->mergedInputTag().map});
            }
            this->_mergedInputTag.map.clear(); // TODO: provide proper API for clearing tags
            _xValues.push_back(_xUtcOffset);
        } else {
            if constexpr (std::is_arithmetic_v<T>) {
                const double Ts = 1.0 / static_cast<double>(sample_rate);
                _xValues.push_back(_xValues.back() + Ts);
            }
        }
        _yValues.push_back(input);
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
            return gr::work::Status::OK;
        }

        struct PlotLineContext {
            std::span<const double>    xValues;
            std::span<const ValueType> yValues;
            AxisScale                  axisScale;
            ValueType                  yOffset{0};
        };

        constexpr auto pointGetter = +[](int signedIndex, void* user_data) -> ImPlotPoint {
            const std::size_t          idx     = cast_to_unsigned(signedIndex);
            auto*                      ctx     = static_cast<PlotLineContext*>(user_data);
            std::span<const double>    xValues = ctx->xValues;
            std::span<const ValueType> yValues = ctx->yValues;
            ValueType                  yOffset = ctx->yOffset;

            switch (ctx->axisScale) {
            case Time: return {xValues[idx], static_cast<double>(yValues[idx] + yOffset)};
            case LinearReverse: {
                const double xValueBack = xValues.back();
                return {xValues[idx] - xValueBack, static_cast<double>(yValues[idx] + yOffset)};
            }
            case Linear:
            case Log10:  // base 10 logarithmic scale -> transform computed by axis
            case SymLog: // symmetric log scale -> transform computed by axis
            default:     // linear
                const double xValueFront = xValues.front();
                return {xValues[idx] - xValueFront, static_cast<double>(yValues[idx] + yOffset)};
            }
        };

        if constexpr (std::is_arithmetic_v<T>) {
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
            ImPlot::SetNextLineStyle(lineColor);

            // draw tags before data (data is drawn on top)
            if (getValueOrDefault<bool>(config, "draw_tag", false)) {
                ImVec4 tagColor = lineColor;
                tagColor.w *= 0.35f; // semi-transparent tags
                drawAndPruneTags(_tagValues, _xValues.front(), _xValues.back(), axisScale, tagColor);
            }

            PlotLineContext ctx{_xValues.get_span(0UZ), _yValues.get_span(0UZ), axisScale, ValueType{0}};
            ImPlot::PlotLineG(label.c_str(), pointGetter, &ctx, static_cast<int>(_xValues.size()));
        } else if constexpr (gr::DataSetLike<T>) {

            const std::size_t nMax = std::min(_yValues.size(), static_cast<std::size_t>(n_history));
            for (std::size_t historyIdx = 0UZ; historyIdx < nMax; historyIdx++) {
                const gr::DataSet<ValueType>& dataSet = _yValues.at(historyIdx);
                // dimension checks
                if (dataSet.extents.size() < 2) {
                    continue; // not enough dimensions
                }

                ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
                lineColor.w      = std::max(0.f, 1.0f - static_cast<float>(historyIdx) * 1.f / static_cast<float>(nMax));
                ImPlot::SetNextLineStyle(lineColor);

                std::vector<double> xAxisDouble;
                if constexpr (!std::is_same_v<ValueType, double>) { // TODO: find a smarter zero-copy solution
                    auto xSpan = dataSet.axisValues(0UZ);
                    xAxisDouble.assign(xSpan.begin(), xSpan.end());
                }

                // draw tags before data (data is drawn on top)
                if (historyIdx == 0UZ || getValueOrDefault<bool>(config, "draw_tag", false)) {
                    ImVec4 tagColor = lineColor;
                    tagColor.w *= std::max(0.35f - 0.05f * static_cast<float>(historyIdx), 0.f); // semi-transparent
                    drawDataSetTimingEvents(dataSet, axisScale, tagColor);
                }

                const auto nsignals = dataSet.extents[0];
                const auto npoints  = static_cast<std::size_t>(dataSet.extents[1]);
                if (dataset_index == std::numeric_limits<gr::Size_t>::max()) {
                    // draw all signals
                    auto [minVal, maxVal] = std::ranges::minmax(dataSet.signal_values);
                    ValueType baseOffset  = static_cast<ValueType>(history_offset) * (maxVal - minVal);

                    for (std::size_t signalIdx = 0UZ; signalIdx < cast_to_unsigned(nsignals); ++signalIdx) {
                        ImPlot::SetNextLineStyle(lineColor);
                        PlotLineContext ctx{xAxisDouble, dataSet.signalValues(0UZ), axisScale, static_cast<ValueType>(signalIdx + historyIdx) * baseOffset};
                        ImPlot::PlotLineG(dataSet.signal_names[signalIdx].c_str(), pointGetter, &ctx, static_cast<int>(npoints));
                    }
                } else {
                    // single sub-signal
                    if (dataset_index >= static_cast<gr::Size_t>(nsignals)) {
                        dataset_index = 0U;
                    }
                    const auto signalIdx = static_cast<std::size_t>(dataset_index);
                    ImPlot::SetNextLineStyle(lineColor);
                    PlotLineContext ctx{xAxisDouble, dataSet.signalValues(0UZ), axisScale};
                    ImPlot::PlotLineG(dataSet.signal_names[signalIdx].c_str(), pointGetter, &ctx, static_cast<int>(npoints));
                }
            }
        } //  if constexpr (gr::DataSetLike<T>) { .. }

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

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        [[maybe_unused]] const gr::work::Status status = this->invokeWork();
        if (data.extents.empty()) {
            return gr::work::Status::OK;
        }
        setAxisFromConfig(config);
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

inline static auto registerImPlotSink        = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(gr::globalBlockRegistry());
inline static auto registerImPlotSinkDataSet = gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float>(gr::globalBlockRegistry());
#endif
