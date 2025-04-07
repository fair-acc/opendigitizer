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
#include "../common/LookAndFeel.hpp"

#include "../components/ColourManager.hpp"

#include "../common/ImguiWrap.hpp"
#include <implot.h>

#include "conversion.hpp"
#include "meta.hpp"

namespace opendigitizer {

constexpr std::string_view kFishyTagKey = "ui_fishy_tag";

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

inline ImVec2 plotVerticalTagLabel(const std::string_view& label, double xData, const ImPlotRect& plotLimits, bool plotLeft, double fractionBelowTop = 0.02, double sizeRatioLimit = 0.75) {
    const double yRange   = std::abs(plotLimits.Y.Max - plotLimits.Y.Min);
    const double ySafeTop = plotLimits.Y.Max - fractionBelowTop * yRange;
    const double yClamped = std::clamp(ySafeTop, plotLimits.Y.Min, plotLimits.Y.Max);
    ImVec2       pixelPos = ImPlot::PlotToPixels(xData, yClamped);
    if (label.empty()) {
        return pixelPos;
    }

    const double yPixelRange = static_cast<double>(std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y));
    ImVec2       textSize    = ImGui::CalcTextSize(label.data());
    if (static_cast<double>(textSize.x) > sizeRatioLimit * yPixelRange) {
        return pixelPos; // the text is too large relative to the vertical scale
    }

    ImVec2 pixOffset{plotLeft ? (-textSize.y + 2.0f) : (+5.0f), textSize.x};
    ImPlot::PlotText(label.data(), xData, yClamped, pixOffset, static_cast<int>(ImPlotTextFlags_Vertical) | static_cast<int>(ImPlotItemFlags_NoFit));
    return {pixelPos.x + pixOffset.x + textSize.y, pixelPos.y};
}

template<typename T>
static double transformX(double xVal, DigitizerUi::AxisScale axisScale, double xMin, double xMax) {
    using enum DigitizerUi::AxisScale;
    switch (axisScale) {
    case Time: return xVal;
    case LinearReverse:
        if constexpr (gr::DataSetLike<T>) {
            return xVal;
        } else {
            return xVal - xMax;
        }
    case Linear:
    case Log10:
    case SymLog:
    default: {
        if constexpr (gr::DataSetLike<T>) {
            return xVal;
        } else {
            return xVal - xMin;
        }
    }
    }
}

template<typename T>
void drawAndPruneTags(std::deque<TagData>& tagValues, double minX, double maxX, DigitizerUi::AxisScale axisScale, const ImVec4& color) {
    using enum DigitizerUi::AxisScale;
    using namespace DigitizerUi;

    std::erase_if(tagValues, [=](const auto& tag) { return static_cast<double>(tag.timestamp) < std::min(minX, maxX); });
    if (tagValues.empty()) {
        return;
    }

    IMW::Font   titleFont(LookAndFeel::instance().fontTiny[LookAndFeel::instance().prototypeMode]);
    const float fontHeight  = ImGui::GetFontSize();
    const auto  plotLimits  = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float yPixelRange = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    float lastTextPixelX = ImPlot::PlotToPixels(transformX<T>(std::min(minX, maxX), axisScale, minX, maxX), 0.0).x;
    float lastAxisPixelX = ImPlot::PlotToPixels(transformX<T>(std::max(minX, maxX), axisScale, minX, maxX), 0.0).x;
    for (const auto& tag : tagValues) {
        double      xTagPosition = transformX<T>(tag.timestamp, axisScale, minX, maxX);
        const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;

        if (tag.map.contains(kFishyTagKey)) {
            ImPlot::SetNextLineStyle(ImVec4(1.0, 0.0, 1.0, 1.0));
        } else {
            ImPlot::SetNextLineStyle(color);
        }
        ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

        // suppress tag labels if it is too close to the previous one or close to the extremities
        if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
            const std::string triggerLabel     = getValueOrDefault<std::string>(tag.map, gr::tag::TRIGGER_NAME.shortKey(), "TRIGGER");
            const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());

            if (triggerLabelSize.x < 0.75f * yPixelRange) {
                lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;
            } else {
                continue;
            }

            const std::string triggerCtx = getValueOrDefault<std::string>(tag.map, gr::tag::CONTEXT.shortKey(), "");
            if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                if (triggerCtxLabelSize.x < 0.75f * yPixelRange) {
                    lastTextPixelX = plotVerticalTagLabel(triggerCtx, xTagPosition, plotLimits, false).x;
                }
            }
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
    const double minX      = static_cast<double>(xAxisSpan.front());
    const double maxX      = static_cast<double>(xAxisSpan.back());

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    float       lastTextPixelX = ImPlot::PlotToPixels(transformX<gr::DataSet<T>>(std::min(minX, maxX), axisScale, minX, maxX), 0.0).x;
    float       lastAxisPixelX = ImPlot::PlotToPixels(transformX<gr::DataSet<T>>(std::max(minX, maxX), axisScale, minX, maxX), 0.0).x;
    const float fontHeight     = ImGui::GetFontSize();
    const auto  plotLimits     = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
    const float yPixelRange    = std::abs(ImPlot::PlotToPixels(0.0, plotLimits.Y.Max).y - ImPlot::PlotToPixels(0.0, plotLimits.Y.Min).y);
    for (std::size_t sig_i = 0; sig_i < dataset.timing_events.size(); ++sig_i) {
        auto& eventsForSig = dataset.timing_events[sig_i];

        for (auto& [xIndex, tagMap] : eventsForSig) {
            if (xIndex < 0 || static_cast<std::size_t>(xIndex) >= xAxisSpan.size()) {
                continue; // out-of-bounds
            }

            double      xVal         = static_cast<double>(xAxisSpan[static_cast<std::size_t>(xIndex)]);
            double      xTagPosition = transformX<gr::DataSet<T>>(xVal, axisScale, minX, maxX);
            const float xPixelPos    = ImPlot::PlotToPixels(xTagPosition, 0.0).x;
            ImPlot::SetNextLineStyle(color);
            ImPlot::PlotInfLines("", &xTagPosition, 1, ImPlotInfLinesFlags_None);

            // suppress tag labels if it is too close to the previous one or close to the extremities
            if ((xPixelPos - lastTextPixelX) > 1.5f * fontHeight && (lastAxisPixelX - xPixelPos) > 2.0f * fontHeight) {
                const std::string triggerLabel     = getValueOrDefault<std::string>(tagMap, gr::tag::TRIGGER_NAME.shortKey(), "TRIGGER");
                const ImVec2      triggerLabelSize = ImGui::CalcTextSize(triggerLabel.c_str());
                if (triggerLabelSize.x < 0.75f * yPixelRange) {
                    lastTextPixelX = plotVerticalTagLabel(triggerLabel, xTagPosition, plotLimits, true).x;
                } else {
                    continue;
                }

                const std::string triggerCtx = getValueOrDefault<std::string>(tagMap, gr::tag::CONTEXT.shortKey(), "");
                if (!triggerCtx.empty() && triggerCtx != triggerLabel) {
                    const ImVec2 triggerCtxLabelSize = ImGui::CalcTextSize(triggerCtx.c_str());
                    if (triggerCtxLabelSize.x < 0.75f * yPixelRange) {
                        lastTextPixelX = plotVerticalTagLabel(triggerCtx, xTagPosition, plotLimits, false).x;
                    }
                }
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

    virtual std::string name() const                    = 0;
    virtual void        setName(std::string name)       = 0;
    virtual std::string signalName() const              = 0;
    virtual void        setSignalName(std::string name) = 0;

    virtual gr::SettingsBase& settings() const = 0;

    virtual gr::work::Result work(std::size_t count) = 0;

    virtual void* raw() const = 0;

    ImVec4 color() const {
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
        std::string signalName() const override { return block->signal_name; }
        void        setSignalName(std::string name) override { block->signal_name = std::move(name); }

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

using namespace gr;

GR_REGISTER_BLOCK(opendigitizer::ImPlotSink, float, double, gr::DataSet<float>, gr::DataSet<double>)

template<typename T>
struct ImPlotSink : ImPlotSinkBase<ImPlotSink<T>> {
    using ValueType = gr::meta::fundamental_base_value_type_t<T>;
    // optional shortening
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    PortIn<T> in;

    ManagedColour                                                                                                                                         _colour;
    A<uint32_t, "plot color", Doc<"RGB color for the plot">>                                                                                              color         = 0U;
    A<gr::Size_t, "required buffer size", Doc<"Minimum number of samples to retain">>                                                                     required_size = gr::DataSetLike<T> ? 10U : 2048U; // TODO: make this a multi-consumer/vector property
    A<std::string, "signal name", Visible, Doc<"Human-readable identifier for the signal">>                                                               signal_name;
    A<std::string, "signal quantity", Visible, Doc<"Physical quantity represented by the signal">>                                                        signal_quantity;
    A<std::string, "signal unit", Visible, Doc<"Unit of measurement for the signal values">>                                                              signal_unit;
    A<float, "signal min", Doc<"Minimum expected value for the signal">, Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min     = std::numeric_limits<float>::lowest();
    A<float, "signal max", Doc<"Maximum expected value for the signal">, Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max     = std::numeric_limits<float>::max();
    A<float, "sample rate", Visible, Doc<"Sampling frequency in Hz">, Unit<"Hz">, Limits<float(0), std::numeric_limits<float>::max()>>                    sample_rate    = 1000.0f;
    A<gr::Size_t, "dataset index", Visible, Doc<"Index of the dataset, if applicable">>                                                                   dataset_index  = std::numeric_limits<gr::Size_t>::max();
    A<gr::Size_t, "history length", Doc<"Number of samples retained for historical visualization">>                                                       n_history      = 3U;
    A<float, "history offset", Doc<"Time offset for historical data display">, Unit<"s">, Limits<float(0.0), std::numeric_limits<float>::max()>>          history_offset = 0.01f;
    A<bool, "plot tags", Doc<"true: draw the timing tags">>                                                                                               plot_tags      = true;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, //
        dataset_index, n_history, history_offset, plot_tags);

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }(); // utc timestamp of the last tag or first sample
    gr::HistoryBuffer<double> _xValues{required_size}; // needs to be 'double' because of required ns-level UTC timestamp precision
    gr::HistoryBuffer<T>      _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    double      _sample_period = 1.0 / static_cast<double>(sample_rate);
    std::size_t _sample_count  = 0UZ;

    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) { ImPlotSinkManager::instance().registerPlotSink(this); }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    void settingsChanged(const gr::property_map& /* oldSettings */, const gr::property_map& newSettings) {
        if (newSettings.contains("color") || color == 0U) {
            if (color == 0U) {
                _colour.updateColour();
            } else {
                _colour.setColour(color);
            }
            color = _colour.colour();
        }

        if (_xValues.capacity() != required_size) {
            _xValues.resize(required_size);
            _tagValues.clear();
        }
        if (_yValues.capacity() != required_size) {
            _yValues.resize(required_size);
            _tagValues.clear();
        }

        _sample_period = 1.0 / static_cast<double>(sample_rate);
    }

    constexpr void processOne(const T& input) noexcept {
        if (this->inputTagsPresent()) { // received tag
            const gr::property_map& tag = this->_mergedInputTag.map;

            if (tag.contains(gr::tag::TRIGGER_TIME.shortKey())) {
                const auto   offset       = static_cast<double>(getValueOrDefault<float>(tag, gr::tag::TRIGGER_OFFSET.shortKey(), 0.f));
                const auto   utcTime      = static_cast<double>(getValueOrDefault<uint64_t>(tag, gr::tag::TRIGGER_TIME.shortKey(), 0U)) + offset;
                const double tagEventTime = utcTime * 1e-9 + offset; // [s]
                bool         tagOK        = true;

                if ((utcTime > 0.0 || tagEventTime > 0.0) && tagEventTime > _xUtcOffset) {
                    _xUtcOffset   = tagEventTime;
                    _sample_count = 0UZ;
                } else {
                    tagOK = false; // mark fishy tag
                }

                if (plot_tags) {
                    _tagValues.push_back({.timestamp = _xUtcOffset, .map = this->mergedInputTag().map});
                    if (!tagOK) {
                        _tagValues.back().map[std::string(kFishyTagKey)] = true;
                    }
                }
            }
            this->_mergedInputTag.map.clear(); // TODO: provide proper API for clearing tags
            _xValues.push_back(_xUtcOffset);
        } else {
            if constexpr (std::is_arithmetic_v<T>) {
                _xValues.push_back(_xUtcOffset + static_cast<double>(_sample_count) * _sample_period);
            }
        }
        _yValues.push_back(input);
        _sample_count++;
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
        const std::string& label     = signal_name.value.empty() ? this->name.value : signal_name.value;
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
            std::size_t idx = static_cast<std::size_t>(signedIndex);
            auto*       ctx = static_cast<PlotLineContext*>(user_data);

            double xVal = ctx->xValues[idx];
            double yVal = static_cast<double>(ctx->yValues[idx] + ctx->yOffset);

            switch (ctx->axisScale) {
            case Time: return {xVal, yVal};

            case LinearReverse: {
                if constexpr (gr::DataSetLike<T>) {
                    return {xVal, yVal};
                } else { // fundamental types
                    double xMax = ctx->xValues.back();
                    return {xVal - xMax, yVal};
                }
            }

            case Linear:
            case Log10:
            case SymLog:
            default: {
                if constexpr (gr::DataSetLike<T>) {
                    return {xVal, yVal};
                } else { // fundamental types
                    double xMin = ctx->xValues.front();
                    return {xVal - xMin, yVal};
                }
            }
            }
        };

        color = _colour.colour();
        if constexpr (std::is_arithmetic_v<T>) {
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | color);
            ImPlot::SetNextLineStyle(lineColor);

            auto [minX, maxX] = std::ranges::minmax(_xValues);
            // draw tags before data (data is drawn on top)
            if (getValueOrDefault<bool>(config, "draw_tag", false)) {
                ImVec4 tagColor = lineColor;
                tagColor.w *= 0.35f; // semi-transparent tags
                drawAndPruneTags<T>(_tagValues, minX, maxX, axisScale, tagColor);
            }

            PlotLineContext ctx{_xValues.get_span(0UZ), _yValues.get_span(0UZ), axisScale, ValueType{0}};
            ImPlot::PlotLineG(label.c_str(), pointGetter, &ctx, static_cast<int>(_xValues.size())); // limited to int, even if xValues can have long values
        } else if constexpr (gr::DataSetLike<T>) {
            const std::size_t nMax = std::min(_yValues.size(), static_cast<std::size_t>(n_history));
            for (std::size_t historyIdx = nMax; historyIdx-- > 0;) { // draw newest DataSet last -> on top
                const gr::DataSet<ValueType>& dataSet = _yValues.at(historyIdx);
                // dimension checks
                const std::size_t nsignals = dataSet.size();
                if (dataSet.extents.size() != 1UZ && nsignals < 1UZ) {
                    continue; // not 1D signal or not enough signals
                }

                ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | color);
                lineColor.w      = std::max(0.f, 1.0f - static_cast<float>(historyIdx) * 1.f / static_cast<float>(nMax));
                ImPlot::SetNextLineStyle(lineColor);

                std::vector<double> xAxisDouble;
                if constexpr (!std::is_same_v<ValueType, double>) { // TODO: find a smarter zero-copy solution
                    if (!dataSet.axis_values.empty()) {
                        auto xSpan = dataSet.axisValues(0UZ);
                        xAxisDouble.assign(xSpan.begin(), xSpan.end());
                    }
                }

                // draw tags before data (data is drawn on top)
                if (historyIdx == 0UZ && getValueOrDefault<bool>(config, "draw_tag", false)) {
                    ImVec4 tagColor = lineColor;
                    tagColor.w *= std::max(0.35f - 0.05f * static_cast<float>(historyIdx), 0.f); // semi-transparent
                    drawDataSetTimingEvents(dataSet, axisScale, tagColor);
                }

                // NOTE: npoints is long int, while ImPlot lines are limited to int
                const auto npoints = cast_to_signed(dataSet.axisValues(0UZ).size());
                if (dataset_index == std::numeric_limits<gr::Size_t>::max()) {
                    // draw all signals
                    auto [minVal, maxVal] = std::ranges::minmax(dataSet.signal_values);
                    ValueType baseOffset  = static_cast<ValueType>(history_offset) * (maxVal - minVal);

                    for (std::size_t signalIdx = 0UZ; signalIdx < nsignals; ++signalIdx) {
                        ImPlot::SetNextLineStyle(lineColor);
                        PlotLineContext ctx{xAxisDouble, dataSet.signalValues(0UZ), axisScale, static_cast<ValueType>(signalIdx + historyIdx) * baseOffset};
                        ImPlot::PlotLineG(historyIdx == 0UZ ? dataSet.signal_names[signalIdx].c_str() : "", pointGetter, &ctx, static_cast<int>(npoints));
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

} // namespace opendigitizer

inline static auto registerImPlotSink = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(gr::globalBlockRegistry());

#endif
