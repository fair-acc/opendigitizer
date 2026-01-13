#include "../charts/SignalSinkImpl.hpp"

#include <boost/ut.hpp>

#include <cmath>
#include <numbers>

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::ui::charts;

    "StreamingSignalSink basic usage"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 100);

        expect(eq(sink->uniqueName(), std::string_view("test_sink")));
        expect(eq(sink->signalName(), std::string_view("test_sink")));
        expect(eq(sink->bufferCapacity(), 100UZ));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "StreamingSignalSink push and retrieve data"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 100);

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

    "StreamingSignalSink circular buffer behavior"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 50);

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

    "StreamingSignalSink metadata"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 100);

        sink->setSignalName("Voltage");
        sink->setSampleRate(1000.0f);
        sink->setColor(0xFF0000);

        expect(eq(sink->signalName(), std::string_view("Voltage")));
        expect(eq(sink->sampleRate(), 1000.0f));
        expect(eq(sink->color(), 0xFF0000U));
    };

    "StreamingSignalSink capacity requests"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 1000);

        void* chart1 = reinterpret_cast<void*>(1);
        void* chart2 = reinterpret_cast<void*>(2);

        // Note: minimum capacity is 2048, so requests below that stay at 2048
        sink->requestCapacity(chart1, 3000);
        expect(eq(sink->bufferCapacity(), 3000UZ));

        sink->requestCapacity(chart2, 5000);
        expect(eq(sink->bufferCapacity(), 5000UZ));

        sink->releaseCapacity(chart2);
        expect(eq(sink->bufferCapacity(), 3000UZ));
    };

    "DataSetSignalSink basic usage"_test = [] {
        auto sink = std::make_shared<DataSetSignalSink<float>>("dataset_sink", 5);

        expect(eq(sink->uniqueName(), std::string_view("dataset_sink")));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "DataSetSignalSink push and retrieve DataSets"_test = [] {
        auto sink = std::make_shared<DataSetSignalSink<float>>("dataset_sink", 5);

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

    "DataSetSignalSink max capacity"_test = [] {
        auto sink = std::make_shared<DataSetSignalSink<float>>("dataset_sink", 3);

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

    "DataSetSignalSink dataSets span"_test = [] {
        auto sink = std::make_shared<DataSetSignalSink<float>>("dataset_sink", 5);

        gr::DataSet<float> ds1;
        ds1.signal_names  = {"sig1"};
        ds1.signal_values = {1.0f, 2.0f, 3.0f};
        sink->pushDataSet(ds1);

        gr::DataSet<float> ds2;
        ds2.signal_names  = {"sig2"};
        ds2.signal_values = {4.0f, 5.0f, 6.0f};
        sink->pushDataSet(ds2);

        auto span = sink->dataSets();
        expect(eq(span.size(), 2UZ));
        expect(eq(span[0].signal_names[0], std::string("sig1")));
        expect(eq(span[1].signal_names[0], std::string("sig2")));
    };

    "SignalSinkManager registration"_test = [] {
        auto& mgr = SignalSinkManager::instance();

        auto sink1 = std::make_shared<StreamingSignalSink<float>>("sink1", 100);
        auto sink2 = std::make_shared<StreamingSignalSink<float>>("sink2", 100);

        std::size_t initialCount = mgr.sinkCount();

        mgr.registerSink(sink1);
        mgr.registerSink(sink2);

        expect(eq(mgr.sinkCount(), initialCount + 2));

        auto* found = mgr.findSinkByName("sink1");
        expect(found != nullptr);
        expect(eq(found->uniqueName(), std::string_view("sink1")));

        mgr.unregisterSink("sink1");
        mgr.unregisterSink("sink2");

        expect(eq(mgr.sinkCount(), initialCount));
    };

    "SignalSinkManager listener"_test = [] {
        auto& mgr = SignalSinkManager::instance();

        bool addedCalled   = false;
        bool removedCalled = false;

        void* owner = reinterpret_cast<void*>(123);
        mgr.addListener(owner, [&](SignalSinkBase& /*sink*/, bool isAdded) {
            if (isAdded) {
                addedCalled = true;
            } else {
                removedCalled = true;
            }
        });

        auto sink = std::make_shared<StreamingSignalSink<float>>("listener_test", 100);
        mgr.registerSink(sink);
        expect(addedCalled);

        mgr.unregisterSink("listener_test");
        expect(removedCalled);

        mgr.removeListener(owner);
    };

    "PlotData compatibility with ImPlot"_test = [] {
        auto sink = std::make_shared<StreamingSignalSink<float>>("test_sink", 100);

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
