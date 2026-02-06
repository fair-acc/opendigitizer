#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

#include <implot.h>

#include "conversion.hpp"

#include "../charts/Chart.hpp"
#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../utils/EmscriptenHelper.hpp"

namespace opendigitizer {

using charts::AxisScale;
using charts::tags::kFishyTagKey;

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
            throw gr::exception(std::format("failed to convert value for key {} - error: {}", key, convertedValue.error()), location);
        }
        return convertedValue.value_or(defaultValue);
    }
    return defaultValue;
}

using charts::tags::drawDataSetTimingEvents;
using charts::tags::drawTags;

GR_REGISTER_BLOCK(opendigitizer::ImPlotSink, float, double, gr::DataSet<float>, gr::DataSet<double>)

template<typename T>
struct ImPlotSink : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">> {
    using ValueType                   = gr::meta::fundamental_base_value_type_t<T>;
    static constexpr bool IsStreaming = std::is_arithmetic_v<T>;
    static constexpr bool IsDataSet   = gr::DataSetLike<T>;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<T> in;

    ManagedColour                                                                                                                                                 _colour;
    A<uint32_t, "plot color", gr::Doc<"RGB color for the plot">>                                                                                                  color         = 0U;
    A<gr::Size_t, "required buffer size", gr::Doc<"Minimum number of samples to retain">>                                                                         required_size = gr::DataSetLike<T> ? 10U : 2048U;
    A<std::string, "signal name", gr::Visible, gr::Doc<"Human-readable identifier for the signal">>                                                               signal_name;
    A<std::string, "abscissa quantity", gr::Visible, gr::Doc<"Physical quantity of the primary (X) axis">>                                                        abscissa_quantity = "time";
    A<std::string, "abscissa unit", gr::Visible, gr::Doc<"Unit of measurement of the primary (X) axis">>                                                          abscissa_unit     = "s";
    A<std::string, "signal quantity", gr::Visible, gr::Doc<"Physical quantity represented by the signal">>                                                        signal_quantity;
    A<std::string, "signal unit", gr::Visible, gr::Doc<"Unit of measurement for the signal values">>                                                              signal_unit;
    A<float, "signal min", gr::Doc<"Minimum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min     = std::numeric_limits<float>::lowest();
    A<float, "signal max", gr::Doc<"Maximum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max     = std::numeric_limits<float>::max();
    A<float, "sample rate", gr::Visible, gr::Doc<"Sampling frequency in Hz">, gr::Unit<"Hz">, gr::Limits<float(0), std::numeric_limits<float>::max()>>            sample_rate    = 1000.0f;
    A<gr::Size_t, "dataset index", gr::Visible, gr::Doc<"Index of the dataset, if applicable">>                                                                   dataset_index  = std::numeric_limits<gr::Size_t>::max();
    A<gr::Size_t, "history length", gr::Doc<"Number of samples retained for historical visualization">>                                                           n_history      = 3U;
    A<float, "history offset", gr::Doc<"Time offset for historical data display">, gr::Unit<"s">, gr::Limits<float(0.0), std::numeric_limits<float>::max()>>      history_offset = 0.01f;
    A<bool, "plot tags", gr::Doc<"true: draw the timing tags">>                                                                                                   plot_tags      = true;
    A<std::uint8_t, "line style", gr::Visible, gr::Doc<"Line drawing style: 0=Solid, 1=Dashed, 2=Dotted, 3=DashDot, 4=None">>                                     line_style     = 0U;
    A<float, "line width", gr::Visible, gr::Doc<"Line width in pixels">, gr::Unit<"px">, gr::Limits<float(0.1), float(10.0)>>                                     line_width     = 1.0f;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, color, required_size, signal_name, abscissa_quantity, abscissa_unit, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, //
        dataset_index, n_history, history_offset, plot_tags, line_style, line_width);

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }(); // utc timestamp of the last tag or first sample
    bool                      _xUtcOffsetInitialised = false; // set to true after first Tag with TRIGGER_TIME arrives and the _xUtcOffset is set
    gr::HistoryBuffer<double> _xValues{required_size};        // needs to be 'double' because of required ns-level UTC timestamp precision
    gr::HistoryBuffer<T>      _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    double      _sample_period = 1.0 / static_cast<double>(sample_rate);
    std::size_t _sample_count  = 0UZ;

    // shared mutex for thread-safe data access between processBulk() and draw()
    // shared_ptr so SinkAdapter can safely hold a reference that outlives the block
    std::shared_ptr<std::mutex> _dataMutex = std::make_shared<std::mutex>();

    // capacity request tracking with auto-expiry
    struct CapacityRequest {
        std::size_t                                        capacity;
        std::chrono::time_point<std::chrono::steady_clock> expiry_time;
    };
    std::unordered_map<std::string, CapacityRequest> _capacityRequests;

    // DataSet cache for float conversion
    mutable std::vector<gr::DataSet<float>> _dsCache;

    // Adapter for SinkRegistry registration (shared ownership with registry)
    std::shared_ptr<SignalSink> _sinkAdapter;

    ImPlotSink(gr::property_map initParameters) : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">>(std::move(initParameters)) {}

    ~ImPlotSink() {
        if (_sinkAdapter) {
            static_cast<SinkAdapter<ImPlotSink<T>>*>(_sinkAdapter.get())->invalidate();
            charts::SinkRegistry::instance().unregisterSink(this->unique_name);
        }
    }

    void settingsChanged(const gr::property_map& /* oldSettings */, const gr::property_map& newSettings) {
        if (newSettings.contains("color") || color == 0U) {
            if (color == 0U) {
                _colour.updateColour();
            } else {
                _colour.setColour(color);
            }
            color = _colour.colour();
        }

        {
            std::lock_guard lock(*_dataMutex);
            if (_xValues.capacity() != required_size || _yValues.capacity() != required_size) {
                _xValues.resize(required_size);
                _yValues.resize(required_size);
                _tagValues.clear();
            }
        }

        _sample_period = 1.0 / static_cast<double>(sample_rate);

        // Register with SinkRegistry (only once)
        if (!_sinkAdapter) {
            _sinkAdapter = std::make_shared<SinkAdapter<ImPlotSink<T>>>(*this);
            charts::SinkRegistry::instance().registerSink(_sinkAdapter);
        }
    }

    [[nodiscard]] std::string_view signalName() const noexcept { return signal_name.value.empty() ? this->name.value : signal_name.value; }
    [[nodiscard]] float            sampleRate() const noexcept { return sample_rate; }
    [[nodiscard]] LineStyle        lineStyle() const noexcept { return static_cast<LineStyle>(std::min(line_style.value, std::uint8_t{4})); }
    [[nodiscard]] float            lineWidth() const noexcept { return line_width; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept { return signal_quantity; }
    [[nodiscard]] std::string_view signalUnit() const noexcept { return signal_unit; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept { return abscissa_quantity; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept { return abscissa_unit; }
    [[nodiscard]] float            signalMin() const noexcept { return signal_min; }
    [[nodiscard]] float            signalMax() const noexcept { return signal_max; }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept { return _sample_count; }

    [[nodiscard]] std::size_t size() const noexcept {
        if constexpr (IsStreaming) {
            return _xValues.size();
        } else if constexpr (IsDataSet) {
            if (_yValues.empty()) {
                return 0;
            }
            const auto& ds = _yValues.at(0);
            if (ds.axis_values.empty()) {
                return 0;
            }
            return ds.axis_values[0].size();
        }
        return 0;
    }

    [[nodiscard]] double xAt(std::size_t i) const {
        if constexpr (IsStreaming) {
            return _xValues.get_span(0)[i];
        } else if constexpr (IsDataSet) {
            if (_yValues.empty()) {
                return 0.0;
            }
            const auto& ds = _yValues.at(0);
            if (ds.axis_values.empty() || i >= ds.axis_values[0].size()) {
                return 0.0;
            }
            return static_cast<double>(ds.axis_values[0][i]);
        }
        return 0.0;
    }

    [[nodiscard]] float yAt(std::size_t i) const {
        if constexpr (IsStreaming) {
            return static_cast<float>(_yValues.get_span(0)[i]);
        } else if constexpr (IsDataSet) {
            if (_yValues.empty()) {
                return 0.0f;
            }
            const auto& ds         = _yValues.at(0);
            auto        signalIdx  = static_cast<std::size_t>(dataset_index);
            const auto  numSignals = ds.size();
            if (numSignals == 0) {
                return 0.0f;
            }
            if (signalIdx >= numSignals) {
                signalIdx = 0;
            }
            auto signalValues = ds.signalValues(signalIdx);
            if (i >= signalValues.size()) {
                return 0.0f;
            }
            return static_cast<float>(signalValues[i]);
        }
        return 0.0f;
    }

    [[nodiscard]] PlotData plotData() const {
        if constexpr (IsStreaming) {
            static auto getter = +[](int idx, void* userData) -> PlotPoint {
                auto* self  = static_cast<const ImPlotSink*>(userData);
                auto  xSpan = self->_xValues.get_span(0);
                auto  ySpan = self->_yValues.get_span(0);
                return {xSpan[static_cast<std::size_t>(idx)], static_cast<double>(ySpan[static_cast<std::size_t>(idx)])};
            };
            return {getter, const_cast<ImPlotSink*>(this), static_cast<int>(_xValues.size())};
        } else if constexpr (IsDataSet) {
            static auto getter = +[](int idx, void* userData) -> PlotPoint {
                auto* self = static_cast<const ImPlotSink*>(userData);
                return {self->xAt(static_cast<std::size_t>(idx)), static_cast<double>(self->yAt(static_cast<std::size_t>(idx)))};
            };
            return {getter, const_cast<ImPlotSink*>(this), static_cast<int>(size())};
        }
        return {nullptr, nullptr, 0};
    }

    [[nodiscard]] bool        hasDataSets() const noexcept { return IsDataSet && _yValues.size() > 0; }
    [[nodiscard]] std::size_t dataSetCount() const noexcept { return IsDataSet ? _yValues.size() : 0; }

    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const {
        if constexpr (IsDataSet) {
            auto span = _yValues.get_span(0);
            if constexpr (std::is_same_v<T, gr::DataSet<float>>) {
                return span;
            } else {
                _dsCache.clear();
                for (const auto& ds : span) {
                    gr::DataSet<float> converted;
                    converted.timestamp    = ds.timestamp;
                    converted.signal_names = ds.signal_names;
                    converted.signal_units = ds.signal_units;
                    converted.axis_names   = ds.axis_names;
                    converted.axis_units   = ds.axis_units;
                    converted.extents      = ds.extents;
                    converted.signal_values.assign(ds.signal_values.begin(), ds.signal_values.end());
                    for (const auto& av : ds.axis_values) {
                        converted.axis_values.emplace_back(av.begin(), av.end());
                    }
                    _dsCache.push_back(std::move(converted));
                }
                return _dsCache;
            }
        }
        return {};
    }

    [[nodiscard]] bool hasStreamingTags() const noexcept { return IsStreaming && !_tagValues.empty(); }

    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept {
        if constexpr (IsStreaming) {
            if (_tagValues.empty()) {
                return {0.0, 0.0};
            }
            return {_tagValues.front().timestamp, _tagValues.back().timestamp};
        }
        return {0.0, 0.0};
    }

    void forEachTag(std::function<void(double, const gr::property_map&)> callback) const {
        if constexpr (IsStreaming) {
            for (const auto& tag : _tagValues) {
                callback(tag.timestamp, tag.map);
            }
        }
    }

    [[nodiscard]] double timeFirst() const noexcept {
        if constexpr (IsStreaming) {
            return _xValues.empty() ? 0.0 : _xValues.get_span(0).front();
        }
        return 0.0;
    }

    [[nodiscard]] double timeLast() const noexcept {
        if constexpr (IsStreaming) {
            return _xValues.empty() ? 0.0 : _xValues.get_span(0).back();
        }
        return 0.0;
    }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept { return static_cast<std::size_t>(required_size); }

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}) {
        std::lock_guard lock(*_dataMutex);

        auto expiry_time                       = std::chrono::steady_clock::now() + timeout;
        _capacityRequests[std::string(source)] = CapacityRequest{capacity, expiry_time};

        // recalculate required_size from all active requests (supports both increase and decrease)
        std::size_t maxCapacity = gr::DataSetLike<T> ? 10UZ : 2048UZ;
        for (const auto& [_, request] : _capacityRequests) {
            maxCapacity = std::max(maxCapacity, request.capacity);
        }
        if (maxCapacity != static_cast<std::size_t>(required_size)) {
            required_size = static_cast<gr::Size_t>(maxCapacity);
            _xValues.resize(required_size);
            _yValues.resize(required_size);
        }
    }

    void expireCapacityRequests() {
        std::lock_guard lock(*_dataMutex);
        auto            now = std::chrono::steady_clock::now();

        std::erase_if(_capacityRequests, [now](const auto& pair) { return pair.second.expiry_time < now; });

        // Recalculate required_size from remaining requests
        std::size_t maxCapacity = gr::DataSetLike<T> ? 10UZ : 2048UZ; // minimum default
        for (const auto& [_, request] : _capacityRequests) {
            maxCapacity = std::max(maxCapacity, request.capacity);
        }
        required_size = static_cast<gr::Size_t>(maxCapacity);
    }

    [[nodiscard]] SignalSink::DataRange getXRange(double tMin, double tMax) const {
        if constexpr (IsStreaming) {
            if (_xValues.empty()) {
                return {0, 0};
            }
            auto xSpan = _xValues.get_span(0);
            // Binary search for range bounds
            auto itBegin = std::lower_bound(xSpan.begin(), xSpan.end(), tMin);
            auto itEnd   = std::upper_bound(xSpan.begin(), xSpan.end(), tMax);
            if (itBegin >= itEnd) {
                return {0, 0};
            }
            std::size_t startIdx = static_cast<std::size_t>(std::distance(xSpan.begin(), itBegin));
            std::size_t count    = static_cast<std::size_t>(std::distance(itBegin, itEnd));
            return {startIdx, count};
        }
        return {0, 0};
    }

    [[nodiscard]] SignalSink::DataRange getTagRange(double tMin, double tMax) const {
        if constexpr (IsStreaming) {
            if (_tagValues.empty()) {
                return {0, 0};
            }
            // Find first tag >= tMin
            auto itBegin = std::find_if(_tagValues.begin(), _tagValues.end(), [tMin](const TagData& tag) { return tag.timestamp >= tMin; });
            // Find first tag > tMax
            auto itEnd = std::find_if(itBegin, _tagValues.end(), [tMax](const TagData& tag) { return tag.timestamp > tMax; });
            if (itBegin >= itEnd) {
                return {0, 0};
            }
            std::size_t startIdx = static_cast<std::size_t>(std::distance(_tagValues.begin(), itBegin));
            std::size_t count    = static_cast<std::size_t>(std::distance(itBegin, itEnd));
            return {startIdx, count};
        }
        return {0, 0};
    }

    [[nodiscard]] SignalSink::XRangeResult getX(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const {
        if constexpr (IsStreaming) {
            if (_xValues.empty()) {
                return {{}, 0.0, 0.0};
            }
            auto xSpan = _xValues.get_span(0);
            // Clamp requested range to available data
            double dataMin      = xSpan.front();
            double dataMax      = xSpan.back();
            double effectiveMin = std::max(tMin, dataMin);
            double effectiveMax = std::min(tMax, dataMax);

            if (effectiveMin > effectiveMax) {
                return {{}, dataMin, dataMax}; // Requested range outside data
            }

            // Binary search for range bounds
            auto itBegin = std::lower_bound(xSpan.begin(), xSpan.end(), effectiveMin);
            auto itEnd   = std::upper_bound(xSpan.begin(), xSpan.end(), effectiveMax);
            if (itBegin >= itEnd) {
                return {{}, dataMin, dataMax};
            }

            std::size_t startIdx = static_cast<std::size_t>(std::distance(xSpan.begin(), itBegin));
            std::size_t count    = static_cast<std::size_t>(std::distance(itBegin, itEnd));
            return {xSpan.subspan(startIdx, count), *itBegin, *(itEnd - 1)};
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] SignalSink::YRangeResult getY(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const {
        if constexpr (IsStreaming) {
            if (_yValues.empty() || _xValues.empty()) {
                return {{}, 0.0, 0.0};
            }

            // Get the X range first to determine indices
            auto xResult = getX(tMin, tMax);
            if (xResult.data.empty()) {
                return {{}, xResult.actual_t_min, xResult.actual_t_max};
            }

            // Calculate indices from X range
            auto        xSpan    = _xValues.get_span(0);
            std::size_t startIdx = static_cast<std::size_t>(xResult.data.data() - xSpan.data());
            std::size_t count    = xResult.data.size();

            if constexpr (std::is_same_v<ValueType, float>) {
                auto ySpan = _yValues.get_span(0);
                return {ySpan.subspan(startIdx, count), xResult.actual_t_min, xResult.actual_t_max};
            } else {
                // non-float: convert to owned float storage so the span is self-contained
                auto storage = std::make_shared<std::vector<float>>(count);
                auto ySpan   = _yValues.get_span(0);
                for (std::size_t i = 0; i < count; ++i) {
                    (*storage)[i] = static_cast<float>(ySpan[startIdx + i]);
                }
                return {std::span<const float>(*storage), xResult.actual_t_min, xResult.actual_t_max, std::move(storage)};
            }
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] SignalSink::TagRangeResult getTags(double tMin = -std::numeric_limits<double>::infinity(), double tMax = +std::numeric_limits<double>::infinity()) const {
        if constexpr (IsStreaming) {
            if (_tagValues.empty()) {
                return {{}, 0.0, 0.0};
            }

            // Get actual data bounds
            double dataMin      = _tagValues.front().timestamp;
            double dataMax      = _tagValues.back().timestamp;
            double effectiveMin = std::max(tMin, dataMin);
            double effectiveMax = std::min(tMax, dataMax);

            std::vector<SignalSink::TagEntry> result;
            for (const auto& tag : _tagValues) {
                if (tag.timestamp >= effectiveMin && tag.timestamp <= effectiveMax) {
                    result.push_back({tag.timestamp, tag.map});
                }
            }

            if (result.empty()) {
                return {{}, dataMin, dataMax};
            }
            double firstTimestamp = result.front().timestamp;
            double lastTimestamp  = result.back().timestamp;
            return {std::move(result), firstTimestamp, lastTimestamp};
        }
        return {{}, 0.0, 0.0};
    }

    [[nodiscard]] std::mutex&                 dataMutex() const { return *_dataMutex; }
    [[nodiscard]] std::shared_ptr<std::mutex> sharedDataMutex() const { return _dataMutex; }

    void pruneTags(double minX) {
        if constexpr (IsStreaming) {
            std::erase_if(_tagValues, [minX](const auto& tag) { return tag.timestamp < minX; });
        }
    }

    gr::work::Status processBulk(std::span<const T> input) noexcept {
        std::lock_guard lock(*_dataMutex);

        // Handle tag at start of span (GR4 guarantees tag on first sample if present)
        if (this->inputTagsPresent()) {
            const gr::property_map& tagMap = this->mergedInputTag().map;

            if (tagMap.contains(gr::tag::TRIGGER_TIME.shortKey())) {
                const auto   offset       = static_cast<double>(getValueOrDefault<float>(tagMap, gr::tag::TRIGGER_OFFSET.shortKey(), 0.f));
                const auto   utcTime      = static_cast<double>(getValueOrDefault<uint64_t>(tagMap, gr::tag::TRIGGER_TIME.shortKey(), 0U)) + offset;
                const double tagEventTime = utcTime * 1e-9 + offset; // [s]
                bool         tagOK        = true;

                if ((utcTime > 0.0 || tagEventTime > 0.0) && (tagEventTime > _xUtcOffset || !_xUtcOffsetInitialised)) {
                    _xUtcOffset            = tagEventTime;
                    _sample_count          = 0UZ;
                    _xUtcOffsetInitialised = true;
                } else {
                    tagOK = false; // mark fishy tag
                }

                if (plot_tags) {
                    _tagValues.push_back({.timestamp = _xUtcOffset, .map = tagMap});
                    if (!tagOK) {
                        _tagValues.back().map[std::string(kFishyTagKey)] = true;
                    }
                }
            }
        }

        // Process all samples in the span
        for (const auto& sample : input) {
            if constexpr (std::is_arithmetic_v<T>) {
                _xValues.push_back(_xUtcOffset + static_cast<double>(_sample_count) * _sample_period);
            }
            _yValues.push_back(sample);
            _sample_count++;
        }

        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        using enum AxisScale;

        if (!isTabVisible()) {
            return gr::work::Status::OK;
        }

        // Acquire lock for thread-safe data access during rendering.
        // Data is copied to GPU buffers by ImPlot API calls below.
        std::lock_guard lock(*_dataMutex);

        // Set axes from config
        {
            static constexpr ImAxis xAxes[] = {ImAxis_X1, ImAxis_X2, ImAxis_X3};
            static constexpr ImAxis yAxes[] = {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3};
            ImPlot::SetAxis(xAxes[std::clamp(getValueOrDefault<std::size_t>(config, "xAxisID", 0UZ), 0UZ, 2UZ)]);
            ImPlot::SetAxis(yAxes[std::clamp(getValueOrDefault<std::size_t>(config, "yAxisID", 0UZ), 0UZ, 2UZ)]);
        }
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
            std::span<const double>    x_values;
            std::span<const ValueType> y_values;
            AxisScale                  axis_scale;
            ValueType                  y_offset{0};
        };

        constexpr auto pointGetter = +[](int signedIndex, void* user_data) -> ImPlotPoint {
            std::size_t idx = static_cast<std::size_t>(signedIndex);
            auto*       ctx = static_cast<PlotLineContext*>(user_data);

            double xVal = ctx->x_values[idx];
            double yVal = static_cast<double>(ctx->y_values[idx] + ctx->y_offset);

            switch (ctx->axis_scale) {
            case Time: return {xVal, yVal};

            case LinearReverse: {
                if constexpr (gr::DataSetLike<T>) {
                    return {xVal, yVal};
                } else {
                    // fundamental types
                    double xMax = ctx->x_values.back();
                    return {xVal - xMax, yVal};
                }
            }

            case Linear:
            case Log10:
            case SymLog:
            default: {
                if constexpr (gr::DataSetLike<T>) {
                    return {xVal, yVal};
                } else {
                    // fundamental types
                    double xMin = ctx->x_values.front();
                    return {xVal - xMin, yVal};
                }
            }
            }
        };

        auto lineColor = ImGui::ColorConvertU32ToFloat4(rgbToImGuiABGR(_colour.colour()));
        if constexpr (std::is_arithmetic_v<T>) {
            ImPlot::SetNextLineStyle(lineColor);

            auto [minX, maxX] = std::ranges::minmax(_xValues);
            // draw tags before data (data is drawn on top)
            if (getValueOrDefault<bool>(config, "draw_tag", false)) {
                ImVec4 tagColor = lineColor;
                tagColor.w *= 0.35f; // semi-transparent tags
                std::erase_if(_tagValues, [minX](const auto& tag) { return tag.timestamp < minX; });
                drawTags(
                    [&](auto&& fn) {
                        for (const auto& t : _tagValues) {
                            fn(t.timestamp, t.map);
                        }
                    },
                    axisScale, minX, maxX, tagColor);
            }

            PlotLineContext ctx{_xValues.get_span(0UZ), _yValues.get_span(0UZ), axisScale, ValueType{0}};
            ImPlot::PlotLineG(label.c_str(), pointGetter, &ctx, static_cast<int>(_xValues.size())); // limited to int, even if x_values can have long values
        } else if constexpr (gr::DataSetLike<T>) {
            const std::size_t nMax = std::min(_yValues.size(), static_cast<std::size_t>(n_history));
            for (std::size_t historyIdx = nMax; historyIdx-- > 0;) {
                // draw newest DataSet last -> on top
                const gr::DataSet<ValueType>& dataSet = _yValues.at(historyIdx);
                // dimension checks
                const std::size_t nsignals = dataSet.size();
                if (dataSet.extents.size() != 1UZ && nsignals < 1UZ) {
                    continue; // not 1D signal or not enough signals
                }

                lineColor.w = std::max(0.f, 1.0f - static_cast<float>(historyIdx) * 1.f / static_cast<float>(nMax));
                ImPlot::SetNextLineStyle(lineColor);

                std::vector<double> xAxisDouble;
                if constexpr (!std::is_same_v<ValueType, double>) {
                    // TODO: find a smarter zero-copy solution
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
                    for (std::size_t sigIdx = 0UZ; sigIdx < nsignals; ++sigIdx) {
                        ImPlot::SetNextLineStyle(lineColor);
                        PlotLineContext ctx{xAxisDouble, dataSet.signalValues(sigIdx), axisScale, static_cast<ValueType>(sigIdx + historyIdx) * baseOffset};
                        ImPlot::PlotLineG(historyIdx == 0UZ ? dataSet.signal_names[sigIdx].c_str() : "", pointGetter, &ctx, static_cast<int>(npoints));
                    }
                } else {
                    // single sub-signal
                    if (dataset_index >= static_cast<gr::Size_t>(nsignals)) {
                        dataset_index = 0U;
                    }
                    const auto sigIdx = static_cast<std::size_t>(dataset_index);
                    ImPlot::SetNextLineStyle(lineColor);
                    PlotLineContext ctx{xAxisDouble, dataSet.signalValues(sigIdx), axisScale};
                    ImPlot::PlotLineG(dataSet.signal_names[sigIdx].c_str(), pointGetter, &ctx, static_cast<int>(npoints));
                }
            }
        } //  if constexpr (gr::DataSetLike<T>) { .. }

        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

inline static auto registerImPlotSink = gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>, gr::UncertainValue<float>>(gr::globalBlockRegistry());

#endif
