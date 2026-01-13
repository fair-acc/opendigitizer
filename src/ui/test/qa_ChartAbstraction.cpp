#include "../charts/Charts.hpp"

#include <boost/ut.hpp>

#include <cmath>
#include <deque>
#include <vector>

namespace {

/**
 * @brief Minimal test implementation of SignalSink for chart abstraction testing.
 */
class TestStreamingSink : public opendigitizer::SignalSink {
    std::string         _uniqueName;
    std::string         _signalName;
    std::uint32_t       _color      = 0xFFFFFF;
    float               _sampleRate = 1000.0f;
    std::vector<double> _xValues;
    std::vector<float>  _yValues;
    std::size_t         _capacity;
    mutable std::mutex  _mutex;
    bool                _drawEnabled = true;

public:
    explicit TestStreamingSink(std::string name, std::size_t capacity = 2048) : _uniqueName(std::move(name)), _signalName(_uniqueName), _capacity(capacity) {
        _xValues.reserve(capacity);
        _yValues.reserve(capacity);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
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

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _capacity; }
    void                      requestCapacity(void* /*consumer*/, std::size_t cap) override { _capacity = std::max(_capacity, cap); }
    void                      releaseCapacity(void* /*consumer*/) override {}

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

    void pushSample(double x, float y) {
        if (_xValues.size() >= _capacity) {
            _xValues.erase(_xValues.begin());
            _yValues.erase(_yValues.begin());
        }
        _xValues.push_back(x);
        _yValues.push_back(y);
    }
};

class TestDataSetSink : public opendigitizer::SignalSink {
    std::string                    _uniqueName;
    std::deque<gr::DataSet<float>> _dataSets;
    std::size_t                    _maxDataSets;
    mutable std::mutex             _mutex;
    bool                           _drawEnabled = true;

public:
    explicit TestDataSetSink(std::string name, std::size_t maxDataSets = 10) : _uniqueName(std::move(name)), _maxDataSets(maxDataSets) {}

    [[nodiscard]] std::string_view name() const noexcept override { return _uniqueName; }
    [[nodiscard]] std::string_view uniqueName() const noexcept override { return _uniqueName; }
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
    [[nodiscard]] std::span<const gr::DataSet<float>> dataSets() const override { return {}; }

    [[nodiscard]] bool                      hasStreamingTags() const noexcept override { return false; }
    [[nodiscard]] std::pair<double, double> tagTimeRange() const noexcept override { return {0.0, 0.0}; }
    void                                    forEachTag(std::function<void(double, const gr::property_map&)> /*callback*/) const override {}

    [[nodiscard]] double timeFirst() const noexcept override { return 0.0; }
    [[nodiscard]] double timeLast() const noexcept override { return 0.0; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept override { return _maxDataSets; }
    void                      requestCapacity(void* /*consumer*/, std::size_t /*cap*/) override {}
    void                      releaseCapacity(void* /*consumer*/) override {}

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

    void pushDataSet(gr::DataSet<float> ds) {
        if (_dataSets.size() >= _maxDataSets) {
            _dataSets.pop_front();
        }
        _dataSets.push_back(std::move(ds));
    }
};

// Helper functions for test sink creation
template<typename T = float>
std::shared_ptr<TestStreamingSink> makeTestStreamingSink(std::string name, std::size_t capacity = 2048) {
    return std::make_shared<TestStreamingSink>(std::move(name), capacity);
}

template<typename T = float>
std::shared_ptr<TestDataSetSink> makeTestDataSetSink(std::string name, std::size_t maxDataSets = 10) {
    return std::make_shared<TestDataSetSink>(std::move(name), maxDataSets);
}

} // anonymous namespace

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::charts;

    "AxisConfig defaults"_test = [] {
        AxisConfig axis;

        expect(axis.scale == AxisScale::Linear);
        expect(axis.format == LabelFormat::Auto);
        expect(axis.autoScale);
        expect(axis.showGrid);
    };

    "XYChart creation via makeXYChart"_test = [] {
        auto chart = makeXYChart("TestChart");

        expect(eq(chart->chartTypeName(), std::string_view("XYChart")));
        expect(chart->uniqueId().starts_with("XYChart_"));
        expect(eq(chart->signalSinkCount(), 0UZ));
    };

    "XYChart signal sink management"_test = [] {
        auto chart = makeXYChart();

        auto sink1 = makeTestStreamingSink("sink1");
        auto sink2 = makeTestStreamingSink("sink2");

        chart->addSignalSink(sink1);
        chart->addSignalSink(sink2);

        expect(eq(chart->signalSinkCount(), 2UZ));

        const auto& sinks = chart->signalSinks();
        expect(eq(sinks[0]->uniqueName(), std::string_view("sink1")));
        expect(eq(sinks[1]->uniqueName(), std::string_view("sink2")));

        chart->removeSignalSink("sink1");
        expect(eq(chart->signalSinkCount(), 1UZ));
    };

    "XYChart clear sinks"_test = [] {
        auto chart = makeXYChart();

        chart->addSignalSink(makeTestStreamingSink("sink1"));
        chart->addSignalSink(makeTestStreamingSink("sink2"));

        expect(eq(chart->signalSinkCount(), 2UZ));

        chart->clearSignalSinks();
        expect(eq(chart->signalSinkCount(), 0UZ));
    };

    "ChartInterface polymorphism"_test = [] {
        auto xyChart = makeXYChart("PolyTest");

        ChartInterface* chartInterface = xyChart.get();

        expect(chartInterface != nullptr);
        expect(eq(chartInterface->chartTypeName(), std::string_view("XYChart")));

        auto sink = makeTestStreamingSink("poly_sink");
        chartInterface->addSignalSink(sink);
        expect(eq(chartInterface->signalSinkCount(), 1UZ));

        chartInterface->clearSignalSinks();
        expect(eq(chartInterface->signalSinkCount(), 0UZ));
    };

    "makeChartByType creates correct chart type"_test = [] {
        auto [xyStorage, xyChart] = makeChartByType("XYChart", "TestXY");
        expect(xyChart != nullptr);
        expect(eq(xyChart->chartTypeName(), std::string_view("XYChart")));

        auto [yyStorage, yyChart] = makeChartByType("YYChart", "TestYY");
        expect(yyChart != nullptr);
        expect(eq(yyChart->chartTypeName(), std::string_view("YYChart")));

        auto [unknownStorage, unknownChart] = makeChartByType("UnknownChart");
        expect(unknownChart == nullptr);
    };

    "registeredChartTypes returns known types"_test = [] {
        auto types = registeredChartTypes();

        expect(std::find(types.begin(), types.end(), "XYChart") != types.end());
        expect(std::find(types.begin(), types.end(), "YYChart") != types.end());
    };

    "Helper functions"_test = [] {
        auto streamingSink = makeTestStreamingSink("helper_streaming");
        expect(streamingSink != nullptr);
        expect(eq(streamingSink->uniqueName(), std::string_view("helper_streaming")));

        auto datasetSink = makeTestDataSetSink("helper_dataset");
        expect(datasetSink != nullptr);
        expect(eq(datasetSink->uniqueName(), std::string_view("helper_dataset")));

        auto xyChart = makeXYChart();
        expect(xyChart != nullptr);
        expect(eq(xyChart->chartTypeName(), std::string_view("XYChart")));

        auto yyChart = makeYYChart();
        expect(yyChart != nullptr);
        expect(eq(yyChart->chartTypeName(), std::string_view("YYChart")));
    };

    "Signal shared between multiple charts"_test = [] {
        auto sink = makeTestStreamingSink("shared_signal");

        for (int i = 0; i < 100; ++i) {
            sink->pushSample(static_cast<double>(i) * 0.01, std::sin(static_cast<float>(i) * 0.1f));
        }

        auto chart1 = makeXYChart();
        auto chart2 = makeXYChart();

        chart1->addSignalSink(sink);
        chart2->addSignalSink(sink);

        expect(eq(chart1->signalSinks()[0]->size(), chart2->signalSinks()[0]->size()));

        sink->requestCapacity(chart1.get(), 3000);
        sink->requestCapacity(chart2.get(), 5000);

        expect(eq(sink->bufferCapacity(), 5000UZ));
    };

    "PlotData can be used for rendering"_test = [] {
        auto sink = makeTestStreamingSink("plot_data_test");

        for (int i = 0; i < 50; ++i) {
            sink->pushSample(static_cast<double>(i), static_cast<float>(i * 2));
        }

        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 50));

        auto p0 = pd.getter(0, pd.userData);
        expect(std::abs(p0.x - 0.0) < 1e-9);
        expect(std::abs(p0.y - 0.0) < 1e-9);

        auto p25 = pd.getter(25, pd.userData);
        expect(std::abs(p25.x - 25.0) < 1e-9);
        expect(std::abs(p25.y - 50.0) < 1e-9);
    };

    "YYChart creation"_test = [] {
        auto chart = makeYYChart();

        expect(eq(chart->chartTypeName(), std::string_view("YYChart")));
        expect(chart->uniqueId().starts_with("YYChart_"));
    };

    "YYChart with two sinks"_test = [] {
        auto chart = makeYYChart();
        auto sink1 = makeTestStreamingSink("yy_sink1");
        auto sink2 = makeTestStreamingSink("yy_sink2");

        chart->addSignalSink(sink1);
        chart->addSignalSink(sink2);

        expect(eq(chart->signalSinks().size(), 2UZ));

        for (int i = 0; i < 100; ++i) {
            double t = static_cast<double>(i) * 0.01;
            sink1->pushSample(t, std::sin(2.0f * 3.14159f * static_cast<float>(t)));
            sink2->pushSample(t, std::sin(5.0f * 3.14159f * static_cast<float>(t)));
        }

        expect(eq(sink1->size(), 100UZ));
        expect(eq(sink2->size(), 100UZ));
    };

    "YYChart as ChartInterface"_test = [] {
        auto yyChart = makeYYChart("YYPolyTest");

        ChartInterface* chartInterface = yyChart.get();
        expect(chartInterface != nullptr);
        expect(eq(chartInterface->chartTypeName(), std::string_view("YYChart")));

        auto sink = makeTestStreamingSink("yy_poly_sink");
        chartInterface->addSignalSink(sink);
        expect(eq(chartInterface->signalSinkCount(), 1UZ));
    };

    return 0;
}
