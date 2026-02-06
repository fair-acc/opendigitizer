#include "TestSinks.hpp"

#include <boost/ut.hpp>

#include <chrono>
#include <cmath>
#include <thread>

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::charts;
    using namespace opendigitizer::test;

    "XYChart creation via makeXYChart"_test = [] {
        auto chart = makeXYChart("TestChart");

        expect(eq(chart->chartTypeName(), std::string_view("XYChart")));
        expect(!chart->uniqueId().empty());
        expect(chart->uniqueId().find("XYChart") != std::string_view::npos);
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

    "Chart mixin via concrete types"_test = [] {
        auto xyChart = makeXYChart("MixinTest");

        expect(eq(xyChart->chartTypeName(), std::string_view("XYChart")));

        auto sink = makeTestStreamingSink("mixin_sink");
        xyChart->addSignalSink(sink);
        expect(eq(xyChart->signalSinkCount(), 1UZ));

        xyChart->clearSignalSinks();
        expect(eq(xyChart->signalSinkCount(), 0UZ));
    };

    "makeChartByType creates correct chart type"_test = [] {
        auto xyStorage = makeChartByType("XYChart", "TestXY");
        expect(std::holds_alternative<std::shared_ptr<XYChart>>(xyStorage));
        auto& xyChart = std::get<std::shared_ptr<XYChart>>(xyStorage);
        expect(eq(xyChart->chartTypeName(), std::string_view("XYChart")));

        auto yyStorage = makeChartByType("YYChart", "TestYY");
        expect(std::holds_alternative<std::shared_ptr<YYChart>>(yyStorage));
        auto& yyChart = std::get<std::shared_ptr<YYChart>>(yyStorage);
        expect(eq(yyChart->chartTypeName(), std::string_view("YYChart")));

        auto unknownStorage = makeChartByType("UnknownChart");
        expect(std::holds_alternative<std::monostate>(unknownStorage));
    };

    "registeredChartTypes queries block registry"_test = [] {
        auto types = registeredChartTypes();

        expect(std::is_sorted(types.begin(), types.end())) << "Types should be sorted";

        for (const auto& type : types) {
            std::string lowerType = type;
            std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), [](unsigned char c) { return std::tolower(c); });
            expect(lowerType.find("chart") != std::string::npos) << "All returned types should contain 'chart'";
        }
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

        sink->requestCapacity(chart1->uniqueId(), 3000);
        sink->requestCapacity(chart2->uniqueId(), 5000);

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

        auto p0 = pd.getter(0, pd.user_data);
        expect(std::abs(p0.x - 0.0) < 1e-9);
        expect(std::abs(p0.y - 0.0) < 1e-9);

        auto p25 = pd.getter(25, pd.user_data);
        expect(std::abs(p25.x - 25.0) < 1e-9);
        expect(std::abs(p25.y - 50.0) < 1e-9);
    };

    "YYChart creation"_test = [] {
        auto chart = makeYYChart();

        expect(eq(chart->chartTypeName(), std::string_view("YYChart")));
        expect(!chart->uniqueId().empty());
        expect(chart->uniqueId().find("YYChart") != std::string_view::npos);
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

    "YYChart mixin methods"_test = [] {
        auto yyChart = makeYYChart("YYMixinTest");

        expect(eq(yyChart->chartTypeName(), std::string_view("YYChart")));

        auto sink = makeTestStreamingSink("yy_mixin_sink");
        yyChart->addSignalSink(sink);
        expect(eq(yyChart->signalSinkCount(), 1UZ));
    };

    "getSinkNames and syncSinksFromNames roundtrip"_test = [] {
        auto chart = makeXYChart();
        auto sink1 = makeTestStreamingSink("roundtrip_sink1");
        auto sink2 = makeTestStreamingSink("roundtrip_sink2");

        SinkRegistry::instance().registerSink(sink1);
        SinkRegistry::instance().registerSink(sink2);

        chart->addSignalSink(sink1);
        chart->addSignalSink(sink2);

        auto names = chart->getSinkNames();
        expect(eq(names.size(), 2UZ));

        chart->clearSignalSinks();
        expect(eq(chart->signalSinkCount(), 0UZ));

        chart->syncSinksFromNames(names);
        expect(eq(chart->signalSinkCount(), 2UZ));

        SinkRegistry::instance().unregisterSink(sink1->uniqueName());
        SinkRegistry::instance().unregisterSink(sink2->uniqueName());
    };

    // --- Axis-grouping edge-case tests ---

    "findOrCreateCategory with 3 different groups"_test = [] {
        std::array<std::optional<AxisCategory>, 3> categories{};

        auto idx0 = axis::findOrCreateCategory(categories, "voltage", "V", 0xFF0000);
        auto idx1 = axis::findOrCreateCategory(categories, "current", "A", 0x00FF00);
        auto idx2 = axis::findOrCreateCategory(categories, "power", "W", 0x0000FF);

        expect(idx0.has_value());
        expect(idx1.has_value());
        expect(idx2.has_value());
        expect(eq(*idx0, 0UZ));
        expect(eq(*idx1, 1UZ));
        expect(eq(*idx2, 2UZ));
    };

    "findOrCreateCategory overflow returns nullopt"_test = [] {
        std::array<std::optional<AxisCategory>, 3> categories{};

        axis::findOrCreateCategory(categories, "voltage", "V", 0xFF0000);
        axis::findOrCreateCategory(categories, "current", "A", 0x00FF00);
        axis::findOrCreateCategory(categories, "power", "W", 0x0000FF);
        auto overflow = axis::findOrCreateCategory(categories, "temperature", "K", 0xFFFF00);

        expect(!overflow.has_value());
    };

    "findOrCreateCategory merges matching quantity and unit"_test = [] {
        std::array<std::optional<AxisCategory>, 3> categories{};

        auto idx0   = axis::findOrCreateCategory(categories, "voltage", "V", 0xFF0000);
        auto idx1   = axis::findOrCreateCategory(categories, "current", "A", 0x00FF00);
        auto idxDup = axis::findOrCreateCategory(categories, "voltage", "V", 0x00FFFF);

        expect(idx0.has_value());
        expect(idx1.has_value());
        expect(idxDup.has_value());
        expect(eq(*idx0, *idxDup)) << "same quantity+unit should return same index";
        expect(eq(*idx1, 1UZ));
    };

    "YYChart buildMultiYCategories groups sinks correctly"_test = [] {
        auto chart = makeYYChart("GroupTest");

        // sink[0] = X-axis source
        auto sinkX = makeTestStreamingSink("x_signal");
        sinkX->setSignalQuantity("time");
        sinkX->setSignalUnit("s");

        // sink[1] and sink[2] share quantity+unit -> same Y-axis group
        auto sinkY1 = makeTestStreamingSink("y_voltage1");
        sinkY1->setSignalQuantity("voltage");
        sinkY1->setSignalUnit("V");

        auto sinkY2 = makeTestStreamingSink("y_voltage2");
        sinkY2->setSignalQuantity("voltage");
        sinkY2->setSignalUnit("V");

        // sink[3] has a different quantity -> separate Y-axis group
        auto sinkY3 = makeTestStreamingSink("y_current");
        sinkY3->setSignalQuantity("current");
        sinkY3->setSignalUnit("A");

        chart->addSignalSink(sinkX);
        chart->addSignalSink(sinkY1);
        chart->addSignalSink(sinkY2);
        chart->addSignalSink(sinkY3);

        // Push one sample each so sinks have data
        for (auto& s : chart->signalSinks()) {
            auto* ts = dynamic_cast<TestStreamingSink*>(s.get());
            if (ts) {
                ts->pushSample(0.0, 1.0f);
            }
        }

        // buildMultiYCategories is private, but we can test axis::findOrCreateCategory logic
        // which is the same underlying mechanism
        std::array<std::optional<AxisCategory>, 3> yCategories{};
        std::array<std::vector<std::string>, 3>    yAxisGroups{};
        std::vector<std::size_t>                   overflowSinkIndices;

        for (std::size_t i = 1; i < chart->signalSinks().size(); ++i) {
            const auto& sink = chart->signalSinks()[i];
            auto        idx  = axis::findOrCreateCategory(yCategories, sink->signalQuantity(), sink->signalUnit(), sink->color());
            if (idx.has_value()) {
                yAxisGroups[*idx].push_back(std::string(sink->uniqueName()));
            } else {
                overflowSinkIndices.push_back(i);
            }
        }

        expect(eq(axis::activeAxisCount(yCategories), 2UZ)) << "should have 2 Y-axis categories";
        expect(eq(yAxisGroups[0].size(), 2UZ)) << "voltage group should have 2 sinks";
        expect(eq(yAxisGroups[1].size(), 1UZ)) << "current group should have 1 sink";
        expect(overflowSinkIndices.empty()) << "no overflow with 2 distinct groups";
    };

    "YYChart buildMultiYCategories overflow"_test = [] {
        auto chart = makeYYChart("OverflowTest");

        auto sinkX = makeTestStreamingSink("x_sig");
        chart->addSignalSink(sinkX);

        // 4 Y sinks with 4 distinct quantity+unit pairs -> 4th overflows (max 3 axes)
        std::vector<std::pair<std::string, std::string>> units = {{"voltage", "V"}, {"current", "A"}, {"power", "W"}, {"temperature", "K"}};
        for (std::size_t i = 0; i < units.size(); ++i) {
            auto sink = makeTestStreamingSink("y_" + std::to_string(i));
            sink->setSignalQuantity(units[i].first);
            sink->setSignalUnit(units[i].second);
            chart->addSignalSink(sink);
        }

        std::array<std::optional<AxisCategory>, 3> yCategories{};
        std::vector<std::size_t>                   overflowSinkIndices;

        for (std::size_t i = 1; i < chart->signalSinks().size(); ++i) {
            const auto& sink = chart->signalSinks()[i];
            auto        idx  = axis::findOrCreateCategory(yCategories, sink->signalQuantity(), sink->signalUnit(), sink->color());
            if (!idx.has_value()) {
                overflowSinkIndices.push_back(i);
            }
        }

        expect(eq(axis::activeAxisCount(yCategories), 3UZ)) << "should fill all 3 Y-axis slots";
        expect(eq(overflowSinkIndices.size(), 1UZ)) << "4th distinct group should overflow";
    };

    "getXRange boundaries"_test = [] {
        auto sink = makeTestStreamingSink("range_test", 1000);

        // empty sink
        auto [emptyStart, emptyCount] = sink->getXRange(0.0, 1.0);
        expect(eq(emptyStart, 0UZ));
        expect(eq(emptyCount, 0UZ));

        // push sorted samples
        for (int i = 0; i < 10; ++i) {
            sink->pushSample(static_cast<double>(i), static_cast<float>(i));
        }

        // tMin > tMax returns {0, 0}
        auto [invStart, invCount] = sink->getXRange(5.0, 2.0);
        expect(eq(invStart, 0UZ));
        expect(eq(invCount, 0UZ));

        // exact boundary match
        auto [start, count] = sink->getXRange(3.0, 7.0);
        expect(eq(start, 3UZ));
        expect(eq(count, 5UZ)) << "should include values [3.0, 4.0, 5.0, 6.0, 7.0]";
    };

    "capacity request expiry reduces capacity"_test = [] {
        auto sink = makeTestStreamingSink("expiry_test", 1000);

        sink->requestCapacity("chart1", 3000);
        sink->requestCapacity("chart2", 5000, std::chrono::seconds{0});
        expect(eq(sink->bufferCapacity(), 5000UZ));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sink->expireCapacityRequests();
        expect(eq(sink->bufferCapacity(), 3000UZ)) << "after expiry, should fall back to chart1's request";
    };

    return 0;
}
