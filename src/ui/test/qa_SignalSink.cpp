#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"

#include <boost/ut.hpp>

#include <chrono>
#include <cmath>
#include <deque>
#include <numbers>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

/**
 * @brief Minimal test implementation of SignalSink for unit testing.
 *
 * This is NOT a GR4 block - it's a simple class for testing the SignalSink interface.
 */
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
    bool                _drawEnabled = true;

    struct CapacityRequest {
        std::size_t                                        capacity;
        std::chrono::time_point<std::chrono::steady_clock> expiryTime;
    };
    std::unordered_map<std::string, CapacityRequest> _capacityRequests;

public:
    explicit TestStreamingSink(std::string name, std::size_t capacity = 2048) : _uniqueName(std::move(name)), _signalName(_uniqueName), _capacity(capacity) {
        _xValues.reserve(capacity);
        _yValues.reserve(capacity);
    }

    // --- Block identity ---
    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }

    // --- SignalSink interface ---
    [[nodiscard]] std::string_view signalName() const noexcept override { return _signalName; }
    [[nodiscard]] std::uint32_t    color() const noexcept override { return _color; }
    [[nodiscard]] float            sampleRate() const noexcept override { return _sampleRate; }

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
        auto expiryTime                        = std::chrono::steady_clock::now() + timeout;
        _capacityRequests[std::string(source)] = CapacityRequest{capacity, expiryTime};
        // Update _capacity to maximum of all requests
        for (const auto& [_, request] : _capacityRequests) {
            _capacity = std::max(_capacity, request.capacity);
        }
    }

    void expireCapacityRequests() override {
        auto now = std::chrono::steady_clock::now();
        std::erase_if(_capacityRequests, [now](const auto& pair) { return pair.second.expiryTime < now; });
        // Recalculate capacity from remaining requests
        std::size_t maxCap = 2048; // default minimum
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

    [[nodiscard]] std::unique_lock<std::mutex> acquireDataLock() const override { return std::unique_lock<std::mutex>(_mutex); }

    gr::work::Status invokeWork() override { return gr::work::Status::OK; }
    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }
    void               setDrawEnabled(bool enabled) override { _drawEnabled = enabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return "voltage"; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return "V"; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return "time"; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return "s"; }
    [[nodiscard]] float            signalMin() const noexcept override { return std::numeric_limits<float>::lowest(); }
    [[nodiscard]] float            signalMax() const noexcept override { return std::numeric_limits<float>::max(); }

    // --- Test helpers ---
    void setSignalName(std::string name) { _signalName = std::move(name); }
    void setSampleRate(float rate) { _sampleRate = rate; }
    void setColor(std::uint32_t c) { _color = c; }

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

/**
 * @brief Minimal test implementation for DataSet-based sinks.
 */
class TestDataSetSink : public opendigitizer::SignalSink {
    std::string                    _uniqueName;
    std::deque<gr::DataSet<float>> _dataSets;
    std::size_t                    _maxDataSets;
    std::size_t                    _totalDataSetCount = 0;
    mutable std::mutex             _mutex;
    bool                           _drawEnabled = true;

public:
    explicit TestDataSetSink(std::string name, std::size_t maxDataSets = 10) : _uniqueName(std::move(name)), _maxDataSets(maxDataSets) {}

    // --- Block identity ---
    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }

    // --- SignalSink interface ---
    [[nodiscard]] std::string_view signalName() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::uint32_t    color() const noexcept override { return 0xFFFFFF; }
    [[nodiscard]] float            sampleRate() const noexcept override { return 1.0f; }

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
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override {
        // Note: std::deque is not contiguous, so we can't return a span directly
        // For testing purposes, this returns empty - real implementation would need different storage
        return {};
    }

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

    [[nodiscard]] std::unique_lock<std::mutex> acquireDataLock() const override { return std::unique_lock<std::mutex>(_mutex); }

    gr::work::Status invokeWork() override { return gr::work::Status::OK; }
    gr::work::Status draw(const gr::property_map& /*config*/) override { return gr::work::Status::OK; }

    [[nodiscard]] bool drawEnabled() const noexcept override { return _drawEnabled; }
    void               setDrawEnabled(bool enabled) override { _drawEnabled = enabled; }

    [[nodiscard]] std::string_view signalQuantity() const noexcept override { return ""; }
    [[nodiscard]] std::string_view signalUnit() const noexcept override { return ""; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept override { return "time"; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept override { return "s"; }
    [[nodiscard]] float            signalMin() const noexcept override { return std::numeric_limits<float>::lowest(); }
    [[nodiscard]] float            signalMax() const noexcept override { return std::numeric_limits<float>::max(); }

    // --- Test helpers ---
    void pushDataSet(gr::DataSet<float> ds) {
        if (_dataSets.size() >= _maxDataSets) {
            _dataSets.pop_front();
        }
        _dataSets.push_back(std::move(ds));
        ++_totalDataSetCount;
    }

    const std::deque<gr::DataSet<float>>& rawDataSets() const { return _dataSets; }
};

} // anonymous namespace

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::charts;

    "TestStreamingSink basic usage"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 100);

        expect(eq(sink->uniqueName(), std::string_view("test_sink")));
        expect(eq(sink->signalName(), std::string_view("test_sink")));
        expect(eq(sink->bufferCapacity(), 100UZ));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "TestStreamingSink push and retrieve data"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 100);

        // Push some data
        for (std::size_t i = 0; i < 50; ++i) {
            double x = static_cast<double>(i) * 0.01; // 0.0, 0.01, 0.02, ...
            float  y = std::sin(static_cast<float>(i) * 0.1f);
            sink->pushSample(x, y);
        }

        expect(eq(sink->size(), 50UZ));

        // Check first and last values via indexed access
        expect(std::abs(sink->xAt(0) - 0.0) < 1e-9);
        expect(std::abs(sink->xAt(49) - 0.49) < 1e-9);

        // Check PlotData accessor
        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 50));
        expect(pd.getter != nullptr);

        // Use the getter
        auto point = pd.getter(0, pd.userData);
        expect(std::abs(point.x - 0.0) < 1e-9);
    };

    "TestStreamingSink circular buffer behavior"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 50);

        // Push more data than capacity
        for (std::size_t i = 0; i < 100; ++i) {
            double x = static_cast<double>(i);
            float  y = static_cast<float>(i);
            sink->pushSample(x, y);
        }

        // Should only have last 50 samples
        expect(eq(sink->size(), 50UZ));

        expect(std::abs(sink->xAt(0) - 50.0) < 1e-9);
        expect(std::abs(sink->xAt(49) - 99.0) < 1e-9);
    };

    "TestStreamingSink metadata"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 100);

        sink->setSignalName("Voltage");
        sink->setSampleRate(1000.0f);
        sink->setColor(0xFF0000);

        expect(eq(sink->signalName(), std::string_view("Voltage")));
        expect(eq(sink->sampleRate(), 1000.0f));
        expect(eq(sink->color(), 0xFF0000U));
    };

    "TestStreamingSink capacity requests"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 1000);

        sink->requestCapacity("chart1", 3000);
        expect(eq(sink->bufferCapacity(), 3000UZ));

        sink->requestCapacity("chart2", 5000);
        expect(eq(sink->bufferCapacity(), 5000UZ));

        // Test auto-expiry with a very short timeout
        sink->requestCapacity("chart3", 7000, std::chrono::seconds{0});
        expect(eq(sink->bufferCapacity(), 7000UZ));

        // Expire requests (chart3 should be removed)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sink->expireCapacityRequests();
        expect(eq(sink->bufferCapacity(), 5000UZ)); // back to chart2's request
    };

    "TestDataSetSink basic usage"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 5);

        expect(eq(sink->uniqueName(), std::string_view("dataset_sink")));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "TestDataSetSink push and retrieve DataSets"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 5);

        // Create a DataSet
        gr::DataSet<float> ds;
        ds.timestamp         = 1000000000; // 1 second in ns
        ds.signal_names      = {"sig1"};
        ds.signal_quantities = {"voltage"};
        ds.signal_units      = {"V"};
        ds.axis_names        = {"time"};
        ds.axis_units        = {"s"};
        ds.axis_values       = {{0.0f, 0.1f, 0.2f, 0.3f, 0.4f}};
        ds.extents           = {5};
        ds.signal_values     = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

        sink->pushDataSet(ds);

        expect(sink->hasDataSets());
        expect(eq(sink->dataSetCount(), 1UZ));
        expect(eq(sink->size(), 5UZ));

        // Check indexed access (uses first DataSet)
        expect(std::abs(sink->xAt(0) - 0.0f) < 1e-6);
        expect(std::abs(sink->yAt(0) - 1.0f) < 1e-6);

        // Check PlotData accessor
        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 5));
    };

    "TestDataSetSink max capacity"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 3);

        // Push 5 datasets
        for (int i = 0; i < 5; ++i) {
            gr::DataSet<float> ds;
            ds.timestamp     = static_cast<int64_t>(i) * 1000000000;
            ds.signal_names  = {"sig" + std::to_string(i)};
            ds.signal_values = {static_cast<float>(i)};
            sink->pushDataSet(ds);
        }

        // Should only have last 3
        expect(eq(sink->dataSetCount(), 3UZ));

        const auto& raw = sink->rawDataSets();
        expect(eq(raw.front().signal_names[0], std::string("sig2")));
    };

    "SinkRegistry registration"_test = [] {
        auto& registry = SinkRegistry::instance();

        auto sink1 = std::make_shared<TestStreamingSink>("registry_sink1", 100);
        auto sink2 = std::make_shared<TestStreamingSink>("registry_sink2", 100);

        std::size_t initialCount = registry.sinkCount();

        registry.registerSink(sink1);
        registry.registerSink(sink2);

        expect(eq(registry.sinkCount(), initialCount + 2));

        auto found = registry.getSink("registry_sink1");
        expect(found != nullptr);
        expect(eq(found->uniqueName(), std::string_view("registry_sink1")));

        registry.unregisterSink("registry_sink1");
        registry.unregisterSink("registry_sink2");

        expect(eq(registry.sinkCount(), initialCount));
    };

    "SinkRegistry listener"_test = [] {
        auto& registry = SinkRegistry::instance();

        bool addedCalled   = false;
        bool removedCalled = false;

        void* owner = reinterpret_cast<void*>(456);
        registry.addListener(owner, [&](SignalSink& /*sink*/, bool isAdded) {
            if (isAdded) {
                addedCalled = true;
            } else {
                removedCalled = true;
            }
        });

        auto sink = std::make_shared<TestStreamingSink>("registry_listener_test", 100);
        registry.registerSink(sink);
        expect(addedCalled);

        registry.unregisterSink("registry_listener_test");
        expect(removedCalled);

        registry.removeListener(owner);
    };

    "PlotData compatibility with ImPlot"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 100);

        for (int i = 0; i < 10; ++i) {
            sink->pushSample(static_cast<double>(i), static_cast<float>(i * 2));
        }

        auto pd = sink->plotData();

        // Verify the PlotData can be used like ImPlotGetter
        expect(eq(pd.count, 10));
        expect(pd.getter != nullptr);

        // The getter should return correct values
        auto p0 = pd.getter(0, pd.userData);
        expect(std::abs(p0.x - 0.0) < 1e-9);
        expect(std::abs(p0.y - 0.0) < 1e-9);

        auto p5 = pd.getter(5, pd.userData);
        expect(std::abs(p5.x - 5.0) < 1e-9);
        expect(std::abs(p5.y - 10.0) < 1e-9);
    };

    return 0;
}
