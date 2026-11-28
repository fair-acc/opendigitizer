#ifndef OPENDIGITIZER_SIGNALSINK_HPP
#define OPENDIGITIZER_SIGNALSINK_HPP

#include "BlockTypes.hpp"
#include "gnuradio-4.0/basic/ConverterBlocks.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/Tensor.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace gr::od {

/// minimum history capacities requested by named consumers, each until its expiry
struct CapacityRequests {
    using clock = std::chrono::steady_clock;
    struct Request {
        std::size_t       capacity;
        clock::time_point expiry;
    };

    std::size_t                              minimum = 0UZ;
    std::unordered_map<std::string, Request> _requests{};

    std::size_t request(std::string_view source, std::size_t capacity, clock::time_point expiry) {
        _requests[std::string(source)] = Request{capacity, expiry};
        return largest();
    }

    std::size_t expire(clock::time_point now) {
        std::erase_if(_requests, [now](const auto& entry) { return entry.second.expiry < now; });
        return largest();
    }

    [[nodiscard]] bool empty() const noexcept { return _requests.empty(); }

    [[nodiscard]] std::size_t largest() const noexcept {
        std::size_t result = minimum;
        for (const auto& [_, request] : _requests) {
            result = std::max(result, request.capacity);
        }
        return result;
    }
};

enum class LineStyle : std::uint8_t {
    Solid,
    Dashed,
    Dotted,
    DashDot,
    None // markers only
};

/// held while reading a sink's data
using DataGuard = std::unique_lock<std::mutex>;

/// one tensor dimension's coordinates, from the producer's gr:axis_* tags; without them, the cell index
struct ImageAxis {
    double      origin = 0.0; // of cell 0
    double      step   = 1.0;
    std::string name;
    std::string unit;
};

[[nodiscard]] constexpr double axisCoordinate(const ImageAxis& axis, std::size_t cell) noexcept { return axis.origin + axis.step * static_cast<double>(cell); }

/// one 2D frame of a sink, row-major {rows, columns}: a Tensor frame; a rank-2 DataSet's image, the signal dataset_index selects; or a rank-1 DataSet's signals as rows.
/// Valid while holding the sink's dataGuard()
struct FrameView {
    std::variant<std::monostate, std::span<const std::uint32_t>, std::span<const float>> values; // uint32: packed 0xAARRGGBB
    std::size_t                                                                          rows       = 0UZ;
    std::size_t                                                                          columns    = 0UZ;
    std::uint64_t                                                                        generation = 0U;   // changes with every new frame
    std::array<ImageAxis, 2>                                                             axes{};            // {rows, columns}
    std::span<const float>                                                               columnCoordinates; // a DataSet's axis values, not necessarily uniform
    const gr::DataSet<float>*                                                            dataSet = nullptr; // null unless the frame is a DataSet's

    [[nodiscard]] std::span<const float> row(std::size_t r) const noexcept {
        const auto* floats = std::get_if<std::span<const float>>(&values);
        return floats && r < rows ? floats->subspan(r * columns, columns) : std::span<const float>{};
    }
};

/// a streaming sink's held samples, oldest first; valid while holding the sink's dataGuard()
struct SampleWindow {
    using Values = std::variant<std::span<const float>, std::span<const double>>;

    std::span<const double> x; // UTC seconds
    Values                  y;
    Values                  yError;      // each y's standard deviation; empty for a sink without uncertainties
    std::size_t             first = 0UZ; // the index of x[0] among every sample received, for incremental uploads

    [[nodiscard]] std::size_t size() const noexcept { return x.size(); }
    [[nodiscard]] double      yAt(std::size_t i) const noexcept {
        return std::visit([i](auto values) { return static_cast<double>(values[i]); }, y);
    }
    [[nodiscard]] bool hasErrors() const noexcept {
        return std::visit([](auto errors) { return !errors.empty(); }, yError);
    }
    [[nodiscard]] double yErrorAt(std::size_t i) const noexcept {
        return std::visit([i](auto errors) { return static_cast<double>(errors[i]); }, yError);
    }

    /// all when fewer than `count` are held
    [[nodiscard]] SampleWindow newest(std::size_t count) const noexcept {
        const std::size_t skip = size() - std::min(count, size());
        const auto        tail = [skip](auto values) -> Values { return values.empty() ? values : values.subspan(skip); };
        return {.x = x.subspan(skip), .y = std::visit(tail, y), .yError = std::visit(tail, yError), .first = first + skip};
    }
};

/// a complex stream's imaginary part is a signal of its own, registered as `<unique name>.im` beside the real part
inline constexpr std::string_view kImaginaryPartSuffix = ".im";

/// one DataSet's standard deviations, laid out as its signal_values and axis_values
struct DataSetErrors {
    std::vector<float>              signals;
    std::vector<std::vector<float>> axes;
};

struct DatedMap {
    double           utcSeconds = 0.0;
    gr::property_map map;
};

/// a chart signal: the one store of a sink block, shared by every chart that shows it.
/// The block creates it, writes on the scheduler thread holding dataGuard(), and copies its settings into `metadata`;
/// charts read holding dataGuard(), the metadata without it, and keep it after the block is gone, until they rebind by name.
struct SignalSink {
    struct Metadata {
        std::string   signalName; // empty: the sink's name
        std::string   signalQuantity;
        std::string   signalUnit;
        std::string   abscissaQuantity = "time";
        std::string   abscissaUnit     = "s";
        std::uint32_t color            = 0xFFFFFFU;
        float         lineWidth        = 1.0f;
        float         sampleRate       = 1.0f;
        bool          visible          = true;
        std::size_t   datasetIndex     = std::numeric_limits<std::size_t>::max(); // the DataSet row 1D charts draw; out of range: row 0
    };

    struct Config {
        SignalKind  kind = SignalKind::Streaming;
        std::string uniqueName;
        std::string name;
        std::size_t capacity      = 2048UZ; // samples, DataSets or events held until a chart asks (required_size)
        std::size_t capacityFloor = 0UZ;    // a chart's request is raised to at least this
        bool        doubleSamples = false;
        bool        withErrors    = false; // one standard deviation per value
    };

    using Samples = std::variant<gr::HistoryBuffer<float>, gr::HistoryBuffer<double>>;
    using Frames  = std::variant<std::monostate, std::deque<gr::Tensor<float>>, std::deque<gr::Tensor<std::uint32_t>>>; // a double tensor is kept as float

    std::string _uniqueName;
    std::string _name;
    SignalKind  _kind;
    Metadata    metadata;

    mutable std::mutex _mutex;
    std::size_t        _configuredCapacity;
    std::size_t        _capacity;
    CapacityRequests   _capacityRequests;

    gr::HistoryBuffer<double>       _xValues; // streaming samples' UTC seconds, oldest first; double: ns precision
    Samples                         _yValues;
    Samples                         _yErrors;
    std::deque<DatedMap>            _tags;     // streaming sinks: at their sample's UTC time
    std::vector<gr::DataSet<float>> _dataSets; // oldest first
    std::vector<DataSetErrors>      _dataSetErrors;
    bool                            _withErrors;
    Frames                          _frames;                // tensor sinks: the newest max_backlog + 1 frames
    std::uint64_t                   _frameGeneration = 0U;  // of the newest frame, counting every frame received
    std::array<ImageAxis, 2>        _axes{};                // tensor sinks: {rows, columns}, from the gr:axis_* tags
    std::vector<DatedMap>           _events;                // event and tag sinks: the newest, oldest first, at their arrival time
    std::atomic<std::uint64_t>      _eventGeneration  = 0U; // read without the guard
    std::size_t                     _totalSampleCount = 0UZ;

    explicit SignalSink(Config config)
        : _uniqueName(std::move(config.uniqueName)), _name(std::move(config.name)), _kind(config.kind), _configuredCapacity(std::max(config.capacity, 1UZ)), _capacity(_configuredCapacity), _capacityRequests{.minimum = config.capacityFloor}, //
          _xValues(_kind == SignalKind::Streaming ? _capacity : 1UZ),                                                                                                                                                                            //
          _yValues(makeSamples(config.doubleSamples, _xValues.capacity())), _yErrors(makeSamples(config.doubleSamples, config.withErrors ? _xValues.capacity() : 1UZ)), _withErrors(config.withErrors) {}

    [[nodiscard]] static Samples makeSamples(bool doubles, std::size_t capacity) { return doubles ? Samples(std::in_place_type<gr::HistoryBuffer<double>>, capacity) : Samples(std::in_place_type<gr::HistoryBuffer<float>>, capacity); }

    SignalSink(const SignalSink&)            = delete;
    SignalSink& operator=(const SignalSink&) = delete;

    [[nodiscard]] std::string_view name() const noexcept { return _name; }
    [[nodiscard]] std::string_view uniqueName() const noexcept { return _uniqueName; }
    [[nodiscard]] SignalKind       signalKind() const noexcept { return _kind; }

    [[nodiscard]] std::string_view signalName() const noexcept { return metadata.signalName.empty() ? std::string_view(_name) : std::string_view(metadata.signalName); }
    [[nodiscard]] std::uint32_t    color() const noexcept { return metadata.color; }
    [[nodiscard]] float            sampleRate() const noexcept { return metadata.sampleRate; }
    [[nodiscard]] float            lineWidth() const noexcept { return metadata.lineWidth; }
    [[nodiscard]] bool             drawEnabled() const noexcept { return metadata.visible; }
    [[nodiscard]] std::string_view signalQuantity() const noexcept { return metadata.signalQuantity; }
    [[nodiscard]] std::string_view signalUnit() const noexcept { return metadata.signalUnit; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept { return metadata.abscissaQuantity; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept { return metadata.abscissaUnit; }

    [[nodiscard]] DataGuard dataGuard() const { return DataGuard(_mutex); }

    /// the streaming samples held
    [[nodiscard]] std::size_t size() const noexcept { return _xValues.size(); }

    [[nodiscard]] SampleWindow samples() const {
        const auto held   = [](const auto& values) -> SampleWindow::Values { return values.get_span(0); };
        const auto errors = _withErrors ? std::visit(held, _yErrors) : SampleWindow::Values{};
        return {.x = _xValues.get_span(0), .y = std::visit(held, _yValues), .yError = errors, .first = _totalSampleCount - std::min(_totalSampleCount, _xValues.size())};
    }

    [[nodiscard]] bool                                hasDataSets() const noexcept { return !_dataSets.empty(); }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const noexcept { return _dataSets; }
    /// beside dataSets(), same order; empty for a sink without uncertainties
    [[nodiscard]] std::span<const DataSetErrors> dataSetErrors() const noexcept { return _dataSetErrors; }
    [[nodiscard]] bool                           hasErrors() const noexcept { return _withErrors; }

    [[nodiscard]] std::size_t datasetRow(const gr::DataSet<float>& dataSet) const noexcept { return metadata.datasetIndex < dataSet.size() ? metadata.datasetIndex : 0UZ; }

    /// the held frame with generation `next`, else the oldest held after it; 0 = the newest.
    /// A DataSet sink's frames are its DataSets, counted from 1 like its tensor frames
    [[nodiscard]] FrameView frame(std::uint64_t next = 0U) const {
        if (_kind == SignalKind::Dataset1D) {
            if (_dataSets.empty()) {
                return {};
            }
            const auto          newest     = static_cast<std::uint64_t>(_totalSampleCount);
            const std::uint64_t oldest     = newest + 1U - _dataSets.size();
            const std::uint64_t generation = next == 0U ? newest : std::clamp(next, oldest, newest);
            return dataSetFrame(*std::next(_dataSets.begin(), static_cast<std::ptrdiff_t>(generation - oldest)), generation);
        }
        return std::visit(
            [this, next]<typename TFrames>(const TFrames& frames) -> FrameView {
                if constexpr (std::is_same_v<TFrames, std::monostate>) {
                    return {};
                } else {
                    if (frames.empty()) {
                        return {};
                    }
                    const std::uint64_t oldest     = _frameGeneration + 1U - frames.size();
                    const std::uint64_t generation = next == 0U ? _frameGeneration : std::clamp(next, oldest, _frameGeneration);
                    const auto&         frame      = *std::next(frames.begin(), static_cast<std::ptrdiff_t>(generation - oldest));
                    if (frame.rank() != 2UZ) {
                        return {};
                    }
                    using V = typename TFrames::value_type::value_type;
                    return {.values = std::span<const V>(frame.data(), frame.size()), .rows = frame.extents()[0], .columns = frame.extents()[1], .generation = generation, .axes = _axes, .columnCoordinates = {}, .dataSet = nullptr};
                }
            },
            _frames);
    }

    [[nodiscard]] FrameView dataSetFrame(const gr::DataSet<float>& dataSet, std::uint64_t generation) const {
        const auto axisOf = [&dataSet](std::size_t d) {
            ImageAxis axis{.origin = 0.0, .step = 1.0, .name = d < dataSet.axis_names.size() ? std::string(dataSet.axis_names[d]) : std::string{}, .unit = d < dataSet.axis_units.size() ? std::string(dataSet.axis_units[d]) : std::string{}};
            if (d < dataSet.axis_values.size() && !dataSet.axis_values[d].empty()) {
                const auto& values = dataSet.axis_values[d];
                axis.origin        = static_cast<double>(values[0]);
                axis.step          = values.size() > 1UZ ? static_cast<double>(values[1] - values[0]) : 1.0;
            }
            return axis;
        };
        const auto coordinates = [&dataSet](std::size_t d) { return d < dataSet.axis_values.size() ? std::span<const float>(dataSet.axis_values[d]) : std::span<const float>{}; };
        if (dataSet.extents.size() == 2UZ) {
            const auto rows    = static_cast<std::size_t>(std::max(dataSet.extents[0], 0));
            const auto columns = static_cast<std::size_t>(std::max(dataSet.extents[1], 0));
            const auto image   = dataSet.size() == 0UZ ? std::span<const float>{} : dataSet.signalValues(datasetRow(dataSet));
            if (image.size() != rows * columns) {
                return {};
            }
            return {.values = image, .rows = rows, .columns = columns, .generation = generation, .axes = {axisOf(0UZ), axisOf(1UZ)}, .columnCoordinates = coordinates(1UZ), .dataSet = &dataSet};
        }
        const std::size_t columns = dataSet.axis_values.empty() ? 0UZ : dataSet.axis_values[0].size();
        if (columns == 0UZ || dataSet.signal_values.size() != dataSet.size() * columns) {
            return {};
        }
        return {.values = std::span<const float>(dataSet.signal_values), .rows = dataSet.size(), .columns = columns, .generation = generation, .axes = {ImageAxis{.origin = 0.0, .step = 1.0, .name = "signal", .unit = {}}, axisOf(0UZ)}, .columnCoordinates = coordinates(0UZ), .dataSet = &dataSet};
    }

    [[nodiscard]] std::span<const DatedMap> events() const noexcept { return _events; }
    [[nodiscard]] std::uint64_t             eventGeneration() const noexcept { return _eventGeneration.load(std::memory_order_acquire); }

    void forEachTag(std::function<void(double timestamp, const gr::property_map& properties)> callback) const {
        for (const DatedMap& tag : _tags) {
            callback(tag.utcSeconds, tag.map);
        }
    }

    [[nodiscard]] double timeLast() const noexcept { return _xValues.empty() ? 0.0 : _xValues.get_span(0).back(); }

    /// since creation, not only those held: samples, DataSets or frames
    [[nodiscard]] std::size_t totalSampleCount() const noexcept { return _totalSampleCount; }
    [[nodiscard]] std::size_t bufferCapacity() const noexcept { return _capacity; }

    /// a named consumer's minimum history, held until `timeout` passes without a new request; takes the guard itself
    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}, CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) {
        std::lock_guard lock(_mutex);
        std::ignore = _capacityRequests.request(source, capacity, now + timeout);
        applyCapacity();
    }

    void expireCapacityRequests(CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) {
        std::lock_guard lock(_mutex);
        std::ignore = _capacityRequests.expire(now);
        applyCapacity();
    }

    /// the configured capacity until a chart asks, then the charts' largest live request (at least the floor);
    /// an event sink keeps at least the configured capacity.
    /// Charts never change the configured capacity.
    [[nodiscard]] std::size_t wantedCapacity() const noexcept {
        if (_capacityRequests.empty()) {
            return _configuredCapacity;
        }
        return _kind == SignalKind::Events ? std::max(_configuredCapacity, _capacityRequests.largest()) : std::max(_capacityRequests.largest(), 1UZ);
    }

    void setConfiguredCapacity(std::size_t capacity) {
        _configuredCapacity = std::max(capacity, 1UZ);
        applyCapacity();
    }

    void applyCapacity() { // a shrinking store drops its oldest items
        const std::size_t capacity = wantedCapacity();
        if (capacity == _capacity) {
            return;
        }
        _capacity             = capacity;
        const auto keepNewest = [capacity](auto& buffer) {
            while (buffer.size() > capacity) {
                buffer.pop_front();
            }
            buffer.resize(capacity);
        };
        switch (_kind) {
        case SignalKind::Streaming:
            keepNewest(_xValues);
            std::visit(keepNewest, _yValues);
            if (_withErrors) {
                std::visit(keepNewest, _yErrors);
            }
            dropTagsBeforeOldestSample();
            break;
        case SignalKind::Dataset1D:
            _dataSets.erase(_dataSets.begin(), _dataSets.end() - static_cast<std::ptrdiff_t>(std::min(_dataSets.size(), capacity)));
            _dataSetErrors.erase(_dataSetErrors.begin(), _dataSetErrors.end() - static_cast<std::ptrdiff_t>(std::min(_dataSetErrors.size(), capacity)));
            break;
        case SignalKind::Events: keepNewestEvents(capacity); break;
        default: break; // tensor frames: max_backlog decides
        }
    }

    template<typename V>
    void pushSample(double utcSeconds, V value) {
        _xValues.push_back(utcSeconds);
        std::visit([value](auto& values) { values.push_back(static_cast<typename std::remove_cvref_t<decltype(values)>::value_type>(value)); }, _yValues);
        ++_totalSampleCount;
    }

    template<typename V>
    void pushSample(double utcSeconds, V value, V standardDeviation) {
        pushSample(utcSeconds, value);
        std::visit([standardDeviation](auto& errors) { errors.push_back(static_cast<typename std::remove_cvref_t<decltype(errors)>::value_type>(standardDeviation)); }, _yErrors);
    }

    /// rebases the provisional history onto the first valid trigger
    void shiftTimes(double seconds) {
        for (std::size_t i = 0UZ, n = _xValues.size(); i < n; ++i) {
            const double utcSeconds = _xValues.front();
            _xValues.pop_front();
            _xValues.push_back(utcSeconds + seconds);
        }
        for (DatedMap& tag : _tags) {
            tag.utcSeconds += seconds;
        }
    }

    gr::property_map& pushTag(double utcSeconds, gr::property_map map) { return _tags.emplace_back(utcSeconds, std::move(map)).map; }

    void dropTagsBeforeOldestSample() {
        if (!_xValues.empty()) {
            const double oldest = _xValues.get_span(0).front();
            std::erase_if(_tags, [oldest](const DatedMap& tag) { return tag.utcSeconds < oldest; });
        }
    }

    /// every event counts in eventGeneration(), held or not
    void pushEvent(double utcSeconds, gr::property_map event) {
        _events.push_back(DatedMap{.utcSeconds = utcSeconds, .map = std::move(event)});
        _eventGeneration.fetch_add(1U, std::memory_order_release);
    }

    void keepNewestEvents(std::size_t count) { _events.erase(_events.begin(), _events.end() - static_cast<std::ptrdiff_t>(std::min(_events.size(), count))); }

    void countSamples(std::size_t count) noexcept { _totalSampleCount += count; } // a tag sink's stream

    /// a full history assigns the DataSet into its oldest entry's buffers: no allocation once the sizes repeat
    void pushDataSet(const gr::DataSet<float>& dataSet) {
        if (_dataSets.size() < _capacity) {
            _dataSets.push_back(dataSet);
        } else {
            std::ranges::rotate(_dataSets, std::next(_dataSets.begin()));
            _dataSets.back() = dataSet;
        }
        ++_totalSampleCount;
    }

    void pushDataSet(gr::DataSet<float>&& dataSet) {
        if (_dataSets.size() >= _capacity) {
            _dataSets.erase(_dataSets.begin());
        }
        _dataSets.push_back(std::move(dataSet));
        ++_totalSampleCount;
    }

    void pushDataSet(gr::DataSet<float>&& dataSet, DataSetErrors&& errors) {
        if (_dataSetErrors.size() >= _capacity) {
            _dataSetErrors.erase(_dataSetErrors.begin());
        }
        _dataSetErrors.push_back(std::move(errors));
        pushDataSet(std::move(dataSet));
    }

    /// reuses the dropped frames' buffers; every frame counts in the generation, kept or not
    template<typename V>
    void pushFrames(const std::ranges::random_access_range auto& frames, std::size_t keep) {
        if (!std::holds_alternative<std::deque<gr::Tensor<V>>>(_frames)) {
            _frames.template emplace<std::deque<gr::Tensor<V>>>();
        }
        auto&             held  = std::get<std::deque<gr::Tensor<V>>>(_frames);
        const std::size_t count = std::ranges::size(frames);
        for (std::size_t i = count - std::min(count, keep); i < count; ++i) {
            gr::Tensor<V> recycled{};
            if (held.size() >= keep) {
                recycled = std::move(held.front());
                held.pop_front();
            }
            const auto& frame = frames[i];
            if (!std::ranges::equal(recycled.extents(), frame.extents())) {
                recycled.resize(frame.extents());
            }
            std::ranges::transform(std::span(frame.data(), frame.size()), recycled.data(), [](auto value) { return static_cast<V>(value); });
            held.push_back(std::move(recycled));
        }
        while (held.size() > keep) {
            held.pop_front();
        }
        _frameGeneration += count;
        _totalSampleCount += count;
    }
};

template<std::ranges::range TSinks>
[[nodiscard]] std::vector<DataGuard> lockAll(const TSinks& sinks) {
    std::vector<const SignalSink*> distinct = sinks | std::views::transform([](const auto& sink) -> const SignalSink* { return std::to_address(sink); }) | std::ranges::to<std::vector>();
    std::ranges::sort(distinct); // each sink's mutex once, in one order
    distinct.erase(std::ranges::unique(distinct).begin(), distinct.end());
    std::vector<DataGuard> guards;
    guards.reserve(distinct.size());
    for (const SignalSink* sink : distinct) {
        guards.push_back(sink->dataGuard());
    }
    return guards;
}

} // namespace gr::od

#endif // OPENDIGITIZER_SIGNALSINK_HPP
