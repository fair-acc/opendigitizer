#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <limits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>

#include "../Dashboard.hpp"
#include "../common/ImguiWrap.hpp"

#include <implot.h>

#include "meta.hpp"

namespace opendigitizer {
struct TagData {
    double           timestamp;
    gr::property_map map;
};

template<typename T>
T getValueOrDefault(const gr::property_map& map, const std::string& key, const T& defaultValue) {
    if (auto it = map.find(key); it != map.end()) {
        constexpr bool                strictChecks   = false;
        std::expected<T, std::string> convertedValue = pmtv::convert_safely<T, strictChecks>(it->second);
        if (!convertedValue) {
            fmt::println("failed to convert value for key {} - error: {}", key, convertedValue.error());
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

struct ImPlotSinkManager {
private:
    ImPlotSinkManager() {}

    ImPlotSinkManager(const ImPlotSinkManager&) = delete;

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
using ImPlotSinkBase = gr::Block<TBlock, gr::BlockingIO<false>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>;

template<typename T>
struct ImPlotSink : ImPlotSinkBase<ImPlotSink<T>> {
    gr::PortIn<T> in;
    uint32_t      color         = 0xff0000;                         ///< RGB color for the plot // TODO use better type, support configurable colors for datasets?
    gr::Size_t    required_size = gr::DataSetLike<T> ? 10U : 2048U; // TODO: make this a multi-consumer/vector property
    std::string   signal_name;
    std::string   signal_quantity;
    std::string   signal_unit;
    float         signal_min    = std::numeric_limits<float>::lowest();
    float         signal_max    = std::numeric_limits<float>::max();
    float         sample_rate   = 1000.0f;
    gr::Size_t    dataset_index = std::numeric_limits<gr::Size_t>::max();

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, dataset_index);

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }();
    gr::HistoryBuffer<double> _xValues{required_size}; // needs to be 'double' because of required ns-level UTC timestamp precision
    gr::HistoryBuffer<T>      _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    ImPlotSink(gr::property_map initParameters) : ImPlotSinkBase<ImPlotSink<T>>(std::move(initParameters)) {
        fmt::println("register ImplotSink<{}> - name: '{}'", gr::meta::type_name<T>(), this->name);
        ImPlotSinkManager::instance().registerPlotSink(this);
    }

    ~ImPlotSink() { ImPlotSinkManager::instance().unregisterPlotSink(this); }

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
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
                if (utcTime > 0.0 || (utcTime + offset) > 0.0) {
                    _xUtcOffset = (utcTime + offset) * 1e-9;
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

        if constexpr (std::is_arithmetic_v<T>) {
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
            ImPlot::SetNextLineStyle(lineColor);

            // draw tags before data (data is drawn on top)
            if (getValueOrDefault<bool>(config, "draw_tag", false)) {
                lineColor.w *= 0.35f; // semi-transparent tags
                drawAndPruneTags(_tagValues, _xValues.front(), _xValues.back(), axisScale, lineColor);
            }

            struct PlotLineContext {
                ImPlotSink<T>* sink;
                AxisScale      axisScale;
            };

            constexpr auto pointGetter = +[](int idx, void* user_data) -> ImPlotPoint {
                auto* ctx  = static_cast<PlotLineContext*>(user_data);
                auto* self = ctx->sink;
                switch (ctx->axisScale) {
                case Time: return {self->_xValues[idx], static_cast<double>(self->_yValues[idx])};
                case LinearReverse: {
                    const double xValueBack = self->_xValues.back();
                    return {self->_xValues[idx] - xValueBack, static_cast<double>(self->_yValues[idx])};
                }
                case Linear:
                default: // linear
                    const double xValueFront = self->_xValues.front();
                    return {self->_xValues[idx] - xValueFront, static_cast<double>(self->_yValues[idx])};
                }
            };

            PlotLineContext ctx{this, axisScale};
            ImPlot::PlotLineG(label.c_str(), pointGetter, &ctx, static_cast<int>(_xValues.size()));
        } else if constexpr (gr::DataSetLike<T>) {
            // Possibly also do line color
            ImVec4 lineColor = ImGui::ColorConvertU32ToFloat4(0xFF000000 | ((color & 0xFF) << 16) | (color & 0xFF00) | ((color & 0xFF0000) >> 16));
            ImPlot::SetNextLineStyle(lineColor);

            // dimension checks
            const auto nsign = _yValues.front().extents[0];
            if (_yValues.front().extents.size() < 2) {
                return gr::work::Status::OK; // not enough dims
            }
            const auto npoints = static_cast<std::size_t>(_yValues.front().extents[1]);
            if (dataset_index == std::numeric_limits<gr::Size_t>::max()) {
                // draw all signals
                for (std::int32_t s = 0; s < nsign; ++s) {
                    const std::size_t idx = static_cast<std::size_t>(s);
                    ImPlot::PlotLine(_yValues.front().signal_names[idx].c_str(), _yValues.front().signal_values.data() + npoints * idx, static_cast<int>(npoints));
                }
            } else {
                // single sub-signal
                if (dataset_index >= static_cast<gr::Size_t>(nsign)) {
                    dataset_index = 0U;
                }
                const auto idx = static_cast<std::size_t>(dataset_index);
                ImPlot::PlotLine(_yValues.front().signal_names[idx].c_str(), _yValues.front().signal_values.data() + npoints * idx, static_cast<int>(npoints));
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

inline static auto registerImPlotSink        = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(gr::globalBlockRegistry());
inline static auto registerImPlotSinkDataSet = gr::registerBlock<opendigitizer::ImPlotSinkDataSet, float>(gr::globalBlockRegistry());
#endif
