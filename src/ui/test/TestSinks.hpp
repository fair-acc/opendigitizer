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

namespace opendigitizer::test {

class TestStreamingSink : public opendigitizer::SignalSink {
    std::string         _uniqueName;
    std::string         _signalName;
    std::uint32_t       _color      = 0xFFFFFF;
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

    struct CapacityRequest {
        std::size_t                                        capacity;
        std::chrono::time_point<std::chrono::steady_clock> expiry_time;
    };
    std::unordered_map<std::string, CapacityRequest> _capacityRequests;

public:
    explicit TestStreamingSink(std::string name, std::size_t capacity = 2048) : _uniqueName(std::move(name)), _signalName(_uniqueName), _capacity(capacity) {
        _xValues.reserve(capacity);
        _yValues.reserve(capacity);
    }

    [[nodiscard]] std::string_view         name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view         uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view         signalName() const noexcept override { return _signalName; }
    [[nodiscard]] std::uint32_t            color() const noexcept override { return _color; }
    [[nodiscard]] float                    sampleRate() const noexcept override { return _sampleRate; }
    [[nodiscard]] opendigitizer::LineStyle lineStyle() const noexcept override { return opendigitizer::LineStyle::Solid; }
    [[nodiscard]] float                    lineWidth() const noexcept override { return 1.0f; }

    [[nodiscard]] std::size_t size() const noexcept override { return _xValues.size(); }
    [[nodiscard]] double      xAt(std::size_t i) const override { return _xValues[i]; }
    [[nodiscard]] float       yAt(std::size_t i) const override { return _yValues[i]; }

    [[nodiscard]] opendigitizer::PlotData plotData() const override {
        static auto getter = +[](int idx, void* userData) -> opendigitizer::PlotPoint {
            auto* self = static_cast<const TestStreamingSink*>(userData);
            return {self->_xValues[static_cast<std::size_t>(idx)], static_cast<double>(self->_yValues[static_cast<std::size_t>(idx)])};
        };
        return {getter, const_cast<TestStreamingSink*>(this), static_cast<int>(_xValues.size())};
    }

    [[nodiscard]] bool                                hasDataSets() const noexcept override { return false; }
    [[nodiscard]] std::size_t                         dataSetCount() const noexcept override { return 0; }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }

    [[nodiscard]] bool                      hasStreamingTags() const noexcept override { return false; }
    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept override { return {0.0, 0.0}; }
    void                                    forEachTag(std::function<void(double, const gr::property_map&)> /*callback*/) const override {}

    [[nodiscard]] double timeFirst() const noexcept override { return _xValues.empty() ? 0.0 : _xValues.front(); }
    [[nodiscard]] double timeLast() const noexcept override { return _xValues.empty() ? 0.0 : _xValues.back(); }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept override { return _totalSampleCount; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _capacity; }

    void requestCapacity(std::string_view source, std::size_t capacity, std::chrono::seconds timeout = std::chrono::seconds{60}) override {
        auto expiry_time                       = std::chrono::steady_clock::now() + timeout;
        _capacityRequests[std::string(source)] = CapacityRequest{capacity, expiry_time};
        for (const auto& [_, request] : _capacityRequests) {
            _capacity = std::max(_capacity, request.capacity);
        }
    }

    void expireCapacityRequests() override {
        auto now = std::chrono::steady_clock::now();
        std::erase_if(_capacityRequests, [now](const auto& pair) { return pair.second.expiry_time < now; });
        std::size_t maxCap = 2048;
        for (const auto& [_, request] : _capacityRequests) {
            maxCap = std::max(maxCap, request.capacity);
        }
        _capacity = maxCap;
    }

    [[nodiscard]] DataRange getXRange(double tMin, double tMax) const override {
        if (_xValues.empty()) {
            return {0, 0};
        }
        auto itBegin = std::lower_bound(_xValues.begin(), _xValues.end(), tMin);
        auto itEnd   = std::upper_bound(_xValues.begin(), _xValues.end(), tMax);
        if (itBegin >= itEnd) {
            return {0, 0};
        }
        std::size_t startIdx = static_cast<std::size_t>(std::distance(_xValues.begin(), itBegin));
        std::size_t count    = static_cast<std::size_t>(std::distance(itBegin, itEnd));
        return {startIdx, count};
    }

    [[nodiscard]] DataRange getTagRange(double /*tMin*/, double /*tMax*/) const override { return {0, 0}; }

    [[nodiscard]] XRangeResult getX(double tMin, double tMax) const override {
        auto [startIdx, count] = getXRange(tMin, tMax);
        if (count == 0) {
            return {{}, 0.0, 0.0};
        }
        return {std::span<const double>(_xValues.data() + startIdx, count), _xValues[startIdx], _xValues[startIdx + count - 1]};
    }
    [[nodiscard]] YRangeResult getY(double tMin, double tMax) const override {
        auto [startIdx, count] = getXRange(tMin, tMax);
        if (count == 0) {
            return {{}, 0.0, 0.0};
        }
        return {std::span<const float>(_yValues.data() + startIdx, count), _xValues[startIdx], _xValues[startIdx + count - 1]};
    }
    [[nodiscard]] TagRangeResult getTags(double /*tMin*/, double /*tMax*/) const override { return {{}, 0.0, 0.0}; }
    [[nodiscard]] XYTagRange     xyTagRange(double tMin, double tMax) const override {
        auto range = getXRange(tMin, tMax);
        if (range.empty()) {
            return XYTagRange{};
        }
        return XYTagRange{XYTagIterator{this, range.start_index, range.start_index + range.count}, XYTagIterator{this, range.start_index + range.count, range.start_index + range.count}};
    }
    void pruneTags(double /*minX*/) override {}

    [[nodiscard]] opendigitizer::DataGuard dataGuard() const override { return opendigitizer::DataGuard(_mutex); }

    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }
    void               setDrawEnabled(bool enabled) override { _drawEnabled = enabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return _signalQuantity; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return _signalUnit; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return _abscissaQuantity; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return _abscissaUnit; }
    [[nodiscard]] float            signalMin() const noexcept override { return std::numeric_limits<float>::lowest(); }
    [[nodiscard]] float            signalMax() const noexcept override { return std::numeric_limits<float>::max(); }

    void setSignalName(std::string name) { _signalName = std::move(name); }
    void setSampleRate(float rate) { _sampleRate = rate; }
    void setColor(std::uint32_t c) { _color = c; }
    void setSignalQuantity(std::string q) { _signalQuantity = std::move(q); }
    void setSignalUnit(std::string u) { _signalUnit = std::move(u); }
    void setAbscissaQuantity(std::string q) { _abscissaQuantity = std::move(q); }
    void setAbscissaUnit(std::string u) { _abscissaUnit = std::move(u); }

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

class TestDataSetSink : public opendigitizer::SignalSink {
    std::string                    _uniqueName;
    std::deque<gr::DataSet<float>> _dataSets;
    std::size_t                    _maxDataSets;
    std::size_t                    _totalDataSetCount = 0;
    mutable std::mutex             _mutex;
    bool                           _drawEnabled = true;

public:
    explicit TestDataSetSink(std::string name, std::size_t maxDataSets = 10) : _uniqueName(std::move(name)), _maxDataSets(maxDataSets) {}

    [[nodiscard]] std::string_view         name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view         uniqueName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view         signalName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::uint32_t            color() const noexcept override { return 0xFFFFFF; }
    [[nodiscard]] float                    sampleRate() const noexcept override { return 1.0f; }
    [[nodiscard]] opendigitizer::LineStyle lineStyle() const noexcept override { return opendigitizer::LineStyle::Solid; }
    [[nodiscard]] float                    lineWidth() const noexcept override { return 1.0f; }

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

    [[nodiscard]] opendigitizer::PlotData plotData() const override {
        static auto getter = +[](int idx, void* userData) -> opendigitizer::PlotPoint {
            auto* self = static_cast<const TestDataSetSink*>(userData);
            return {self->xAt(static_cast<std::size_t>(idx)), static_cast<double>(self->yAt(static_cast<std::size_t>(idx)))};
        };
        return {getter, const_cast<TestDataSetSink*>(this), static_cast<int>(size())};
    }

    [[nodiscard]] bool                                hasDataSets() const noexcept override { return !_dataSets.empty(); }
    [[nodiscard]] std::size_t                         dataSetCount() const noexcept override { return _dataSets.size(); }
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }

    [[nodiscard]] bool                      hasStreamingTags() const noexcept override { return false; }
    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept override { return {0.0, 0.0}; }
    void                                    forEachTag(std::function<void(double, const gr::property_map&)> /*callback*/) const override {}

    [[nodiscard]] double timeFirst() const noexcept override { return 0.0; }
    [[nodiscard]] double timeLast() const noexcept override { return 0.0; }

    [[nodiscard]] std::size_t totalSampleCount() const noexcept override { return _totalDataSetCount; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _maxDataSets; }

    void requestCapacity(std::string_view /*source*/, std::size_t /*capacity*/, std::chrono::seconds /*timeout*/ = std::chrono::seconds{60}) override {}

    void expireCapacityRequests() override {}

    [[nodiscard]] DataRange getXRange(double /*tMin*/, double /*tMax*/) const override { return {0, 0}; }
    [[nodiscard]] DataRange getTagRange(double /*tMin*/, double /*tMax*/) const override { return {0, 0}; }

    [[nodiscard]] XRangeResult   getX(double /*tMin*/, double /*tMax*/) const override { return {{}, 0.0, 0.0}; }
    [[nodiscard]] YRangeResult   getY(double /*tMin*/, double /*tMax*/) const override { return {{}, 0.0, 0.0}; }
    [[nodiscard]] TagRangeResult getTags(double /*tMin*/, double /*tMax*/) const override { return {{}, 0.0, 0.0}; }
    [[nodiscard]] XYTagRange     xyTagRange(double /*tMin*/, double /*tMax*/) const override { return XYTagRange{}; }
    void                         pruneTags(double /*minX*/) override {}

    [[nodiscard]] opendigitizer::DataGuard dataGuard() const override { return opendigitizer::DataGuard(_mutex); }

    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }
    void               setDrawEnabled(bool enabled) override { _drawEnabled = enabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return ""; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return ""; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return "time"; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return "s"; }
    [[nodiscard]] float            signalMin() const noexcept override { return std::numeric_limits<float>::lowest(); }
    [[nodiscard]] float            signalMax() const noexcept override { return std::numeric_limits<float>::max(); }

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

inline std::shared_ptr<opendigitizer::charts::XYChart> makeXYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<opendigitizer::charts::XYChart>(std::move(initParams));
}

inline std::shared_ptr<opendigitizer::charts::YYChart> makeYYChart(const std::string& name = "") {
    gr::property_map initParams;
    if (!name.empty()) {
        initParams["chart_name"] = name;
    }
    return std::make_shared<opendigitizer::charts::YYChart>(std::move(initParams));
}

inline std::variant<std::shared_ptr<opendigitizer::charts::XYChart>, std::shared_ptr<opendigitizer::charts::YYChart>, std::monostate> makeChartByType(std::string_view typeName, const std::string& name = "") {
    using namespace opendigitizer::charts;
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

} // namespace opendigitizer::test

#endif // OPENDIGITIZER_TEST_TESTSINKS_HPP
