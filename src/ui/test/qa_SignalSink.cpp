#include "TestSinks.hpp"

#include <boost/ut.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::charts;
    using namespace opendigitizer::test;

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

        for (std::size_t i = 0; i < 50; ++i) {
            double x = static_cast<double>(i) * 0.01;
            float  y = std::sin(static_cast<float>(i) * 0.1f);
            sink->pushSample(x, y);
        }

        expect(eq(sink->size(), 50UZ));

        expect(std::abs(sink->xAt(0) - 0.0) < 1e-9);
        expect(std::abs(sink->xAt(49) - 0.49) < 1e-9);

        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 50));
        expect(pd.getter != nullptr);

        auto point = pd.getter(0, pd.user_data);
        expect(std::abs(point.x - 0.0) < 1e-9);
    };

    "TestStreamingSink circular buffer behavior"_test = [] {
        auto sink = std::make_shared<TestStreamingSink>("test_sink", 50);

        for (std::size_t i = 0; i < 100; ++i) {
            double x = static_cast<double>(i);
            float  y = static_cast<float>(i);
            sink->pushSample(x, y);
        }

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

        sink->requestCapacity("chart3", 7000, std::chrono::seconds{0});
        expect(eq(sink->bufferCapacity(), 7000UZ));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sink->expireCapacityRequests();
        expect(eq(sink->bufferCapacity(), 5000UZ));
    };

    "TestDataSetSink basic usage"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 5);

        expect(eq(sink->uniqueName(), std::string_view("dataset_sink")));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "TestDataSetSink push and retrieve DataSets"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 5);

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

        expect(std::abs(sink->xAt(0) - 0.0f) < 1e-6);
        expect(std::abs(sink->yAt(0) - 1.0f) < 1e-6);

        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 5));
    };

    "TestDataSetSink max capacity"_test = [] {
        auto sink = std::make_shared<TestDataSetSink>("dataset_sink", 3);

        for (int i = 0; i < 5; ++i) {
            gr::DataSet<float> ds;
            ds.timestamp     = static_cast<int64_t>(i) * 1000000000;
            ds.signal_names  = {"sig" + std::to_string(i)};
            ds.signal_values = {static_cast<float>(i)};
            sink->pushDataSet(ds);
        }

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

        expect(eq(pd.count, 10));
        expect(pd.getter != nullptr);

        auto p0 = pd.getter(0, pd.user_data);
        expect(std::abs(p0.x - 0.0) < 1e-9);
        expect(std::abs(p0.y - 0.0) < 1e-9);

        auto p5 = pd.getter(5, pd.user_data);
        expect(std::abs(p5.x - 5.0) < 1e-9);
        expect(std::abs(p5.y - 10.0) < 1e-9);
    };

    return 0;
}
