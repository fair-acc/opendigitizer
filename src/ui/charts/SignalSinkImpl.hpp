#ifndef OPENDIGITIZER_UI_CHARTS_SIGNALSINKIMPL_HPP
#define OPENDIGITIZER_UI_CHARTS_SIGNALSINKIMPL_HPP

#include "SignalSink.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <ranges>

#include <gnuradio-4.0/HistoryBuffer.hpp>

namespace opendigitizer::ui::charts {

/**
 * Standalone streaming signal sink for testing and headless usage.
 */
template<typename T = float>
class StreamingSignalSink : public SignalSinkBase {
public:
    explicit StreamingSignalSink(std::string uniqueName, std::size_t initialCapacity = 2048) : _uniqueName(std::move(uniqueName)), _xValues(initialCapacity), _yValues(initialCapacity) {}

    ~StreamingSignalSink() override = default;

    // Identity
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view signalName() const noexcept override { return _signalName.empty() ? _uniqueName : _signalName; }

    void setSignalName(std::string_view name) { _signalName = std::string(name); }

    // Minimal metadata
    [[nodiscard]] std::uint32_t color() const noexcept override { return _color; }
    [[nodiscard]] float         sampleRate() const noexcept override { return _sampleRate; }

    void setColor(std::uint32_t c) noexcept { _color = c; }
    void setSampleRate(float sr) noexcept { _sampleRate = sr; }

    // Indexed data access
    [[nodiscard]] std::size_t size() const noexcept override { return _xValues.size(); }

    [[nodiscard]] double xAt(std::size_t i) const override { return _xValues.get_span(0)[i]; }

    [[nodiscard]] float yAt(std::size_t i) const override { return static_cast<float>(_yValues.get_span(0)[i]); }

    // PlotData accessor
    [[nodiscard]] PlotData plotData() const override {
        static auto getter = +[](int idx, void* userData) -> PlotPoint {
            auto* self  = static_cast<const StreamingSignalSink*>(userData);
            auto  xSpan = self->_xValues.get_span(0);
            auto  ySpan = self->_yValues.get_span(0);
            return {xSpan[static_cast<std::size_t>(idx)], static_cast<double>(ySpan[static_cast<std::size_t>(idx)])};
        };
        return {getter, const_cast<StreamingSignalSink*>(this), static_cast<int>(_xValues.size())};
    }

    // DataSet support (not available for streaming)
    [[nodiscard]] bool                                hasDataSets() const noexcept override { return false; }
    [[nodiscard]] std::size_t                         dataSetCount() const noexcept override { return 0; }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }

    // Time range
    [[nodiscard]] double timeFirst() const noexcept override { return _xValues.empty() ? 0.0 : _xValues.front(); }
    [[nodiscard]] double timeLast() const noexcept override { return _xValues.empty() ? 0.0 : _xValues.back(); }

    // Buffer control
    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _xValues.capacity(); }

    void requestCapacity(void* owner, std::size_t minSamples) override {
        std::lock_guard lock(_historyMutex);
        _historyRequirements[owner] = HistoryRequirement{minSamples, std::chrono::steady_clock::now(), std::chrono::milliseconds{30000}};
        updateBufferCapacity();
    }

    void releaseCapacity(void* owner) override {
        std::lock_guard lock(_historyMutex);
        _historyRequirements.erase(owner);
        updateBufferCapacity();
    }

    // Push operations (for feeding data)
    void pushSample(double xValue, const T& yValue) {
        _xValues.push_back(xValue);
        _yValues.push_back(yValue);
    }

    void pushSamples(std::span<const double> xValues, std::span<const T> yValues) {
        if (xValues.size() != yValues.size()) {
            return;
        }
        _xValues.push_back(xValues.begin(), xValues.end());
        _yValues.push_back(yValues.begin(), yValues.end());
    }

    // Direct buffer access for tests
    [[nodiscard]] const gr::HistoryBuffer<double>& xBuffer() const noexcept { return _xValues; }
    [[nodiscard]] const gr::HistoryBuffer<T>&      yBuffer() const noexcept { return _yValues; }

private:
    void updateBufferCapacity() {
        auto now = std::chrono::steady_clock::now();
        std::erase_if(_historyRequirements, [now](const auto& kvp) { return (now - kvp.second.lastUpdate) > kvp.second.timeout; });

        std::size_t maxRequired = 2048;
        for (const auto& [_, req] : _historyRequirements) {
            maxRequired = std::max(maxRequired, req.requiredSize);
        }

        if (_xValues.capacity() != maxRequired) {
            _xValues.resize(maxRequired);
            _yValues.resize(maxRequired);
        }
    }

    std::string   _uniqueName;
    std::string   _signalName;
    std::uint32_t _color      = 0xFFFFFFFF;
    float         _sampleRate = 1000.0f;

    gr::HistoryBuffer<double> _xValues;
    gr::HistoryBuffer<T>      _yValues;

    std::mutex                                    _historyMutex;
    std::unordered_map<void*, HistoryRequirement> _historyRequirements;
};

/**
 * Standalone DataSet signal sink for testing and headless usage.
 */
template<typename T = float>
class DataSetSignalSink : public SignalSinkBase {
public:
    explicit DataSetSignalSink(std::string uniqueName, std::size_t maxDataSets = 10) : _uniqueName(std::move(uniqueName)), _maxDataSets(maxDataSets) {}

    ~DataSetSignalSink() override = default;

    // Identity
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view signalName() const noexcept override { return _signalName.empty() ? _uniqueName : _signalName; }

    void setSignalName(std::string_view name) { _signalName = std::string(name); }

    // Minimal metadata
    [[nodiscard]] std::uint32_t color() const noexcept override { return _color; }
    [[nodiscard]] float         sampleRate() const noexcept override { return _sampleRate; }

    void setColor(std::uint32_t c) noexcept { _color = c; }
    void setSampleRate(float sr) noexcept { _sampleRate = sr; }

    // Indexed data access (returns first DataSet's values)
    [[nodiscard]] std::size_t size() const noexcept override {
        if (_dataSets.empty()) {
            return 0;
        }
        return _dataSets.front().signal_values.size();
    }

    [[nodiscard]] double xAt(std::size_t i) const override {
        if (_dataSets.empty() || _dataSets.front().axis_values.empty()) {
            return 0.0;
        }
        return static_cast<double>(_dataSets.front().axis_values[0][i]);
    }

    [[nodiscard]] float yAt(std::size_t i) const override {
        if (_dataSets.empty()) {
            return 0.0f;
        }
        return static_cast<float>(_dataSets.front().signal_values[i]);
    }

    // PlotData accessor (for first DataSet)
    [[nodiscard]] PlotData plotData() const override {
        if (_dataSets.empty()) {
            return {nullptr, nullptr, 0};
        }
        static auto getter = +[](int idx, void* userData) -> PlotPoint {
            auto*       self = static_cast<const DataSetSignalSink*>(userData);
            const auto& ds   = self->_dataSets.front();
            double      x    = ds.axis_values.empty() ? static_cast<double>(idx) : static_cast<double>(ds.axis_values[0][static_cast<std::size_t>(idx)]);
            double      y    = static_cast<double>(ds.signal_values[static_cast<std::size_t>(idx)]);
            return {x, y};
        };
        return {getter, const_cast<DataSetSignalSink*>(this), static_cast<int>(_dataSets.front().signal_values.size())};
    }

    // DataSet support
    [[nodiscard]] bool        hasDataSets() const noexcept override { return !_dataSets.empty(); }
    [[nodiscard]] std::size_t dataSetCount() const noexcept override { return _dataSets.size(); }

    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override {
        if constexpr (std::is_same_v<T, float>) {
            _floatDataSets.assign(_dataSets.begin(), _dataSets.end());
        } else {
            _floatDataSets.clear();
            for (const auto& ds : _dataSets) {
                _floatDataSets.push_back(convertToFloat(ds));
            }
        }
        return _floatDataSets;
    }

    // Time range
    [[nodiscard]] double timeFirst() const noexcept override {
        if (_dataSets.empty()) {
            return 0.0;
        }
        return static_cast<double>(_dataSets.front().timestamp) * 1e-9;
    }

    [[nodiscard]] double timeLast() const noexcept override {
        if (_dataSets.empty()) {
            return 0.0;
        }
        return static_cast<double>(_dataSets.back().timestamp) * 1e-9;
    }

    // Buffer control
    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _maxDataSets; }

    void requestCapacity(void* owner, std::size_t minSamples) override {
        std::lock_guard lock(_historyMutex);
        _historyRequirements[owner] = HistoryRequirement{minSamples, std::chrono::steady_clock::now(), std::chrono::milliseconds{30000}};
        updateMaxDataSets();
    }

    void releaseCapacity(void* owner) override {
        std::lock_guard lock(_historyMutex);
        _historyRequirements.erase(owner);
        updateMaxDataSets();
    }

    // Push operations (for feeding data)
    void pushDataSet(const gr::DataSet<T>& dataSet) {
        _dataSets.push_back(dataSet);
        while (_dataSets.size() > _maxDataSets) {
            _dataSets.pop_front();
        }
    }

    // Direct access for tests
    [[nodiscard]] const std::deque<gr::DataSet<T>>& rawDataSets() const noexcept { return _dataSets; }

private:
    void updateMaxDataSets() {
        auto now = std::chrono::steady_clock::now();
        std::erase_if(_historyRequirements, [now](const auto& kvp) { return (now - kvp.second.lastUpdate) > kvp.second.timeout; });

        std::size_t maxRequired = 10;
        for (const auto& [_, req] : _historyRequirements) {
            maxRequired = std::max(maxRequired, req.requiredSize);
        }
        _maxDataSets = maxRequired;

        while (_dataSets.size() > _maxDataSets) {
            _dataSets.pop_front();
        }
    }

    static gr::DataSet<float> convertToFloat(const gr::DataSet<T>& src) {
        gr::DataSet<float> dst;
        dst.timestamp         = src.timestamp;
        dst.axis_names        = src.axis_names;
        dst.axis_units        = src.axis_units;
        dst.extents           = src.extents;
        dst.layout            = src.layout;
        dst.signal_names      = src.signal_names;
        dst.signal_quantities = src.signal_quantities;
        dst.signal_units      = src.signal_units;
        dst.meta_information  = src.meta_information;
        dst.timing_events     = src.timing_events;

        dst.axis_values.resize(src.axis_values.size());
        for (std::size_t i = 0; i < src.axis_values.size(); ++i) {
            dst.axis_values[i].resize(src.axis_values[i].size());
            std::transform(src.axis_values[i].begin(), src.axis_values[i].end(), dst.axis_values[i].begin(), [](const T& v) { return static_cast<float>(v); });
        }

        dst.signal_values.resize(src.signal_values.size());
        std::transform(src.signal_values.begin(), src.signal_values.end(), dst.signal_values.begin(), [](const T& v) { return static_cast<float>(v); });

        dst.signal_ranges.resize(src.signal_ranges.size());
        std::transform(src.signal_ranges.begin(), src.signal_ranges.end(), dst.signal_ranges.begin(), [](const gr::Range<T>& r) { return gr::Range<float>{static_cast<float>(r.min), static_cast<float>(r.max)}; });

        return dst;
    }

    std::string   _uniqueName;
    std::string   _signalName;
    std::uint32_t _color      = 0xFFFFFFFF;
    float         _sampleRate = 1000.0f;

    std::deque<gr::DataSet<T>>              _dataSets;
    std::size_t                             _maxDataSets;
    mutable std::vector<gr::DataSet<float>> _floatDataSets;

    std::mutex                                    _historyMutex;
    std::unordered_map<void*, HistoryRequirement> _historyRequirements;
};

} // namespace opendigitizer::ui::charts

#endif // OPENDIGITIZER_UI_CHARTS_SIGNALSINKIMPL_HPP
