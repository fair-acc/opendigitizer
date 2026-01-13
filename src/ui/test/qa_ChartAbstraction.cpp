#include "../charts/Charts.hpp"

#include <boost/ut.hpp>

int main() {
    using namespace boost::ut;
    using namespace opendigitizer::ui::charts;

    "ChartConfig defaults"_test = [] {
        ChartConfig config;

        expect(config.xAxisMode == XAxisMode::RelativeTime);
        expect(config.showLegend);
        expect(config.showTags);
        expect(config.showGrid);
        expect(eq(config.maxHistoryCount, 3UZ));
    };

    "AxisConfig defaults"_test = [] {
        AxisConfig axis;

        expect(axis.scale == AxisScale::Linear);
        expect(axis.format == LabelFormat::Auto);
        expect(axis.autoScale);
        expect(axis.showGrid);
    };

    "XYChart creation"_test = [] {
        ChartConfig config;
        config.name  = "TestChart";
        config.title = "Test Chart Title";

        XYChart chart(config);

        expect(eq(chart.chartTypeName(), std::string_view("XYChart")));
        expect(eq(chart.config().name, std::string("TestChart")));
        expect(eq(chart.signalSinkCount(), 0UZ));
    };

    "XYChart signal sink management"_test = [] {
        XYChart chart;

        auto sink1 = makeStreamingSink<float>("sink1");
        auto sink2 = makeStreamingSink<float>("sink2");

        chart.addSignalSink(sink1);
        chart.addSignalSink(sink2);

        expect(eq(chart.signalSinkCount(), 2UZ));

        const auto& sinks = chart.signalSinks();
        expect(eq(sinks[0]->uniqueName(), std::string_view("sink1")));
        expect(eq(sinks[1]->uniqueName(), std::string_view("sink2")));

        chart.removeSignalSink("sink1");
        expect(eq(chart.signalSinkCount(), 1UZ));
    };

    "XYChart move sinks to another chart"_test = [] {
        XYChart chart1;
        XYChart chart2;

        chart1.addSignalSink(makeStreamingSink<float>("sink1"));
        chart1.addSignalSink(makeStreamingSink<float>("sink2"));

        expect(eq(chart1.signalSinkCount(), 2UZ));
        expect(eq(chart2.signalSinkCount(), 0UZ));

        chart1.moveSignalSinksTo(chart2);

        expect(eq(chart1.signalSinkCount(), 0UZ));
        expect(eq(chart2.signalSinkCount(), 2UZ));
    };

    "XYChart copy sinks to another chart"_test = [] {
        XYChart chart1;
        XYChart chart2;

        auto sink1 = makeStreamingSink<float>("sink1");
        chart1.addSignalSink(sink1);

        chart1.copySignalSinksTo(chart2);

        expect(eq(chart1.signalSinkCount(), 1UZ));
        expect(eq(chart2.signalSinkCount(), 1UZ));

        // Should be same shared_ptr
        expect(chart1.signalSinks()[0].get() == chart2.signalSinks()[0].get());
    };

    "ChartConfig modification"_test = [] {
        XYChart chart;

        ChartConfig newConfig;
        newConfig.name       = "Modified";
        newConfig.xAxisMode  = XAxisMode::UtcTime;
        newConfig.showLegend = false;

        chart.setConfig(newConfig);

        expect(eq(chart.config().name, std::string("Modified")));
        expect(chart.config().xAxisMode == XAxisMode::UtcTime);
        expect(!chart.config().showLegend);
    };

    "ChartTypeRegistry registration and creation"_test = [] {
        auto& registry = ChartTypeRegistry::instance();

        // XYChart should be registered via REGISTER_CHART_TYPE macro
        expect(registry.hasType("XYChart"));

        auto types = registry.registeredTypes();
        expect(std::find(types.begin(), types.end(), "XYChart") != types.end());

        // Create chart via factory
        ChartConfig config;
        config.name = "FactoryCreated";

        auto chart = registry.create("XYChart", config);
        expect(chart != nullptr);
        expect(eq(chart->chartTypeName(), std::string_view("XYChart")));
        expect(eq(chart->config().name, std::string("FactoryCreated")));
    };

    "ChartManager registration"_test = [] {
        auto& mgr = ChartManager::instance();

        std::size_t initialCount = mgr.chartCount();

        auto        chart = std::make_shared<XYChart>();
        std::string chartId(chart->uniqueId());

        mgr.registerChart(chart);
        expect(eq(mgr.chartCount(), initialCount + 1));

        auto* found = mgr.findChart(chartId);
        expect(found != nullptr);

        mgr.unregisterChart(chartId);
        expect(eq(mgr.chartCount(), initialCount));
    };

    "ChartManager listener"_test = [] {
        auto& mgr = ChartManager::instance();

        bool addedCalled   = false;
        bool removedCalled = false;

        void* owner = reinterpret_cast<void*>(456);
        mgr.addListener(owner, [&](Chart& chart, bool isAdded) {
            (void)chart;
            if (isAdded) {
                addedCalled = true;
            } else {
                removedCalled = true;
            }
        });

        auto        chart = std::make_shared<XYChart>();
        std::string chartId(chart->uniqueId());

        mgr.registerChart(chart);
        expect(addedCalled);

        mgr.unregisterChart(chartId);
        expect(removedCalled);

        mgr.removeListener(owner);
    };

    "Helper functions"_test = [] {
        auto streamingSink = makeStreamingSink<float>("helper_streaming");
        expect(streamingSink != nullptr);
        expect(eq(streamingSink->uniqueName(), std::string_view("helper_streaming")));

        auto datasetSink = makeDataSetSink<float>("helper_dataset");
        expect(datasetSink != nullptr);
        expect(eq(datasetSink->uniqueName(), std::string_view("helper_dataset")));

        auto xyChart = makeXYChart();
        expect(xyChart != nullptr);
        expect(eq(xyChart->chartTypeName(), std::string_view("XYChart")));

        auto chart = makeChart("XYChart");
        expect(chart != nullptr);
    };

    "Signal shared between multiple charts"_test = [] {
        // Create a signal sink
        auto sink = makeStreamingSink<float>("shared_signal");

        // Add some data using new API
        for (int i = 0; i < 100; ++i) {
            sink->pushSample(static_cast<double>(i) * 0.01, std::sin(static_cast<float>(i) * 0.1f));
        }

        // Create two charts sharing the same sink
        auto chart1 = makeXYChart();
        auto chart2 = makeXYChart();

        chart1->addSignalSink(sink);
        chart2->addSignalSink(sink);

        // Both charts should see the same data
        expect(eq(chart1->signalSinks()[0]->size(), chart2->signalSinks()[0]->size()));

        // Register different capacity requirements (must be above minimum capacity of 2048)
        sink->requestCapacity(chart1.get(), 3000);
        sink->requestCapacity(chart2.get(), 5000);

        // Buffer should accommodate the larger requirement
        expect(eq(sink->bufferCapacity(), 5000UZ));

        // When chart2 releases, buffer should shrink
        sink->releaseCapacity(chart2.get());
        expect(eq(sink->bufferCapacity(), 3000UZ));
    };

    "PlotData can be used for rendering"_test = [] {
        auto sink = makeStreamingSink<float>("plot_data_test");

        // Add test data
        for (int i = 0; i < 50; ++i) {
            sink->pushSample(static_cast<double>(i), static_cast<float>(i * 2));
        }

        auto pd = sink->plotData();
        expect(!pd.empty());
        expect(eq(pd.count, 50));

        // Verify getter works
        auto p0 = pd.getter(0, pd.userData);
        expect(std::abs(p0.x - 0.0) < 1e-9);
        expect(std::abs(p0.y - 0.0) < 1e-9);

        auto p25 = pd.getter(25, pd.userData);
        expect(std::abs(p25.x - 25.0) < 1e-9);
        expect(std::abs(p25.y - 50.0) < 1e-9);
    };

    return 0;
}
