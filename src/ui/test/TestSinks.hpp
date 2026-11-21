#ifndef OPENDIGITIZER_TEST_TESTSINKS_HPP
#define OPENDIGITIZER_TEST_TESTSINKS_HPP

#include "../charts/Charts.hpp"

#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gr::od::test {

struct TestStreamingSink : SignalSink {
    std::string         _uniqueName;
    std::string         _signalName;
    std::uint32_t       _color      = 0xFFFFFF;
    float               _lineWidth  = 1.0f;
    float               _sampleRate = 1000.0f;
    std::vector<double> _xValues;
    std::vector<float>  _yValues;
    std::size_t         _capacity;
    std::size_t         _totalSampleCount = 0;
    mutable std::mutex  _mutex;
    bool                _drawEnabled      = true;
    std::string         _signalQuantity   = "voltage";
    std::string         _signalUnit       = "V";
    std::string         _abscissaQuantity = "time";
    std::string         _abscissaUnit     = "s";

    CapacityRequests _capacityRequests;

    explicit TestStreamingSink(std::string name, std::size_t capacity = 2048) : _uniqueName(std::move(name)), _signalName(_uniqueName), _capacity(capacity), _capacityRequests{.minimum = capacity} {
        _xValues.reserve(capacity);
        _yValues.reserve(capacity);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view signalName() const noexcept override { return _signalName; }
    [[nodiscard]] std::uint32_t    color() const noexcept override { return _color; }
    [[nodiscard]] float            sampleRate() const noexcept override { return _sampleRate; }
    [[nodiscard]] float            lineWidth() const noexcept override { return _lineWidth; }

    [[nodiscard]] std::size_t size() const noexcept override { return _xValues.size(); }
    [[nodiscard]] double      xAt(std::size_t i) const override { return _xValues[i]; }
    [[nodiscard]] float       yAt(std::size_t i) const override { return _yValues[i]; }

    [[nodiscard]] bool                                hasDataSets() const noexcept override { return false; }
    [[nodiscard]] std::size_t                         dataSetCount() const noexcept override { return 0; }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }
    [[nodiscard]] SignalKind                          signalKind() const noexcept override { return SignalKind::Streaming; }

    void forEachTag(std::function<void(double, const gr::property_map&)> /*callback*/) const override {}

    [[nodiscard]] double timeLast() const noexcept override { return _xValues.empty() ? 0.0 : _xValues.back(); }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept override { return _totalSampleCount; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _capacity; }

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}, CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) override { _capacity = std::max(_capacity, _capacityRequests.request(source, capacity, now + timeout)); }

    void expireCapacityRequests(CapacityRequests::clock::time_point now = CapacityRequests::clock::now()) override { _capacity = _capacityRequests.expire(now); }

    [[nodiscard]] DataGuard dataGuard() const override { return DataGuard(_mutex); }

    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return _signalQuantity; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return _signalUnit; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return _abscissaQuantity; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return _abscissaUnit; }

    void setColor(std::uint32_t c) { _color = c; }
    void setLineWidth(float width) { _lineWidth = width; }
    void setSignalName(std::string_view nm) { _signalName = std::string(nm); }

    void setSampleRate(float rate) { _sampleRate = rate; }
    void setSignalQuantity(std::string q) { _signalQuantity = std::move(q); }
    void setSignalUnit(std::string u) { _signalUnit = std::move(u); }

    void pushSample(double x, float y) {
        if (_xValues.size() >= _capacity) {
            _xValues.erase(_xValues.begin());
            _yValues.erase(_yValues.begin());
        }
        _xValues.push_back(x);
        _yValues.push_back(y);
        ++_totalSampleCount;
    }
};

struct TestDataSetSink : SignalSink {
    std::string                    _uniqueName;
    std::deque<gr::DataSet<float>> _dataSets;
    std::size_t                    _maxDataSets;
    std::size_t                    _totalDataSetCount = 0;
    mutable std::mutex             _mutex;
    bool                           _drawEnabled = true;

    explicit TestDataSetSink(std::string name, std::size_t maxDataSets = 10) : _uniqueName(std::move(name)), _maxDataSets(maxDataSets) {}

    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view signalName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::uint32_t    color() const noexcept override { return 0xFFFFFF; }
    [[nodiscard]] float            sampleRate() const noexcept override { return 1.0f; }
    [[nodiscard]] float            lineWidth() const noexcept override { return 1.0f; }

    [[nodiscard]] std::size_t size() const noexcept override {
        if (_dataSets.empty()) {
            return 0;
        }
        const auto& ds = _dataSets.front();
        return ds.axis_values.empty() ? 0 : ds.axis_values[0].size();
    }

    [[nodiscard]] double xAt(std::size_t i) const override {
        if (_dataSets.empty()) {
            return 0.0;
        }
        const auto& ds = _dataSets.front();
        return ds.axis_values.empty() ? 0.0 : static_cast<double>(ds.axis_values[0][i]);
    }

    [[nodiscard]] float yAt(std::size_t i) const override {
        if (_dataSets.empty()) {
            return 0.0f;
        }
        const auto& ds = _dataSets.front();
        return i < ds.signal_values.size() ? ds.signal_values[i] : 0.0f;
    }

    [[nodiscard]] bool                                hasDataSets() const noexcept override { return !_dataSets.empty(); }
    [[nodiscard]] std::size_t                         dataSetCount() const noexcept override { return _dataSets.size(); }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }
    [[nodiscard]] SignalKind                          signalKind() const noexcept override { return SignalKind::Dataset1D; }

    void forEachTag(std::function<void(double, const gr::property_map&)> /*callback*/) const override {}

    [[nodiscard]] double timeLast() const noexcept override { return 0.0; }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept override { return _totalDataSetCount; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _maxDataSets; }

    void requestCapacity(std::string_view /*source*/, std::size_t /*capacity*/, std::chrono::seconds /*timeout*/ = std::chrono::seconds{60}, CapacityRequests::clock::time_point /*now*/ = CapacityRequests::clock::now()) override {}

    void expireCapacityRequests(CapacityRequests::clock::time_point /*now*/ = CapacityRequests::clock::now()) override {}

    [[nodiscard]] DataGuard dataGuard() const override { return DataGuard(_mutex); }

    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return ""; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return ""; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return "time"; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return "s"; }

    void pushDataSet(gr::DataSet<float> ds) {
        if (_dataSets.size() >= _maxDataSets) {
            _dataSets.pop_front();
        }
        _dataSets.push_back(std::move(ds));
        ++_totalDataSetCount;
    }

    const std::deque<gr::DataSet<float>>& rawDataSets() const { return _dataSets; }
};

template<typename T = float>
inline std::shared_ptr<TestStreamingSink> makeTestStreamingSink(std::string name, std::size_t capacity = 2048) {
    return std::make_shared<TestStreamingSink>(std::move(name), capacity);
}

template<typename T = float>
inline std::shared_ptr<TestDataSetSink> makeTestDataSetSink(std::string name, std::size_t maxDataSets = 10) {
    return std::make_shared<TestDataSetSink>(std::move(name), maxDataSets);
}

inline std::shared_ptr<charts::XYChart> makeXYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<charts::XYChart>(std::move(initParams));
}

inline std::shared_ptr<charts::YYChart> makeYYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<charts::YYChart>(std::move(initParams));
}

inline std::variant<std::shared_ptr<charts::XYChart>, std::shared_ptr<charts::YYChart>, std::monostate> makeChartByType(std::string_view typeName, const std::string& name = "") {
    using namespace charts;
    using Storage = std::variant<std::shared_ptr<XYChart>, std::shared_ptr<YYChart>, std::monostate>;

    std::string lowerTypeName(typeName);
    std::transform(lowerTypeName.begin(), lowerTypeName.end(), lowerTypeName.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lowerTypeName.find("xychart") != std::string::npos || typeName == "XYChart") {
        return Storage{makeXYChart(name)};
    }
    if (lowerTypeName.find("yychart") != std::string::npos || typeName == "YYChart") {
        return Storage{makeYYChart(name)};
    }
    return Storage{std::monostate{}};
}

} // namespace gr::od::test

#endif // OPENDIGITIZER_TEST_TESTSINKS_HPP
