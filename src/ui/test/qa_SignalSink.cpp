#include "TestSinks.hpp"

#include <boost/ut.hpp>

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

using namespace gr::od;

int main() {
    using namespace boost::ut;
    using namespace charts;
    using namespace test;

    "TestStreamingSink basic usage"_test = [] {
        auto sink = makeTestStreamingSink("test_sink", 100);

        expect(eq(sink->uniqueName(), std::string_view("test_sink")));
        expect(eq(sink->signalName(), std::string_view("test_sink")));
        expect(eq(sink->bufferCapacity(), 100UZ));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "TestStreamingSink push and retrieve data"_test = [] {
        auto sink = makeTestStreamingSink("test_sink", 100);

        for (std::size_t i = 0; i < 50; ++i) {
            double x = static_cast<double>(i) * 0.01;
            float  y = std::sin(static_cast<float>(i) * 0.1f);
            sink->pushSample(x, y);
        }

        expect(eq(sink->size(), 50UZ));

        expect(std::abs(sink->samples().x[0] - 0.0) < 1e-9);
        expect(std::abs(sink->samples().x[49] - 0.49) < 1e-9);
    };

    // ground truth by construction: sinks of 5 and 3 samples, 0…4 and 10…12, a chart showing 3 pairs 2…4 with 10…12
    "a chart's window is taken from the newest end, so sinks of different lengths pair their newest samples"_test = [] {
        auto longer  = makeTestStreamingSink("longer", 8);
        auto shorter = makeTestStreamingSink("shorter", 8);
        for (int i = 0; i < 5; ++i) {
            longer->pushSample(static_cast<double>(i), static_cast<float>(i));
        }
        for (int i = 0; i < 3; ++i) {
            shorter->pushSample(static_cast<double>(i), static_cast<float>(10 + i));
        }
        const SampleWindow x = longer->samples().newest(3UZ);
        const SampleWindow y = shorter->samples().newest(3UZ);
        expect(eq(x.size(), 3UZ) && eq(y.size(), 3UZ));
        expect(eq(x.yAt(0UZ), 2.0) && eq(y.yAt(0UZ), 10.0)) << "the oldest pair shown";
        expect(eq(x.yAt(2UZ), 4.0) && eq(y.yAt(2UZ), 12.0)) << "the newest pair";
        expect(eq(x.first, 2UZ)) << "its place among every sample received";
        expect(eq(longer->samples().newest(99UZ).size(), 5UZ)) << "all when fewer are held";
    };

    // ground truth by construction: events 0…4 at 0…4 s into a sink holding 3; a chart asking 10 raises it, its expiry restores 3
    "an event sink holds its newest events at their arrival time and counts every one"_test = [] {
        SignalSink sink(SignalSink::Config{.kind = SignalKind::Events, .uniqueName = "events", .name = "events", .capacity = 3UZ, .capacityFloor = 0UZ, .doubleSamples = false});
        for (std::int64_t n = 0; n < 5; ++n) {
            sink.pushEvent(static_cast<double>(n), gr::property_map{{"n", n}});
        }
        sink.keepNewestEvents(sink.bufferCapacity());
        expect(eq(sink.events().size(), 3UZ) >> fatal);
        expect(eq(sink.events().front().map.value_or<std::int64_t>("n", -1), std::int64_t{2}));
        expect(eq(sink.events().back().utcSeconds, 4.0));
        expect(eq(sink.eventGeneration(), 5U)) << "every event counts, held or not";

        const auto now = CapacityRequests::clock::now();
        sink.requestCapacity("chart", 10UZ, std::chrono::seconds{60}, now);
        expect(eq(sink.bufferCapacity(), 10UZ)) << "at least what a chart asks";
        sink.expireCapacityRequests(now + std::chrono::seconds{61});
        expect(eq(sink.bufferCapacity(), 3UZ)) << "at least its own";
    };

    "TestStreamingSink circular buffer behavior"_test = [] {
        auto sink = makeTestStreamingSink("test_sink", 50);

        for (std::size_t i = 0; i < 100; ++i) {
            double x = static_cast<double>(i);
            float  y = static_cast<float>(i);
            sink->pushSample(x, y);
        }

        expect(eq(sink->size(), 50UZ));

        expect(std::abs(sink->samples().x[0] - 50.0) < 1e-9);
        expect(std::abs(sink->samples().x[49] - 99.0) < 1e-9);
    };

    "TestStreamingSink metadata"_test = [] {
        auto sink = makeTestStreamingSink("test_sink", 100);

        sink->metadata.signalName = "Voltage";
        sink->metadata.sampleRate = 1000.0f;
        sink->metadata.color      = 0xFF0000;

        expect(eq(sink->signalName(), std::string_view("Voltage")));
        expect(eq(sink->sampleRate(), 1000.0f));
        expect(eq(sink->color(), 0xFF0000U));
    };

    "TestStreamingSink capacity requests"_test = [] {
        auto sink = makeTestStreamingSink("test_sink", 1000);

        sink->requestCapacity("chart1", 3000);
        expect(eq(sink->bufferCapacity(), 3000UZ));

        sink->requestCapacity("chart2", 5000);
        expect(eq(sink->bufferCapacity(), 5000UZ));

        const auto now = CapacityRequests::clock::now();
        sink->requestCapacity("chart3", 7000, std::chrono::seconds{0}, now);
        expect(eq(sink->bufferCapacity(), 7000UZ));

        sink->expireCapacityRequests(now + std::chrono::milliseconds(10));
        expect(eq(sink->bufferCapacity(), 5000UZ));
    };

    "TestDataSetSink basic usage"_test = [] {
        auto sink = makeTestDataSetSink("dataset_sink", 5);

        expect(eq(sink->uniqueName(), std::string_view("dataset_sink")));
        expect(eq(sink->size(), 0UZ));
        expect(!sink->hasDataSets());
    };

    "TestDataSetSink push and retrieve DataSets"_test = [] {
        auto sink = makeTestDataSetSink("dataset_sink", 5);

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
        expect(eq(sink->dataSets().size(), 1UZ));
        expect(eq(sink->size(), 0UZ)) << "no streaming samples";
        expect(eq(sink->dataSets().back().axis_values[0].size(), 5UZ));
        expect(approx(sink->dataSets().back().signal_values[0], 1.0f, 1e-6f));
    };

    "TestDataSetSink max capacity"_test = [] {
        auto sink = makeTestDataSetSink("dataset_sink", 3);

        for (int i = 0; i < 5; ++i) {
            gr::DataSet<float> ds;
            ds.timestamp = static_cast<int64_t>(i) * 1000000000;
            ds.signal_names.emplace_back("sig" + std::to_string(i));
            ds.signal_values = {static_cast<float>(i)};
            sink->pushDataSet(ds);
        }

        expect(eq(sink->dataSets().size(), 3UZ));

        const auto& raw = sink->dataSets();
        expect(eq(std::string_view(raw.front().signal_names[0]), std::string_view("sig2")));
    };

    // ground truth by construction: four DataSets of equal size into a history of three; the fourth takes the oldest's buffers
    "a full DataSet history reuses the oldest entry's buffers"_test = [] {
        auto       sink      = makeTestDataSetSink("reuse_sink", 3);
        const auto makeFrame = [](float value) {
            gr::DataSet<float> ds;
            ds.signal_names  = {"sig"};
            ds.axis_values   = {{0.0f, 1.0f, 2.0f, 3.0f}};
            ds.extents       = {4};
            ds.signal_values = {value, value, value, value};
            return ds;
        };
        for (float value : {1.0f, 2.0f, 3.0f}) {
            sink->pushDataSet(makeFrame(value));
        }
        const float* oldestBuffer = sink->dataSets().front().signal_values.data();
        const auto   fourth       = makeFrame(4.0f);
        sink->pushDataSet(fourth);

        expect(eq(sink->dataSets().size(), 3UZ) >> fatal);
        expect(eq(sink->dataSets().front().signal_values[0], 2.0f)) << "the oldest dropped";
        expect(eq(sink->dataSets().back().signal_values[0], 4.0f));
        expect(sink->dataSets().back().signal_values.data() == oldestBuffer) << "no allocation for the newest";
        expect(eq(sink->totalSampleCount(), 4UZ));
    };

    // ground truth by construction: rows 0 and 1 hold 10 and 20; dataset_index 1 selects 20, an index past the rows falls back to row 0
    "dataset_index selects the DataSet row a 1D chart draws"_test = [] {
        auto               sink = makeTestDataSetSink("row_sink", 2);
        gr::DataSet<float> ds;
        ds.signal_names  = {"magnitude", "phase"};
        ds.axis_values   = {{0.0f, 1.0f}};
        ds.extents       = {2};
        ds.signal_values = {10.0f, 10.0f, 20.0f, 20.0f};
        sink->pushDataSet(ds);

        const auto drawnRow = [&sink] {
            const auto& newest = sink->dataSets().back();
            return newest.signalValues(sink->datasetRow(newest))[1];
        };
        expect(eq(drawnRow(), 10.0f)) << "unset: row 0";
        sink->metadata.datasetIndex = 1UZ;
        expect(eq(drawnRow(), 20.0f));
        sink->metadata.datasetIndex = 5UZ;
        expect(eq(drawnRow(), 10.0f)) << "out of range: row 0";
    };

    // ground truth by construction: two 2 × 3 images, a = 1…6 and b = 7…12, on axes {0, 1} and {10, 20, 30}
    "a rank-2 DataSet frames as the image of the signal dataset_index selects"_test = [] {
        auto               sink = makeTestDataSetSink("image_sink", 2);
        gr::DataSet<float> ds;
        ds.signal_names  = {"a", "b"};
        ds.axis_names    = {"y", "x"};
        ds.axis_units    = {"m", "s"};
        ds.axis_values   = {{0.0f, 1.0f}, {10.0f, 20.0f, 30.0f}};
        ds.extents       = {2, 3};
        ds.signal_values = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f};
        sink->pushDataSet(ds);

        auto frame = sink->frame();
        expect(eq(frame.rows, 2UZ) && eq(frame.columns, 3UZ) && eq(frame.generation, 1U));
        expect(eq(frame.row(1UZ).front(), 4.f)) << "row 1 of signal a";
        expect(eq(frame.axes[1].origin, 10.0) && eq(frame.axes[1].step, 10.0) && eq(frame.axes[1].unit, std::string("s")));
        expect(frame.dataSet == &sink->dataSets().back());
        sink->metadata.datasetIndex = 1UZ;
        expect(eq(sink->frame().row(0UZ).front(), 7.f)) << "signal b";
    };

    // ground truth by construction: two signals of three points per DataSet, three DataSets into a history of two
    "a rank-1 DataSet frames its signals as rows, generations counted like tensor frames"_test = [] {
        auto sink = makeTestDataSetSink("rows_sink", 2);
        for (float base : {0.f, 10.f, 20.f}) {
            gr::DataSet<float> ds;
            ds.signal_names  = {"magnitude", "phase"};
            ds.axis_values   = {{1.f, 2.f, 4.f}};
            ds.extents       = {3};
            ds.signal_values = {base, base, base, base + 1.f, base + 1.f, base + 1.f};
            sink->pushDataSet(ds);
        }
        const auto newest = sink->frame();
        expect(eq(newest.rows, 2UZ) && eq(newest.columns, 3UZ) && eq(newest.generation, 3U));
        expect(eq(newest.row(1UZ).front(), 21.f)) << "phase of the newest";
        expect(eq(newest.columnCoordinates.size(), 3UZ) && eq(newest.columnCoordinates[2], 4.f)) << "non-uniform axis values kept";
        expect(eq(sink->frame(1U).generation, 2U)) << "clamped to the oldest held";
        expect(eq(sink->frame(1U).row(0UZ).front(), 10.f));
    };

    "a double tensor sink keeps its frames as float"_test = [] {
        SignalSink         sink(SignalSink::Config{.kind = SignalKind::Image2D, .uniqueName = "tensor", .name = "tensor", .capacity = 1UZ, .capacityFloor = 0UZ, .doubleSamples = false});
        gr::Tensor<double> tensor;
        tensor.resize({2UZ, 2UZ});
        std::ranges::copy(std::array{1.5, 2.5, 3.5, 4.5}, tensor.data());
        std::vector<gr::Tensor<double>> frames;
        frames.push_back(std::move(tensor));
        sink.pushFrames<float>(frames, 1UZ);
        const auto frame = sink.frame();
        expect(eq(frame.rows, 2UZ) && eq(frame.columns, 2UZ) >> fatal);
        expect(eq(frame.row(1UZ)[1], 4.5f));
    };

    // ground truth, arithmetic: (1 - 0.3)^2 = 0.49, 2 × 5 = 10
    "an older DataSet fades by the opacity decay and rises by the vertical offset"_test = [] {
        const auto newest = historyTrace(0UZ, 0.3f, 5.0f);
        expect(eq(newest.opacity, 1.0f) && eq(newest.yOffset, 0.0));
        const auto twoBack = historyTrace(2UZ, 0.3f, 5.0f);
        expect(approx(twoBack.opacity, 0.49f, 1e-6f));
        expect(eq(twoBack.yOffset, 10.0));
        expect(eq(historyTrace(1UZ, 1.0f, 0.0f).opacity, 0.0f)) << "decay 1: only the newest shows";
    };

    "SinkRegistry registration"_test = [] {
        auto& registry = SinkRegistry::instance();

        auto sink1 = makeTestStreamingSink("registry_sink1", 100);
        auto sink2 = makeTestStreamingSink("registry_sink2", 100);

        std::size_t initialCount = registry.sinkCount();

        registry.registerSink(sink1);
        registry.registerSink(sink2);

        expect(eq(registry.sinkCount(), initialCount + 2));

        auto found = registry.getSink("registry_sink1");
        if (found == nullptr) {
            expect(false) << "registry_sink1 should be registered";
        } else {
            expect(eq(found->uniqueName(), std::string_view("registry_sink1")));
        }

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

        auto sink = makeTestStreamingSink("registry_listener_test", 100);
        registry.registerSink(sink);
        expect(addedCalled);

        registry.unregisterSink("registry_listener_test");
        expect(removedCalled);

        registry.removeListener(owner);
    };

    return 0;
}
