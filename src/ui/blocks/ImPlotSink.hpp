#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include "../components/SignalColours.hpp"

#include <implot.h>

#include "conversion.hpp"
#include <PluginPaths.hpp>

#include "../charts/Chart.hpp"
#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../utils/Browser.hpp"

namespace gr::od {

using charts::AxisScale;
using charts::tags::kFishyTagKey;

struct TagData {
    double           timestamp;
    gr::property_map map;
};

template<typename T>
T getValueOrDefault(const gr::property_map& map, std::string_view key, const T& defaultValue) noexcept {
    if (auto it = map.find(key); it != map.end()) {
        return gr::pmt::convert_safely<T, false>(it->second).value_or(defaultValue);
    }
    return defaultValue;
}

using charts::tags::drawDataSetTimingEvents;
using charts::tags::drawTags;

template<typename T>
struct ImPlotSink : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::Content, "ImGui">> {
    using ValueType = gr::meta::fundamental_base_value_type_t<T>;
    using LineStyle = gr::od::LineStyle;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    static constexpr bool kIsStreaming = std::is_arithmetic_v<T>;
    static constexpr bool kIsDataSet   = gr::DataSetLike<T>;
    static constexpr bool kIsTensor    = gr::TensorLike<T>;
    static constexpr bool kIsEvents    = std::is_same_v<T, gr::property_map>;

    static constexpr SignalKind kSignalKind = []() consteval {
        if constexpr (kIsEvents) {
            return SignalKind::Events;
        } else if constexpr (kIsTensor) {
            return SignalKind::Image2D;
        } else if constexpr (kIsDataSet) {
            return SignalKind::Dataset1D;
        } else {
            return SignalKind::Streaming;
        }
    }();
    static constexpr gr::Size_t kDefaultRequiredSize = []() consteval -> gr::Size_t {
        if constexpr (kIsTensor) {
            return 1U;
        } else if constexpr (kIsEvents) {
            return 64U;
        } else if constexpr (kIsDataSet) {
            return static_cast<gr::Size_t>(charts::Chart::kDefaultDataSetHistorySize);
        } else {
            return 2048U;
        }
    }();

    std::conditional_t<kIsTensor, gr::TensorPortIn<ValueType>, std::conditional_t<kIsEvents, gr::EventPortIn, gr::PortIn<T>>> in;

    A<bool, "visible", gr::Doc<"Whether the signal is drawn in charts">>                                 visible = true;
    A<std::pmr::string, "name", gr::Doc<"Human-readable identifier for the signal">, gr::Visible>        signal_name;
    A<uint32_t, "colour", gr::Unit<"Color">, gr::Doc<"RGB colour for the plot">>                         color      = 0U;
    A<LineStyle, "style", gr::Doc<"Line drawing style: 0=Solid, 1=Dashed, 2=Dotted, 3=DashDot, 4=None">> line_style = LineStyle::Solid;
    A<float, "width", gr::Doc<"Line width in pixels">, gr::Unit<"px">, gr::Limits<0.1f, 10.f>>           line_width = 1.0f;
    A<std::pmr::string, "quantity", gr::Doc<"Physical quantity represented by the signal">, gr::Visible> signal_quantity;
    A<std::pmr::string, "unit", gr::Doc<"Unit of measurement for the signal values">, gr::Visible>       signal_unit;

    A<float, "min", gr::Doc<"Minimum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min = std::numeric_limits<float>::lowest();
    A<float, "max", gr::Doc<"Maximum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max = std::numeric_limits<float>::max();

    A<float, "sample rate", gr::Doc<"Sampling frequency in Hz">, gr::Unit<"Hz">, gr::Limits<float(0), std::numeric_limits<float>::max()>>                             sample_rate       = 1000.0f;
    A<std::string, "abscissa quantity", gr::Doc<"Physical quantity of the primary (X) axis">>                                                                         abscissa_quantity = "time";
    A<std::string, "abscissa unit", gr::Doc<"Unit of measurement of the primary (X) axis">>                                                                           abscissa_unit     = "s";
    A<gr::Size_t, "required buffer size", gr::Doc<"Minimum number of samples to retain">>                                                                             required_size     = kDefaultRequiredSize;
    A<gr::Size_t, "dataset index", gr::Doc<"Index of the dataset, if applicable">>                                                                                    dataset_index     = std::numeric_limits<gr::Size_t>::max();
    A<gr::Size_t, "history length", gr::Doc<"Number of samples retained for historical visualization">>                                                               n_history         = 3U;
    A<float, "history offset", gr::Doc<"Time offset for historical data display">, gr::Unit<"s">, gr::Limits<float(0.0), std::numeric_limits<float>::max()>>          history_offset    = 0.01f;
    A<bool, "plot tags", gr::Doc<"true: draw the timing tags">>                                                                                                       plot_tags         = true;
    A<gr::Size_t, "max backlog", gr::Limits<0U, 256U>, gr::Doc<"Tensor frames queued for in-order display; 0 = newest only (skipped frames are shown by the chart)">> max_backlog       = 0U;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, visible, signal_name, color, line_style, line_width, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, abscissa_quantity, abscissa_unit, required_size, //
        dataset_index, n_history, history_offset, plot_tags, max_backlog);

    ManagedColour _colour;

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }(); // utc timestamp of the last tag or first sample
    bool                      _xUtcOffsetInitialised = false; // after the first kTriggerTime tag
    gr::HistoryBuffer<double> _xValues{required_size};        // needs to be 'double' because of required ns-level UTC timestamp precision
    gr::HistoryBuffer<T>      _yValues{required_size};
    std::deque<TagData>       _tagValues{};

    double      _samplePeriod     = 1.0 / static_cast<double>(sample_rate);
    std::size_t _sampleCount      = 0UZ;
    std::size_t _totalSampleCount = 0UZ;

    // shared with the SinkAdapter, which may outlive the block
    std::shared_ptr<std::mutex> _dataMutex = std::make_shared<std::mutex>();

    CapacityRequests _capacityRequests{.minimum = gr::DataSetLike<T> ? charts::Chart::kDefaultDataSetHistorySize : 2048UZ};

    mutable std::vector<gr::DataSet<float>> _floatConvertedDataSets;

    std::deque<std::conditional_t<kIsTensor, T, std::monostate>> _frames;               // Tensor<T> sinks: the newest max_backlog + 1 frames
    std::uint64_t                                                _frameGeneration = 0U; // of _frames.back(), counting every received frame
    std::vector<gr::property_map>                                _events;               // event sinks: the newest required_size events, owned
    std::atomic<std::uint64_t>                                   _eventGeneration = 0U; // read by charts without the data guard
    std::array<ImageAxis, 2>                                     _axes{};               // Tensor<T> sinks: from the latest gr:axis_* tags

    std::shared_ptr<SignalSink> _sinkAdapter;

    ImPlotSink(gr::property_map initParameters) : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::Content, "ImGui">>(std::move(initParameters)) {}

    ~ImPlotSink() {
        if (_sinkAdapter) {
            static_cast<SinkAdapter<ImPlotSink<T>>*>(_sinkAdapter.get())->invalidate();
            charts::SinkRegistry::instance().unregisterSink(this->unique_name);
        }
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        if constexpr (kIsEvents) {
            std::lock_guard lock(*_dataMutex);
            for (const gr::property_map_view& event : input) {
                _events.emplace_back(event);
            }
            const std::size_t excess = _events.size() - std::min(_events.size(), static_cast<std::size_t>(required_size.value));
            _events.erase(_events.begin(), _events.begin() + static_cast<std::ptrdiff_t>(excess));
            _eventGeneration.fetch_add(input.size(), std::memory_order_release);
            std::ignore = input.consume(input.size());
            return gr::work::Status::OK;
        } else {
            return appendSamples(input);
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

        _samplePeriod = 1.0 / static_cast<double>(sample_rate);

        if (!_sinkAdapter) {
            _sinkAdapter = std::make_shared<SinkAdapter<ImPlotSink<T>>>(*this);
            charts::SinkRegistry::instance().registerSink(_sinkAdapter);
        }
    }

    [[nodiscard]] std::span<const gr::property_map> events() const noexcept { return _events; }
    [[nodiscard]] std::uint64_t                     eventGeneration() const noexcept { return _eventGeneration.load(std::memory_order_acquire); }

    [[nodiscard]] ImageFrame imageFrame(std::uint64_t next = 0U) const {
        if constexpr (kIsTensor) {
            if (_frames.empty()) {
                return {};
            }
            const std::uint64_t oldest     = _frameGeneration + 1U - _frames.size();
            const std::uint64_t generation = next == 0U ? _frameGeneration : std::clamp(next, oldest, _frameGeneration);
            const T&            frame      = *std::next(_frames.begin(), static_cast<std::ptrdiff_t>(generation - oldest));
            if (frame.rank() == 2UZ) {
                return {.pixels = std::span<const ValueType>(frame.data(), frame.size()), .rows = frame.extents()[0], .columns = frame.extents()[1], .generation = generation, .axes = _axes};
            }
        }
        return {};
    }

    [[nodiscard]] std::string_view signalName() const noexcept { return signal_name.value.empty() ? std::string_view(this->name.value) : std::string_view(signal_name.value); }
    [[nodiscard]] float            sampleRate() const noexcept { return sample_rate; }
    [[nodiscard]] float            lineWidth() const noexcept { return line_width; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept { return signal_quantity; }
    [[nodiscard]] std::string_view signalUnit() const noexcept { return signal_unit; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept { return abscissa_quantity; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept { return abscissa_unit; }

    void updateAxisMetadataFromDataSet(const T& ds) {
        if constexpr (kIsDataSet) {
            const auto autoUpdate = this->settings().autoUpdateParameters(this->settings().activeContext());
            if (std::ranges::contains(autoUpdate, std::string_view{"abscissa_quantity"}) && !ds.axis_names.empty() && !ds.axis_names[0].empty()) {
                abscissa_quantity = ds.axis_names[0];
            }
            if (std::ranges::contains(autoUpdate, std::string_view{"abscissa_unit"}) && !ds.axis_units.empty() && !ds.axis_units[0].empty()) {
                abscissa_unit = ds.axis_units[0];
            }
            if (std::ranges::contains(autoUpdate, std::string_view{"signal_quantity"}) && !ds.signal_quantities.empty() && !ds.signal_quantities[0].empty()) {
                signal_quantity = ds.signal_quantities[0];
            }
            if (std::ranges::contains(autoUpdate, std::string_view{"signal_unit"}) && !ds.signal_units.empty() && !ds.signal_units[0].empty()) {
                signal_unit = ds.signal_units[0];
            }
        }
    }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept { return _totalSampleCount; }

    [[nodiscard]] std::size_t size() const noexcept {
        if constexpr (kIsStreaming) {
            return _xValues.size();
        } else if constexpr (kIsDataSet) {
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
        if constexpr (kIsStreaming) {
            return _xValues.get_span(0)[i];
        } else if constexpr (kIsDataSet) {
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
        if constexpr (kIsStreaming) {
            return static_cast<float>(_yValues.get_span(0)[i]);
        } else if constexpr (kIsDataSet) {
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

    [[nodiscard]] bool        hasDataSets() const noexcept { return kIsDataSet && _yValues.size() > 0; }
    [[nodiscard]] std::size_t dataSetCount() const noexcept { return kIsDataSet ? _yValues.size() : 0; }
    [[nodiscard]] SignalKind  signalKind() const noexcept { return kSignalKind; }

    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const {
        if constexpr (kIsDataSet) {
            auto span = _yValues.get_span(0);
            if constexpr (std::is_same_v<T, gr::DataSet<float>>) {
                return span;
            } else {
                _floatConvertedDataSets.clear();
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
                    _floatConvertedDataSets.push_back(std::move(converted));
                }
                return _floatConvertedDataSets;
            }
        }
        return {};
    }

    void forEachTag(std::function<void(double, const gr::property_map&)> callback) const {
        if constexpr (kIsStreaming) {
            for (const auto& tag : _tagValues) {
                callback(tag.timestamp, tag.map);
            }
        }
    }

    [[nodiscard]] double timeLast() const noexcept {
        if constexpr (kIsStreaming) {
            return _xValues.empty() ? 0.0 : _xValues.get_span(0).back();
        }
        return 0.0;
    }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept { return static_cast<std::size_t>(required_size); }

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}, CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) {
        std::lock_guard lock(*_dataMutex);
        resizeHistory(_capacityRequests.request(source, capacity, now + timeout));
    }

    void expireCapacityRequests(CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) {
        std::lock_guard lock(*_dataMutex);
        resizeHistory(_capacityRequests.expire(now));
    }

    void resizeHistory(std::size_t capacity) {
        if (capacity != static_cast<std::size_t>(required_size)) {
            required_size = static_cast<gr::Size_t>(capacity);
            _xValues.resize(required_size);
            _yValues.resize(required_size);
        }
    }

    [[nodiscard]] std::mutex&                 dataMutex() const { return *_dataMutex; }
    [[nodiscard]] std::shared_ptr<std::mutex> sharedDataMutex() const { return _dataMutex; }

    void updateAxes(const gr::property_map& tag) {
        const auto field   = [&tag](std::string_view key) { return tag.find_value(key).value_or(gr::pmt::Value{}); };
        const auto doubles = [&field](std::string_view key) {
            const gr::pmt::Value value = field(key);
            const auto           view  = value.get_if<gr::TensorView<double>>();
            return view ? std::vector<double>(view->begin(), view->end()) : std::vector<double>{};
        };
        const auto strings = [&field](std::string_view key) {
            const gr::pmt::Value value = field(key);
            const auto           view  = value.get_if<gr::TensorView<gr::pmt::Value>>();
            return view ? *view | std::views::transform([](const gr::pmt::Value& v) { return v.value_or(std::string{}); }) | std::ranges::to<std::vector>() : std::vector<std::string>{};
        };
        const std::vector<double>      origins = doubles(gr::tag::kAxisOrigin.key());
        const std::vector<double>      steps   = doubles(gr::tag::kAxisStep.key());
        const std::vector<std::string> names   = strings(gr::tag::kAxisName.key());
        const std::vector<std::string> units   = strings(gr::tag::kAxisUnit.key());
        for (std::size_t d = 0UZ; d < _axes.size(); ++d) {
            if (d < origins.size()) {
                _axes[d].origin = origins[d];
            }
            if (d < steps.size()) {
                _axes[d].step = steps[d];
            }
            if (d < names.size()) {
                _axes[d].name = names[d];
            }
            if (d < units.size()) {
                _axes[d].unit = units[d];
            }
        }
    }

    gr::work::Status appendSamples(gr::InputSpanLike auto& input) noexcept {
        std::lock_guard lock(*_dataMutex);
        if constexpr (kIsTensor) {
            for (const auto& [relIndex, tagMapRef] : input.tags()) {
                updateAxes(tagMapRef.get());
            }
            const std::size_t keep = static_cast<std::size_t>(max_backlog.value) + 1UZ;
            for (std::size_t i = input.size() - std::min(input.size(), keep); i < input.size(); ++i) {
                T recycled{}; // the dropped oldest frame's buffer, reused when its extents match
                if (_frames.size() >= keep) {
                    recycled = std::move(_frames.front());
                    _frames.pop_front();
                }
                const auto& frame = input[i];
                if (!std::ranges::equal(recycled.extents(), frame.extents())) {
                    recycled.resize(frame.extents());
                }
                std::copy_n(frame.data(), frame.size(), recycled.data());
                _frames.push_back(std::move(recycled));
            }
            while (_frames.size() > keep) {
                _frames.pop_front();
            }
            _frameGeneration += input.size();
            _sampleCount += input.size();
            _totalSampleCount += input.size();
            return gr::work::Status::OK;
        }

        for (const auto& [relIndex, tagMapRef] : input.tags()) {
            if (relIndex < 0) {
                continue; // skip unconsumed pre-span tags
            }
            if (relIndex > 0) {
                break; // only process tags at the start of the span
            }
            const gr::property_map& tagMap = tagMapRef.get();
            if (tagMap.contains(gr::tag::kTriggerTime)) {
                const auto   offsetSeconds = static_cast<double>(getValueOrDefault<float>(tagMap, gr::tag::kTriggerOffset, 0.f));
                const auto   triggerTimeNs = getValueOrDefault<std::uint64_t>(tagMap, gr::tag::kTriggerTime, 0U);
                const double tagEventTime  = static_cast<double>(triggerTimeNs) * 1e-9 + offsetSeconds;
                bool         tagOK         = true;

                const bool firstTrigger = !_xUtcOffsetInitialised;

                if ((triggerTimeNs > 0U || tagEventTime > 0.0) && (firstTrigger || tagEventTime > _xUtcOffset)) {
                    if constexpr (kIsStreaming) {
                        if (firstTrigger) {
                            // the provisional history rebased so that the next sample starts at the first valid trigger time
                            const double      timestampShift = tagEventTime - (_xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod);
                            const std::size_t historySize    = _xValues.size();
                            for (std::size_t i = 0UZ; i < historySize; ++i) {
                                const double timestamp = _xValues.front();
                                _xValues.pop_front();
                                _xValues.push_back(timestamp + timestampShift);
                            }
                            for (auto& tag : _tagValues) {
                                tag.timestamp += timestampShift;
                            }
                        }
                        _xUtcOffset            = tagEventTime;
                        _sampleCount           = 0UZ;
                        _xUtcOffsetInitialised = true;
                    }
                } else {
                    tagOK = false; // mark fishy tag
                }

                if (plot_tags && kIsStreaming) {
                    _tagValues.push_back({.timestamp = _xUtcOffset, .map = tagMap});
                    if (!tagOK) {
                        _tagValues.back().map[std::pmr::string(kFishyTagKey)] = true;
                    }
                }
            }

            // surface dropped-sample tags at the current position so the gap is visible in the plot
            if (plot_tags && kIsStreaming && tagMap.contains(gr::tag::kNDroppedSamples)) {
                _tagValues.push_back({.timestamp = _xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod, .map = tagMap});
            }
        }

        for (const auto& sample : input) {
            if constexpr (std::is_arithmetic_v<T>) {
                _xValues.push_back(_xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod);
            }
            _yValues.push_back(sample);
            _sampleCount++;
            _totalSampleCount++;

            if constexpr (kIsDataSet) {
                updateAxisMetadataFromDataSet(sample);
            }
        }

        if constexpr (kIsStreaming) {
            if (!_xValues.empty()) {
                const double oldestSampleTime = _xValues.get_span(0).front();
                std::erase_if(_tagValues, [oldestSampleTime](const auto& tag) { return tag.timestamp < oldestSampleTime; });
            }
        }

        return gr::work::Status::OK;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        if constexpr (kIsEvents) {
            return gr::work::Status::OK;
        } else {
            return drawSamples(config);
        }
    }

    gr::work::Status drawSamples(const gr::property_map& config) noexcept {
        using enum AxisScale;
        if constexpr (kIsTensor) {
            return gr::work::Status::OK;
        }

        if (!isTabVisible()) {
            return gr::work::Status::OK;
        }

        // the ImPlot calls below copy the data to GPU buffers
        std::lock_guard lock(*_dataMutex);

        {
            static constexpr ImAxis kXAxes[] = {ImAxis_X1, ImAxis_X2, ImAxis_X3};
            static constexpr ImAxis kYAxes[] = {ImAxis_Y1, ImAxis_Y2, ImAxis_Y3};
            ImPlot::SetAxis(kXAxes[std::clamp(getValueOrDefault<std::size_t>(config, "xAxisID", 0UZ), 0UZ, 2UZ)]);
            ImPlot::SetAxis(kYAxes[std::clamp(getValueOrDefault<std::size_t>(config, "yAxisID", 0UZ), 0UZ, 2UZ)]);
        }
        std::string     scaleStr  = config.contains("scale") ? config.find_value("scale").value_or(gr::pmt::Value{}).value_or(std::string{"Linear"}) : std::string{"Linear"};
        const AxisScale axisScale = gr::meta::parseEnum<AxisScale>(Digitizer::trimWhitespace(scaleStr)).value_or(AxisScale::Linear);
        const char*     label     = signal_name.value.empty() ? this->name.value.c_str() : signal_name.value.c_str();
        if (_yValues.empty()) {
            // plot one single dummy value so that the sink shows up in the plot legend
            double v = {};
            ImPlot::PlotLine(label, &v, 1);
            return gr::work::Status::OK;
        }

        struct PlotLineContext {
            std::span<const double>    xValues;
            std::span<const ValueType> yValues;
            AxisScale                  axisScale;
            ValueType                  yOffset{0};
        };

        constexpr auto pointGetter = +[](int signedIndex, void* userData) -> ImPlotPoint {
            std::size_t idx = static_cast<std::size_t>(signedIndex);
            auto*       ctx = static_cast<PlotLineContext*>(userData);

            double xVal = ctx->xValues[idx];
            double yVal = static_cast<double>(ctx->yValues[idx] + ctx->yOffset);

            switch (ctx->axisScale) {
            case Time: return {xVal, yVal};

            case LinearReverse: {
                if constexpr (gr::DataSetLike<T>) {
                    return {xVal, yVal};
                } else {
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
                } else {
                    double xMin = ctx->xValues.front();
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
                tagColor.w *= LookAndFeel::instance().chartStyle.tagAlpha;
                drawTags(
                    [&](auto&& fn) {
                        for (const auto& t : _tagValues) {
                            fn(t.timestamp, t.map);
                        }
                    },
                    axisScale, minX, maxX, tagColor);
            }

            PlotLineContext ctx{_xValues.get_span(0UZ), _yValues.get_span(0UZ), axisScale, ValueType{0}};
            ImPlot::PlotLineG(label, pointGetter, &ctx, static_cast<int>(_xValues.size())); // limited to int, even if xValues can have long values
        } else if constexpr (gr::DataSetLike<T>) {
            const std::size_t nMax = std::min(_yValues.size(), static_cast<std::size_t>(n_history));
            for (std::size_t historyIdx = nMax; historyIdx-- > 0;) {
                // draw newest DataSet last -> on top
                const gr::DataSet<ValueType>& dataSet  = _yValues.at(historyIdx);
                const std::size_t             nsignals = dataSet.size();
                if (dataSet.extents.size() != 1UZ && nsignals < 1UZ) {
                    continue; // not 1D signal or not enough signals
                }

                lineColor.w = std::max(0.f, 1.0f - static_cast<float>(historyIdx) * 1.f / static_cast<float>(nMax));
                ImPlot::SetNextLineStyle(lineColor);

                std::vector<double> xAxisDouble;
                if constexpr (!std::is_same_v<ValueType, double>) {
                    if (!dataSet.axis_values.empty()) {
                        auto xSpan = dataSet.axisValues(0UZ);
                        xAxisDouble.assign(xSpan.begin(), xSpan.end());
                    }
                }

                // draw tags before data (data is drawn on top)
                if (historyIdx == 0UZ && getValueOrDefault<bool>(config, "draw_tag", false)) {
                    ImVec4 tagColor = lineColor;
                    tagColor.w *= std::max(0.35f - 0.05f * static_cast<float>(historyIdx), 0.f); // semi-transparent
                    drawDataSetTimingEvents(dataSet, tagColor);
                }

                // NOTE: npoints is long int, while ImPlot lines are limited to int
                const auto npoints = cast_to_signed(dataSet.axisValues(0UZ).size());
                if (dataset_index == std::numeric_limits<gr::Size_t>::max()) {
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
        }

        return gr::work::Status::OK;
    }
};

template<typename... T>
void registerPlotSinks(gr::BlockRegistry& registry) {
    (plotSinkTypes().insert_or_assign(registerWithIdPrefix<ImPlotSink<T>>(registry, kOpendigitizerIdPrefix), gr::meta::is_instantiation_of<T, gr::DataSet>), ...);
}

} // namespace gr::od

#endif
