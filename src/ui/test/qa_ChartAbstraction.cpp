#include "DashboardBlocks.hpp"
#include "TestSinks.hpp"
#include "blocks/Arithmetic.hpp"
#include "blocks/ImPlotSink.hpp"
#include "blocks/TagSink.hpp"
#include "blocks/ToUncertainValue.hpp"
#include "charts/ImageChart.hpp"
#include "charts/RDSView.hpp"
#include "charts/SpectrumHelper.hpp"

#include <boost/ut.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>
#include <gnuradio-4.0/testing/TensorSource.hpp>
#if __has_include(<gnuradio-4.0/localisation/PairCrossSpectra.hpp>)
#include <gnuradio-4.0/localisation/PairCrossSpectra.hpp>
#endif
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>

using namespace gr::od;

struct FixedUncertainty : gr::Block<FixedUncertainty> {
    gr::PortIn<float>                      in;
    gr::PortOut<gr::UncertainValue<float>> out;
    GR_MAKE_REFLECTABLE(FixedUncertainty, in, out);

    [[nodiscard]] constexpr gr::UncertainValue<float> processOne(float value) const noexcept { return {value, 0.5f}; }
};

struct UncertainDataSetSource : gr::Block<UncertainDataSetSource> {
    gr::PortOut<gr::DataSet<gr::UncertainValue<float>>> out;
    GR_MAKE_REFLECTABLE(UncertainDataSetSource, out);

    bool _sent = false;

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        if (_sent) {
            output.publish(0UZ);
            return gr::work::Status::DONE;
        }
        gr::DataSet<gr::UncertainValue<float>> dataSet;
        dataSet.signal_names  = {"current"};
        dataSet.axis_values   = {{{0.f, 0.01f}, {1.f, 0.02f}}};
        dataSet.extents       = {2};
        dataSet.signal_values = {{1.f, 0.1f}, {2.f, 0.2f}};
        output[0UZ]           = std::move(dataSet);
        output.publish(1UZ);
        _sent = true;
        return gr::work::Status::OK;
    }
};

struct ToUncertainComplex : gr::Block<ToUncertainComplex> {
    gr::PortIn<std::complex<float>>                      in;
    gr::PortOut<gr::UncertainValue<std::complex<float>>> out;
    GR_MAKE_REFLECTABLE(ToUncertainComplex, in, out);

    [[nodiscard]] constexpr gr::UncertainValue<std::complex<float>> processOne(std::complex<float> z) const noexcept { return {z, {0.1f, 0.2f}}; }
};

struct ComplexDataSetSource : gr::Block<ComplexDataSetSource> {
    gr::PortOut<gr::DataSet<std::complex<float>>> out;
    GR_MAKE_REFLECTABLE(ComplexDataSetSource, out);

    bool _sent = false;

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        if (_sent) {
            output.publish(0UZ);
            return gr::work::Status::DONE;
        }
        gr::DataSet<std::complex<float>> dataSet;
        dataSet.signal_names      = {"iq"};
        dataSet.signal_quantities = {"voltage"};
        dataSet.signal_units      = {"V"};
        dataSet.axis_values       = {{{0.f, 0.f}, {1.f, 0.f}}};
        dataSet.extents           = {2};
        dataSet.signal_values     = {{1.f, 2.f}, {3.f, 4.f}};
        output[0UZ]               = std::move(dataSet);
        output.publish(1UZ);
        _sent = true;
        return gr::work::Status::OK;
    }
};

struct Int16DataSetSource : gr::Block<Int16DataSetSource> {
    gr::PortOut<gr::DataSet<std::int16_t>> out;
    GR_MAKE_REFLECTABLE(Int16DataSetSource, out);

    bool _sent = false;

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        if (_sent) {
            output.publish(0UZ);
            return gr::work::Status::DONE;
        }
        gr::DataSet<std::int16_t> dataSet;
        dataSet.signal_names  = {"counts"};
        dataSet.axis_values   = {{0, 1}};
        dataSet.extents       = {2};
        dataSet.signal_values = {-32767, 32767};
        output[0UZ]           = std::move(dataSet);
        output.publish(1UZ);
        _sent = true;
        return gr::work::Status::OK;
    }
};

int main() {
    using namespace boost::ut;
    using namespace charts;
    using namespace test;

    "every chart mode survives the draw config, an absent mode is View"_test = [] {
        for (const ChartMode mode : {ChartMode::View, ChartMode::Interaction, ChartMode::Layout}) {
            expect(chartModeFrom(chartDrawConfig(mode)) == mode) << magic_enum::enum_name(mode);
        }
        expect(chartModeFrom(gr::property_map{}) == ChartMode::View);
        expect(chartModeFrom(gr::property_map{{"chartMode", std::string("Interaction")}}) == ChartMode::Interaction) << "the dashboard writes the enumerator name";
    };

    "SpectrumPlot requests small default dataset capacity"_test = [] {
        ImPlotSink<gr::DataSet<float>> sink({});
        auto                           adapter = sink.signalSink();
        SpectrumPlot                   chart;
        chart.addSignalSink(adapter);
        expect(eq(sink.signalSink()->bufferCapacity(), 8UZ));
    };

    "XYChart requests dataset capacity from max_history_count"_test = [] {
        ImPlotSink<gr::DataSet<float>> sink({});
        auto                           adapter = sink.signalSink();
        XYChart                        chart;
        chart.n_history         = 30'000U; // should be ignored for DataSets
        chart.max_history_count = 16U;
        chart.addSignalSink(adapter);
        expect(eq(sink.signalSink()->bufferCapacity(), 16UZ));
    };

    // ground truth, arithmetic: two seconds are 96000 samples at 48 kHz and 192000 at 96 kHz, and the sink is asked to hold that many
    "XYChart holds a history stated in seconds at the rate its signal arrives at"_test = [] {
        for (const auto& [rate, samples] : {std::pair{48000.0f, 96'000UZ}, std::pair{96000.0f, 192'000UZ}}) {
            gr::Graph graph;
            auto&     sink    = graph.emplaceBlock<ImPlotSink<float>>({{"sample_rate", rate}}); // as the stream's tags set it
            auto      adapter = sink.signalSink();
            XYChart   chart;
            chart.history_seconds = 2.0f;
            chart.addSignalSink(adapter);
            chart.followHistorySeconds();
            expect(eq(static_cast<std::size_t>(chart.n_history.value), samples)) << "at " << rate << " Hz";
            expect(eq(sink.signalSink()->bufferCapacity(), samples)) << "the sink holds it";
        }
    };

    // ground truth by hand: 1000 points at x = index, all zero but 5 at x 150 and -3 at x 160, reduced to 10 columns of 100.
    // The second column keeps (150, 5) then (160, -3), in that order; every column gives two points.
    "a trace longer than its plot is wide keeps each pixel column's extremes, in order"_test = [] {
        std::vector<double> x(1000UZ);
        std::vector<double> y(1000UZ, 0.0);
        std::iota(x.begin(), x.end(), 0.0);
        y[150UZ] = 5.0;
        y[160UZ] = -3.0;
        axis::reduceToColumns(x, y, 10UZ);
        expect(fatal(eq(x.size(), 20UZ))) << "two points a column";
        expect(eq(x[2UZ], 150.0) && eq(y[2UZ], 5.0)) << "the second column's maximum first, where it came";
        expect(eq(x[3UZ], 160.0) && eq(y[3UZ], -3.0)) << "then its minimum";
        expect(eq(std::ranges::max(y), 5.0) && eq(std::ranges::min(y), -3.0)) << "the envelope of every point";

        std::vector<double> shortX{0.0, 1.0, 2.0};
        std::vector<double> shortY{1.0, -1.0, 1.0};
        axis::reduceToColumns(shortX, shortY, 10UZ);
        expect(eq(shortX.size(), 3UZ)) << "a trace no longer than two points a column is left as it is";
    };

    "XYChart keeps n_history when no history in seconds is stated"_test = [] {
        ImPlotSink<float> sink({});
        sink.sample_rate = 48000.0f;
        auto    adapter  = sink.signalSink();
        XYChart chart;
        chart.n_history = 5000U;
        chart.addSignalSink(adapter);
        chart.followHistorySeconds();
        expect(eq(chart.n_history.value, gr::Size_t{5000U}));
    };

    "DataSet keeps custom axis labels"_test = [] {
        gr::Graph graph;
        auto&     sink = graph.emplaceBlock<ImPlotSink<gr::DataSet<float>>>({{"signal_quantity", "amplitude"}, {"signal_unit", "dB"}});

        gr::DataSet<float> ds;
        ds.axis_names        = {"Frequency"};
        ds.axis_units        = {"Hz"};
        ds.signal_quantities = {"Magnitude(FFT)"};
        ds.signal_units      = {"V/√Hz"};
        sink.updateAxisMetadataFromDataSet(ds);

        expect(eq(sink.signalSink()->abscissaQuantity(), std::string_view{"Frequency"}));
        expect(eq(sink.signalSink()->abscissaUnit(), std::string_view{"Hz"}));
        expect(eq(sink.signalSink()->signalQuantity(), std::string_view{"amplitude"}));
        expect(eq(sink.signalSink()->signalUnit(), std::string_view{"dB"}));

        ds.axis_names = {"Time"};
        ds.axis_units = {"s"};
        sink.updateAxisMetadataFromDataSet(ds);

        expect(eq(sink.signalSink()->abscissaQuantity(), std::string_view{"Time"}));
        expect(eq(sink.signalSink()->abscissaUnit(), std::string_view{"s"}));
        expect(eq(sink.signalSink()->signalUnit(), std::string_view{"dB"}));
    };

    "DataSet supplies default axis labels"_test = [] {
        gr::Graph graph;
        auto&     sink = graph.emplaceBlock<ImPlotSink<gr::DataSet<float>>>();

        gr::DataSet<float> ds;
        ds.axis_names        = {"Frequency"};
        ds.axis_units        = {"Hz"};
        ds.signal_quantities = {"Amplitude"};
        ds.signal_units      = {"V"};
        sink.updateAxisMetadataFromDataSet(ds);

        expect(eq(sink.signalSink()->abscissaQuantity(), std::string_view{"Frequency"}));
        expect(eq(sink.signalSink()->abscissaUnit(), std::string_view{"Hz"}));
        expect(eq(sink.signalSink()->signalQuantity(), std::string_view{"Amplitude"}));
        expect(eq(sink.signalSink()->signalUnit(), std::string_view{"V"}));
    };

    "trigger tags do not reset the DataSet count"_test = [] {
        constexpr gr::Size_t kFftSize = 64U;
        constexpr gr::Size_t kSpectra = 10U;

        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", kFftSize * kSpectra}, {"mark_tag", false}});
        for (gr::Size_t i = 0U; i < kSpectra; ++i) {
            gr::property_map trigger;
            gr::tag::put(trigger, gr::tag::kTriggerName, std::string("CMD_DIAG_TRIGGER1"));
            gr::tag::put(trigger, gr::tag::kTriggerTime, std::uint64_t{1'000'000'000U} * (i + 1U));
            source._tags.push_back({i * kFftSize, std::move(trigger)});
        }
        auto& fft  = graph.emplaceBlock<gr::blocks::fft::FFT<float, gr::DataSet<float>>>({{"fft_size", kFftSize}});
        auto& sink = graph.emplaceBlock<ImPlotSink<gr::DataSet<float>>>();
        expect(graph.connect<"out", "in">(source, fft).has_value());
        expect(graph.connect<"out", "in">(fft, sink).has_value());

        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());
        expect(eq(sink.signalSink()->totalSampleCount(), std::size_t{kSpectra}));
    };

    "trigger tags do not reset the streaming count"_test = [] {
        constexpr gr::Size_t kSamples = 1000U;

        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", kSamples}, {"mark_tag", false}});
        for (gr::Size_t i = 0U; i < 5U; ++i) {
            gr::property_map trigger;
            gr::tag::put(trigger, gr::tag::kTriggerName, std::string("CMD_DIAG_TRIGGER1"));
            gr::tag::put(trigger, gr::tag::kTriggerTime, std::uint64_t{1'000'000'000U} * (i + 1U));
            source._tags.push_back({i * 200U, std::move(trigger)});
        }
        auto& sink = graph.emplaceBlock<ImPlotSink<float>>();
        expect(graph.connect<"out", "in">(source, sink).has_value());

        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());
        expect(eq(sink.signalSink()->totalSampleCount(), std::size_t{kSamples}));
    };

    // ground truth by construction: the source counts 0, 1, 2, …; 5000 of them into a sink of 8192, then a chart asks 3000
    "a chart's request shrinks the sink to its newest samples and leaves required_size alone"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", gr::Size_t{5000U}}, {"mark_tag", false}});
        auto&     sink   = graph.emplaceBlock<ImPlotSink<float>>({{"required_size", gr::Size_t{8192U}}});
        expect(graph.connect<"out", "in">(source, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());
        expect(eq(sink.signalSink()->bufferCapacity(), 8192UZ));
        expect(eq(sink.signalSink()->size(), 5000UZ) >> fatal);

        const auto now = CapacityRequests::clock::now();
        sink.signalSink()->requestCapacity("chart", 3000UZ, std::chrono::seconds{60}, now);
        expect(eq(sink.signalSink()->bufferCapacity(), 3000UZ)) << "requests decide once a chart asks";
        expect(eq(sink.signalSink()->size(), 3000UZ) >> fatal);
        expect(eq(sink.signalSink()->samples().yAt(0UZ), 2000.0)) << "the oldest kept";
        expect(eq(sink.signalSink()->samples().yAt(2999UZ), 4999.0)) << "the newest";
        expect(eq(sink.required_size.value, gr::Size_t{8192U})) << "a saved dashboard keeps what its author set";

        sink.signalSink()->expireCapacityRequests(now + std::chrono::seconds{61});
        expect(eq(sink.signalSink()->bufferCapacity(), 8192UZ)) << "no chart asking: required_size again";
        expect(eq(sink.signalSink()->size(), 3000UZ)) << "growing keeps what is held";
    };

    // ground truth, IEEE 754: float holds every 16-bit integer exactly and double every integer up to 2^53;
    // 2^53 + 1 lies halfway between 2^53 and 2^53 + 2 and rounds to the even one, 2^53
    "an integer stream is kept exactly, as float up to 16 bits and as double above"_test = [] {
        const auto received = []<typename T>(std::vector<T> values) {
            gr::Graph graph;
            auto&     source = graph.emplaceBlock<gr::testing::TagSource<T>>({{"n_samples_max", static_cast<gr::Size_t>(values.size())}, {"mark_tag", false}});
            source.values    = gr::Tensor<T>(gr::data_from, values);
            auto& sink       = graph.emplaceBlock<ImPlotSink<T>>();
            expect(graph.connect<"out", "in">(source, sink).has_value());
            gr::scheduler::Simple<> scheduler;
            expect(scheduler.exchange(std::move(graph)).has_value());
            expect(scheduler.runAndWait().has_value());
            return sink.signalSink(); // the store outlives its block
        };
        const auto asFloat = [](const SampleWindow& window) { return std::holds_alternative<std::span<const float>>(window.y); };

        const auto         int16Sink = received(std::vector<std::int16_t>{-32767, 32767}); // a window views the sink's storage
        const SampleWindow int16     = int16Sink->samples();
        expect(asFloat(int16)) << "16 bits: float";
        expect(eq(int16.yAt(0UZ), -32767.0) && eq(int16.yAt(1UZ), 32767.0));

        const auto         int32Sink = received(std::vector<std::int32_t>{std::numeric_limits<std::int32_t>::max()});
        const SampleWindow int32     = int32Sink->samples();
        expect(!asFloat(int32)) << "32 bits: double";
        expect(eq(int32.yAt(0UZ), 2147483647.0));

        const auto         int64Sink = received(std::vector<std::int64_t>{std::int64_t{1} << 53});
        const SampleWindow int64     = int64Sink->samples();
        expect(eq(int64.yAt(0UZ), 9007199254740992.0)) << "2^53 exactly";

        const auto         uint64Sink = received(std::vector<std::uint64_t>{(std::uint64_t{1} << 53) + 1U});
        const SampleWindow uint64     = uint64Sink->samples();
        expect(eq(uint64.yAt(0UZ), 9007199254740992.0)) << "2^53 + 1 rounds to even, 2^53";
    };

    // ground truth by construction: means 1 and 2 with standard deviations 0.1 and 0.2, paired in order
    "ToUncertainValue pairs each value with its standard deviation, which a sink keeps for the charts"_test = [] {
        gr::Graph graph;
        auto&     means   = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", gr::Size_t{2U}}, {"mark_tag", false}});
        means.values      = gr::Tensor<float>(gr::data_from, std::vector{1.f, 2.f});
        auto& deviations  = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", gr::Size_t{2U}}, {"mark_tag", false}});
        deviations.values = gr::Tensor<float>(gr::data_from, std::vector{0.1f, 0.2f});
        auto& pair        = graph.emplaceBlock<ToUncertainValue<float>>();
        auto& sink        = graph.emplaceBlock<ImPlotSink<gr::UncertainValue<float>>>({{"name", "pairedSink"}});
        expect(graph.connect<"out", "value">(means, pair).has_value());
        expect(graph.connect<"out", "uncertainty">(deviations, pair).has_value());
        expect(graph.connect<"out", "in">(pair, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());
        const SampleWindow window = sink.signalSink()->samples();
        expect(eq(window.size(), 2UZ) >> fatal);
        expect(eq(window.yAt(1UZ), 2.0) && approx(window.yErrorAt(0UZ), 0.1, 1e-6) && approx(window.yErrorAt(1UZ), 0.2, 1e-6));
    };

    "a DataSet of UncertainValues keeps its values and their standard deviations, signal and axis"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<UncertainDataSetSource>();
        auto&     sink   = graph.emplaceBlock<ImPlotSink<gr::DataSet<gr::UncertainValue<float>>>>();
        expect(graph.connect<"out", "in">(source, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        const auto& signal = *sink.signalSink();
        expect(signal.hasErrors());
        expect(eq(signal.dataSets().size(), 1UZ) && eq(signal.dataSetErrors().size(), 1UZ) >> fatal);
        const gr::DataSet<float>& values = signal.dataSets().back();
        const DataSetErrors&      errors = signal.dataSetErrors().back();
        expect(eq(values.signal_values[0], 1.f) && eq(values.signal_values[1], 2.f));
        expect(eq(errors.signals.size(), 2UZ) && eq(errors.signals[0], 0.1f) && eq(errors.signals[1], 0.2f));
        expect(eq(values.axis_values[0][1], 1.f));
        expect(eq(errors.axes.size(), 1UZ) >> fatal);
        expect(eq(errors.axes[0][0], 0.01f) && eq(errors.axes[0][1], 0.02f)) << "the x errors";
    };

    // ground truth by construction: the same uint32 frame, 0xFF0000FF (opaque red) and 1000, read as colour and as values
    "a uint32 tensor sink keeps packed colour, or integer values when packed_colour is false"_test = [] {
        const auto received = [](bool packedColour) {
            gr::Graph graph;
            auto&     source = graph.emplaceBlock<gr::testing::TensorSource<std::uint32_t>>();
            source._frames.push_back({.extents = {1UZ, 2UZ}, .elements = {0xFF0000FFU, 1000U}});
            auto& sink = graph.emplaceBlock<ImPlotSink<gr::Tensor<std::uint32_t>>>({{"packed_colour", packedColour}});
            expect(graph.connect<"out", "in">(source, sink).has_value());
            gr::scheduler::Simple<> scheduler;
            expect(scheduler.exchange(std::move(graph)).has_value());
            expect(scheduler.runAndWait().has_value());
            return sink.signalSink();
        };
        const auto      colourSink = received(true);
        const FrameView colour     = colourSink->frame();
        const auto*     pixels     = std::get_if<std::span<const std::uint32_t>>(&colour.values);
        expect(fatal(pixels != nullptr)) << "packed colour by default";
        expect(eq((*pixels)[0], 0xFF0000FFU));
        const auto      valueSink = received(false);
        const FrameView values    = valueSink->frame();
        expect(eq(values.row(0UZ).size(), 2UZ) >> fatal) << "float values";
        expect(eq(values.row(0UZ)[1], 1000.f));
    };

    // ground truth by construction: 1 + 2i and 3 + 4i, σ 0.1 + 0.2i
    "a complex stream is two signals, its real part under the block's name and its imaginary part beside it"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<gr::testing::TagSource<std::complex<float>>>({{"n_samples_max", gr::Size_t{2U}}, {"mark_tag", false}});
        source.values    = gr::Tensor<std::complex<float>>(gr::data_from, std::vector<std::complex<float>>{{1.f, 2.f}, {3.f, 4.f}});
        auto& convert    = graph.emplaceBlock<ToUncertainComplex>();
        auto& plain      = graph.emplaceBlock<ImPlotSink<std::complex<float>>>({{"name", "iqSink"}, {"signal_name", "iq"}});
        auto& uncertain  = graph.emplaceBlock<ImPlotSink<gr::UncertainValue<std::complex<float>>>>({{"name", "uncertainIqSink"}}); // a sink registers with its first settings, as a dashboard's do
        expect(graph.connect<"out", "in">(source, plain).has_value());
        expect(graph.connect<"out", "in">(source, convert).has_value());
        expect(graph.connect<"out", "in">(convert, uncertain).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        const auto real      = plain.signalSink();
        const auto imaginary = SinkRegistry::instance().getSink(std::format("{}{}", plain.unique_name, kImaginaryPartSuffix));
        expect(fatal(imaginary != nullptr)) << "the imaginary part registered beside the block";
        expect(eq(real->signalName(), std::string_view("Re(iq)")) && eq(imaginary->signalName(), std::string_view("Im(iq)")));
        expect(eq(imaginary->name(), std::string_view("iqSink.im")));
        expect(real->color() != imaginary->color()) << "two traces, two colours";
        const SampleWindow re = real->samples();
        const SampleWindow im = imaginary->samples();
        expect(eq(re.size(), 2UZ) && eq(im.size(), 2UZ) >> fatal);
        expect(eq(re.yAt(0UZ), 1.0) && eq(re.yAt(1UZ), 3.0) && eq(im.yAt(0UZ), 2.0) && eq(im.yAt(1UZ), 4.0));
        expect(eq(re.x[1], im.x[1])) << "dated alike";

        const auto uncertainImaginary = SinkRegistry::instance().getSink(std::format("{}{}", uncertain.unique_name, kImaginaryPartSuffix));
        expect(fatal(uncertainImaginary != nullptr));
        expect(approx(uncertain.signalSink()->samples().yErrorAt(0UZ), 0.1, 1e-6) && approx(uncertainImaginary->samples().yErrorAt(0UZ), 0.2, 1e-6)) << "σ per component";
    };

    "a complex DataSet arrives as two real rows per signal, Re and Im, selected by dataset_index"_test = [] {
        gr::Graph graph;
        auto&     source = graph.emplaceBlock<ComplexDataSetSource>();
        auto&     sink   = graph.emplaceBlock<ImPlotSink<gr::DataSet<std::complex<float>>>>({{"dataset_index", gr::Size_t{1U}}});
        expect(graph.connect<"out", "in">(source, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        const auto& signal = *sink.signalSink();
        expect(eq(signal.dataSets().size(), 1UZ) >> fatal);
        const gr::DataSet<float>& dataSet = signal.dataSets().back();
        expect(eq(dataSet.signal_names.size(), 2UZ) >> fatal);
        expect(eq(std::string_view(dataSet.signal_names[0]), std::string_view("Re(iq)")) && eq(std::string_view(dataSet.signal_names[1]), std::string_view("Im(iq)")));
        expect(eq(std::string_view(dataSet.signal_units[1]), std::string_view("V"))) << "Re and Im keep the unit";
        expect(dataSet.signal_values == std::pmr::vector<float>{1.f, 3.f, 2.f, 4.f});
        expect(eq(dataSet.axis_values[0][1], 1.f));
        const auto row = dataSet.signalValues(signal.datasetRow(dataSet));
        expect(eq(row[0], 2.f) && eq(row[1], 4.f)) << "dataset_index 1: the Im row";
    };

    "an integer DataSet and an 8-bit image arrive as float"_test = [] {
        gr::Graph graph;
        auto&     dataSetSource = graph.emplaceBlock<Int16DataSetSource>();
        auto&     dataSetSink   = graph.emplaceBlock<ImPlotSink<gr::DataSet<std::int16_t>>>();
        auto&     imageSource   = graph.emplaceBlock<gr::testing::TensorSource<std::uint8_t>>();
        imageSource._frames.push_back({.extents = {2UZ, 2UZ}, .elements = {0U, 1U, 254U, 255U}});
        auto& imageSink = graph.emplaceBlock<ImPlotSink<gr::Tensor<std::uint8_t>>>();
        expect(graph.connect<"out", "in">(dataSetSource, dataSetSink).has_value());
        expect(graph.connect<"out", "in">(imageSource, imageSink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        const auto dataSets = dataSetSink.signalSink()->dataSets();
        expect(eq(dataSets.size(), 1UZ) >> fatal);
        expect(eq(dataSets.back().signal_values[0], -32767.f) && eq(dataSets.back().signal_values[1], 32767.f));
        expect(eq(std::string_view(dataSets.back().signal_names[0]), std::string_view("counts")));
        const FrameView image = imageSink.signalSink()->frame();
        expect(eq(image.rows, 2UZ) && eq(image.columns, 2UZ) >> fatal);
        expect(eq(image.row(1UZ)[1], 255.f)) << "an 8-bit pixel as its value";
    };

    "an UncertainValue sink keeps the values"_test = [] {
        gr::Graph graph;
        auto&     source  = graph.emplaceBlock<gr::testing::TagSource<float>>({{"n_samples_max", gr::Size_t{100U}}, {"mark_tag", false}});
        auto&     convert = graph.emplaceBlock<FixedUncertainty>();
        auto&     sink    = graph.emplaceBlock<ImPlotSink<gr::UncertainValue<float>>>();
        expect(graph.connect<"out", "in">(source, convert).has_value());
        expect(graph.connect<"out", "in">(convert, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        expect(sink.signalSink()->signalKind() == SignalKind::Streaming);
        expect(sink.signalSink()->hasErrors());
        expect(eq(sink.signalSink()->size(), 100UZ) >> fatal);
        const SampleWindow window = sink.signalSink()->samples();
        expect(eq(window.yAt(0UZ), 0.0));
        expect(eq(window.yAt(99UZ), 99.0));
        expect(window.x[99UZ] > window.x[0UZ]) << "dated like any streaming sample";
        expect(window.hasErrors() >> fatal) << "and its uncertainty kept";
        expect(eq(window.yErrorAt(0UZ), 0.5) && eq(window.yErrorAt(99UZ), 0.5));
    };

    "a DataSet<double> sink converts each DataSet once, metadata included"_test = [] {
        constexpr gr::Size_t kFftSize = 64U;
        gr::Graph            graph;
        auto&                source = graph.emplaceBlock<gr::testing::TagSource<double>>({{"n_samples_max", kFftSize * 3U}, {"mark_tag", false}});
        auto&                fft    = graph.emplaceBlock<gr::blocks::fft::FFT<double, gr::DataSet<double>>>({{"fft_size", kFftSize}});
        auto&                sink   = graph.emplaceBlock<ImPlotSink<gr::DataSet<double>>>();
        expect(graph.connect<"out", "in">(source, fft).has_value());
        expect(graph.connect<"out", "in">(fft, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        const auto held = sink.signalSink()->dataSets();
        expect(eq(held.size(), 3UZ) >> fatal);
        expect(held.data() == sink.signalSink()->dataSets().data()) << "a read hands out the stored DataSets";
        const gr::DataSet<float>& newest = held.back();
        expect(!newest.signal_names.empty() >> fatal);
        expect(eq(newest.signal_quantities.size(), newest.signal_names.size())) << "quantities survive the conversion";
        expect(eq(newest.axis_values.size(), 1UZ) >> fatal);
        expect(eq(newest.signal_values.size(), newest.signal_names.size() * newest.axis_values[0].size()));
    };

    "DataSet drag keeps signal"_test = [] {
        gr::Graph graph;
        auto&     linearBlock = graph.emplaceBlock<ImPlotSink<gr::DataSet<float>>>({{"name", "linearSink"}, {"signal_name", "same signal"}});
        auto&     dbBlock     = graph.emplaceBlock<ImPlotSink<gr::DataSet<float>>>({{"name", "dbSink"}, {"signal_name", "same signal"}});
        auto      linearSink  = SinkRegistry::instance().getSink(linearBlock.unique_name);
        auto      dbSink      = SinkRegistry::instance().getSink(dbBlock.unique_name);
        expect(linearSink != nullptr);
        expect(dbSink != nullptr);
        if (!linearSink || !dbSink) {
            return;
        }

        auto& chart = graph.emplaceBlock<XYChart>({{"chart_name", "drag test"}});
        expect(chart.onSinkAddedFromDnd(dnd::makePayload(*linearSink)));
        expect(chart.onSinkAddedFromDnd(dnd::makePayload(*dbSink)));
        chart.data_sinks.value = {"pendingSink", "same signal", "dbSink"};

        chart.onSinkRemovedFromDnd(dbSink->uniqueName());
        std::ignore = chart.settings().applyStagedParameters();
        chart.syncSinksIfNeeded(chart.data_sinks.value);
        expect(chart.signalSinks() == std::vector{linearSink});
        expect(chart.data_sinks.value == std::vector<std::string>{"pendingSink", "linearSink"});
        chart.clearSignalSinks();
        chart.syncSinksIfNeeded(chart.data_sinks.value);
        expect(chart.signalSinks() == std::vector{linearSink});

        expect(chart.onSinkAddedFromDnd(dnd::makePayload(*dbSink)));
        std::ignore = chart.settings().applyStagedParameters();
        expect(chart.signalSinks() == std::vector{linearSink, dbSink});
        expect(chart.data_sinks.value == std::vector<std::string>{"pendingSink", "linearSink", "dbSink"});

        std::ignore = dbBlock.settings().set({{"signal_name", std::string("dB")}});
        std::ignore = dbBlock.settings().activateContext();
        std::ignore = dbBlock.settings().applyStagedParameters();
        expect(eq(dbSink->signalName(), std::string_view("dB"))) << "the sink follows its block's settings";
        chart.data_sinks.value.push_back("dB");
        chart.onSinkRemovedFromDnd(dbSink->uniqueName());
        std::ignore = chart.settings().applyStagedParameters();
        chart.syncSinksIfNeeded(chart.data_sinks.value);
        expect(chart.signalSinks() == std::vector{linearSink});
        expect(chart.data_sinks.value == std::vector<std::string>{"pendingSink", "linearSink"});
    };

    "Legend keeps duplicate names separate"_test = [] {
        auto first                  = makeTestStreamingSink("first");
        auto second                 = makeTestStreamingSink("second");
        first->metadata.signalName  = "same signal";
        second->metadata.signalName = "same signal";

        const auto firstLabel  = plotLabel(*first);
        const auto secondLabel = plotLabel(*second);
        const auto firstId     = ImHashStr(firstLabel.c_str());
        expect(neq(firstId, ImHashStr(secondLabel.c_str())));
        expect(eq(std::string_view(firstLabel.c_str(), ImGui::FindRenderedTextEnd(firstLabel.c_str())), std::string_view{"same signal"}));
        expect(eq(std::string_view(secondLabel.c_str(), ImGui::FindRenderedTextEnd(secondLabel.c_str())), std::string_view{"same signal"}));

        first->metadata.signalName = "renamed signal";
        expect(eq(ImHashStr(plotLabel(*first).c_str()), firstId));
    };

    "XYChart creation via makeXYChart"_test = [] {
        auto chart = makeXYChart("TestChart");

        expect(eq(chart->kChartTypeName, std::string_view("XYChart")));
        expect(!chart->unique_name.value().empty());
        expect(chart->unique_name.value().find("XYChart") != std::string_view::npos);
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

        expect(eq(xyChart->kChartTypeName, std::string_view("XYChart")));

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
        expect(eq(xyChart->kChartTypeName, std::string_view("XYChart")));

        auto yyStorage = makeChartByType("YYChart", "TestYY");
        expect(std::holds_alternative<std::shared_ptr<YYChart>>(yyStorage));
        auto& yyChart = std::get<std::shared_ptr<YYChart>>(yyStorage);
        expect(eq(yyChart->kChartTypeName, std::string_view("YYChart")));

        auto unknownStorage = makeChartByType("UnknownChart");
        expect(std::holds_alternative<std::monostate>(unknownStorage));
    };

    "registeredChartTypes queries block registry"_test = [] {
        registerDashboardBlocks(gr::globalBlockRegistry());
        auto types = registeredChartTypes();
        expect(!types.empty()) << fatal;
        expect(std::ranges::any_of(types, [](const std::string& type) { return type.ends_with("XYChart"); })) << "XYChart is registered";

        expect(std::is_sorted(types.begin(), types.end())) << "Types should be sorted";

        for (const auto& type : types) {
            std::string lowerType = type;
            std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), [](unsigned char c) { return std::tolower(c); });
            expect(lowerType.find("chart") != std::string::npos) << "All returned types should contain 'chart'";
        }
    };

    "hold and average include every new spectrum"_test = [] {
        const std::array<std::array<float, 2UZ>, 3UZ> magnitudes{{{1.f, 6.f}, {5.f, 2.f}, {3.f, 4.f}}};
        std::vector<gr::DataSet<float>>               history(magnitudes.size());
        for (std::size_t i = 0UZ; i < history.size(); ++i) {
            history[i].axis_names    = {"frequency"};
            history[i].axis_values   = {{100.f, 200.f}};
            history[i].signal_names  = {"magnitude"};
            history[i].signal_values = {magnitudes[i][0], magnitudes[i][1]};
        }
        SpectrumFrame newest = toSpectrumFrame(history.back()).value();
        newest.history       = history;

        TraceAccumulator all;
        forEachNewSpectrum(newest, 3UZ, [&](const SpectrumFrame& f) { all.update(f.yValues, f.nBins, 0.0, true); });
        expect(std::ranges::equal(all.maxHold(), std::array{5.f, 6.f}));
        expect(std::ranges::equal(all.average(), std::array{3.f, 4.f}));

        TraceAccumulator lastTwo;
        forEachNewSpectrum(newest, 2UZ, [&](const SpectrumFrame& f) { lastTwo.update(f.yValues, f.nBins, 0.0, true); });
        expect(std::ranges::equal(lastTwo.maxHold(), std::array{5.f, 4.f})) << "only the two newest";
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
        expect(eq(xyChart->kChartTypeName, std::string_view("XYChart")));

        auto yyChart = makeYYChart();
        expect(yyChart != nullptr);
        expect(eq(yyChart->kChartTypeName, std::string_view("YYChart")));
    };

    "Minimal scientific values remain normalized without redundant exponents"_test = [] {
        expect(eq(axis::formatMinimalScientific(0.0), std::string{"0"}));
        expect(eq(axis::formatMinimalScientific(1.23), std::string{"1.23"}));
        expect(eq(axis::formatMinimalScientific(9.9999), std::string{"1E1"}));
        expect(eq(axis::formatMinimalScientific(99'400.0), std::string{"9.94E4"}));
        expect(eq(axis::formatMinimalScientific(99'999.0), std::string{"1E5"}));
        expect(eq(axis::formatMinimalScientific(-99'999.0), std::string{"-1E5"}));
        expect(eq(axis::formatMinimalScientific(std::numeric_limits<double>::quiet_NaN()), std::string{"nan"}));
        expect(eq(axis::formatMinimalScientific(std::numeric_limits<double>::infinity()), std::string{"inf"}));
        expect(eq(axis::formatMinimalScientific(-std::numeric_limits<double>::infinity()), std::string{"-inf"}));
    };

    "Scientific axis labels preserve values and units across display ranges"_test = [] {
        const auto formatScientific = [](double value, std::string_view unit) {
            std::array<char, 64UZ> buffer{};
            std::string            unitStorage(unit);
            const int              count = axis::formatScientific(value, buffer.data(), static_cast<int>(buffer.size()), unitStorage.data());
            const std::string      result(buffer.data());
            expect(eq(count, static_cast<int>(result.size())));
            return result;
        };

        expect(eq(formatScientific(0.0, "Hz"), std::string{"0Hz"}));
        expect(eq(formatScientific(1.23, "Hz"), std::string{"1.23Hz"}));
        expect(eq(formatScientific(99'999.0, "Hz"), std::string{"1E5Hz"}));
        expect(eq(formatScientific(0.00099999, "Hz"), std::string{"1E-3Hz"}));
        expect(eq(formatScientific(std::numeric_limits<double>::quiet_NaN(), "Hz"), std::string{"nanHz"}));
    };

    "Metric axis labels select SI prefixes and preserve units"_test = [] {
        const auto formatMetric = [](double value, std::string_view unit) {
            std::array<char, 64UZ> buffer{};
            std::string            unitStorage(unit);
            const int              count = axis::formatMetric(value, buffer.data(), static_cast<int>(buffer.size()), unitStorage.data());
            const std::string      result(buffer.data());
            expect(eq(count, static_cast<int>(result.size())));
            return result;
        };

        expect(eq(formatMetric(0.0, "Hz"), std::string{"0Hz"}));
        expect(eq(formatMetric(1'234.0, "Hz"), std::string{"1.234kHz"}));
        expect(eq(formatMetric(47.0, "Hz"), std::string{"47Hz"}));
        expect(eq(formatMetric(0.0033, "Hz"), std::string{"3.3mHz"}));
        expect(eq(formatMetric(std::numeric_limits<double>::quiet_NaN(), "Hz"), std::string{"nanHz"}));
        expect(eq(formatMetric(std::numeric_limits<double>::infinity(), "Hz"), std::string{"infHz"}));
        expect(eq(formatMetric(-std::numeric_limits<double>::infinity(), "Hz"), std::string{"-infHz"}));
    };

    "Signal shared between multiple charts"_test = [] {
        auto sink = makeTestStreamingSink("shared_signal", 16);

        for (int i = 0; i < 100; ++i) {
            sink->pushSample(static_cast<double>(i) * 0.01, std::sin(static_cast<float>(i) * 0.1f));
        }

        auto chart1 = makeXYChart();
        auto chart2 = makeXYChart();

        chart1->n_history         = 30U;
        chart2->n_history         = 50U;
        chart1->max_history_count = 64U; // above n_history; ignored for streaming samples
        chart2->max_history_count = 128U;
        chart1->addSignalSink(sink);
        expect(eq(sink->bufferCapacity(), 30UZ));
        chart2->addSignalSink(sink);

        expect(eq(chart1->signalSinks()[0]->size(), chart2->signalSinks()[0]->size()));

        expect(eq(sink->bufferCapacity(), 50UZ));
    };

    "YYChart creation"_test = [] {
        auto chart = makeYYChart();

        expect(eq(chart->kChartTypeName, std::string_view("YYChart")));
        expect(!chart->unique_name.value().empty());
        expect(chart->unique_name.value().find("YYChart") != std::string_view::npos);
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

        expect(eq(yyChart->kChartTypeName, std::string_view("YYChart")));

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

    "Axis parsing selects the n-th entry per axis kind"_test = [] {
        expect(!parseAxisConfig({}, AxisKind::X));
        expect(!parseAxisConfig({{"axes", "not a tensor"}}, AxisKind::X));

        gr::Tensor<gr::pmt::Value> axes(gr::extents_from, {7UZ});
        axes[0] = gr::property_map{{"axis", "Y"}, {"min", 1}, {"max", 2.5f}};
        axes[1] = gr::property_map{{"axis", "X"}, {"scale", "tImE"}, {"format", "mEtRiC"}, {"plot_tags", 0}};
        axes[2] = 42;
        axes[3] = gr::property_map{{"axis", 1}};
        axes[4] = gr::property_map{{"scale", "Log10"}};
        axes[5] = gr::property_map{{"axis", "unknown"}};
        axes[6] = gr::property_map{{"axis", "y"}, {"scale", 7}, {"format", 7}, {"width", 80}, {"plot_tags", false}};
        const gr::property_map constraints{{"axes", std::move(axes)}};
        const auto             x  = parseAxisConfig(constraints, AxisKind::X);
        const auto             y  = parseAxisConfig(constraints, AxisKind::Y);
        const auto             y2 = parseAxisConfig(constraints, AxisKind::Y, 1UZ);

        expect(x && y && y2) << fatal;
        expect(x->scale == AxisScale::Time);
        expect(x->format == LabelFormat::Metric);
        expect(x->plotTags);
        expect(eq(y->min, 1.f));
        expect(eq(y->max, 2.5f));
        expect(std::isnan(y->width));
        expect(y->plotTags);
        expect(!y->scale);
        expect(y->format == LabelFormat::Auto);
        expect(std::isnan(y2->min) && std::isnan(y2->max));
        expect(!y2->scale);
        expect(y2->format == LabelFormat::Auto);
        expect(eq(y2->width, 80.f));
        expect(!y2->plotTags);
        expect(!parseAxisConfig(constraints, AxisKind::X, 1UZ));
        expect(!parseAxisConfig(constraints, AxisKind::Y, 2UZ));
    };

    "Axis parsing reads the SymLog linear threshold"_test = [] {
        gr::Tensor<gr::pmt::Value> axes(gr::extents_from, {2UZ});
        axes[0] = gr::property_map{{"axis", "Y"}, {"scale", "SymLog"}, {"linear_threshold", 0.001f}};
        axes[1] = gr::property_map{{"axis", "X"}, {"scale", "SymLog"}};
        const gr::property_map constraints{{"axes", std::move(axes)}};
        const auto             y = parseAxisConfig(constraints, AxisKind::Y);
        const auto             x = parseAxisConfig(constraints, AxisKind::X);
        expect(fatal(y && x));
        expect(eq(y->linearThreshold, 0.001f));
        expect(std::isnan(x->linearThreshold)) << "absent: ImPlot's own SymLog";
    };

    // ground truth, analytic: f(v) = sign(v) log10(1 + |v|/c).
    // At c = 1 mFS, f(±c) = ±log10 2, f(1) = log10 1001, and the decades from c to 1 are 1 m, 10 m, 100 m and 1 on each side of zero.
    "SymLog with a linear threshold: log10(1 + |v|/c), odd, inverted exactly, ticked by decade"_test = [] {
        double c = 1e-3;
        expect(approx(axis::symLogForward(0.0, &c), 0.0, 1e-15));
        expect(approx(axis::symLogForward(c, &c), std::log10(2.0), 1e-12));
        expect(approx(axis::symLogForward(-c, &c), -std::log10(2.0), 1e-12));
        expect(approx(axis::symLogForward(1.0, &c), std::log10(1001.0), 1e-12));
        for (const double v : {-0.9, -2e-3, -1e-4, 0.0, 5e-4, 0.25, 1.0}) {
            expect(approx(axis::symLogInverse(axis::symLogForward(v, &c), &c), v, 1e-12)) << v;
        }
        const std::vector<double> ticks = axis::symLogDecadeTicks(c, 1.0);
        const std::vector<double> decades{-1.0, -0.1, -0.01, -0.001, 0.0, 0.001, 0.01, 0.1, 1.0};
        expect(fatal(eq(ticks.size(), decades.size())));
        for (std::size_t i = 0UZ; i < ticks.size(); ++i) {
            expect(approx(ticks[i], decades[i], 1e-12)) << i;
        }
        expect(axis::symLogDecadeTicks(c, 0.5 * c) == std::vector<double>{0.0}) << "range inside the linear band";
        expect(axis::symLogDecadeTicks(c, 0.0) == std::vector<double>{0.0}) << "empty range";
    };

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

        auto sinkX                     = makeTestStreamingSink("x_signal");
        sinkX->metadata.signalQuantity = "time";
        sinkX->metadata.signalUnit     = "s";

        // sink[1] and sink[2] share quantity+unit -> same Y-axis group
        auto sinkY1                     = makeTestStreamingSink("y_voltage1");
        sinkY1->metadata.signalQuantity = "voltage";
        sinkY1->metadata.signalUnit     = "V";

        auto sinkY2                     = makeTestStreamingSink("y_voltage2");
        sinkY2->metadata.signalQuantity = "voltage";
        sinkY2->metadata.signalUnit     = "V";

        // sink[3] has a different quantity -> separate Y-axis group
        auto sinkY3                     = makeTestStreamingSink("y_current");
        sinkY3->metadata.signalQuantity = "current";
        sinkY3->metadata.signalUnit     = "A";

        chart->addSignalSink(sinkX);
        chart->addSignalSink(sinkY1);
        chart->addSignalSink(sinkY2);
        chart->addSignalSink(sinkY3);

        for (auto& s : chart->signalSinks()) {
            s->pushSample(0.0, 1.0f);
        }

        // buildMultiYCategories is private; axis::findOrCreateCategory is the same mechanism
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
            auto sink                     = makeTestStreamingSink("y_" + std::to_string(i));
            sink->metadata.signalQuantity = units[i].first;
            sink->metadata.signalUnit     = units[i].second;
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

    // ground truth, analytic: a cell hit by n spectra with decay τ holds Σ_{k<n} (1 − 1/τ)^k = τ (1 − (1 − 1/τ)^n);
    // τ = 4, n = 6: 4 (1 − 0.75^6) = 3.2880859375.
    // Rows of 1 unit from yMax = 10: value 5 is row 5
    "a constant spectrum fills one cell per column to tau (1 - (1 - 1/tau)^n), and the peak with it"_test = [] {
        DensityGrid grid;
        grid.resize(4UZ, 10UZ);
        const std::array spectrum{5.f, 5.f, 5.f, 5.f};
        for (int n = 0; n < 6; ++n) {
            grid.accumulate(spectrum, 0.0, 10.0, 4.0);
        }
        constexpr float kExpected = 3.2880859375f;
        for (std::size_t column = 0UZ; column < 4UZ; ++column) {
            expect(approx(grid.cells[5UZ * 4UZ + column], kExpected, 1e-5f)) << "column" << column;
        }
        expect(approx(std::accumulate(grid.cells.begin(), grid.cells.end(), 0.f), 4.f * kExpected, 1e-4f)) << "no other cell";
        expect(approx(grid.peak, kExpected, 1e-5f));
        expect(approx(grid.fullScale(DensityNormalisation::peak), grid.fullScale(DensityNormalisation::maximum), 1e-5f)) << "a steady signal: the same full scale";
    };

    // ground truth by construction: rows of 1 unit from yMax = 10 down to yMin = 0
    "a value beyond the range lands in the edge row, NaN in none"_test = [] {
        DensityGrid grid;
        grid.resize(4UZ, 10UZ);
        grid.accumulate(std::array{11.f, -1.f, std::numeric_limits<float>::quiet_NaN(), 10.f}, 0.0, 10.0, 0.0);
        expect(eq(grid.cells[0UZ * 4UZ + 0UZ], 1.f)) << "above yMax: the top row";
        expect(eq(grid.cells[9UZ * 4UZ + 1UZ], 1.f)) << "below yMin: the bottom row";
        expect(eq(std::ranges::count(grid.cells | std::views::drop(2UZ) | std::views::stride(4UZ), 0.f), 10)) << "NaN: nowhere";
        expect(eq(grid.cells[0UZ * 4UZ + 3UZ], 1.f)) << "yMax itself: the top row";
    };

    // ground truth by construction: without decay, values 5 then 6 in one column hit two cells once each, after two spectra
    "peak normalisation counts the spectra, maximum the densest cell"_test = [] {
        DensityGrid grid;
        grid.resize(1UZ, 10UZ);
        grid.accumulate(std::array{5.f}, 0.0, 10.0, 0.0);
        grid.accumulate(std::array{6.f}, 0.0, 10.0, 0.0);
        expect(eq(grid.cells[5UZ], 1.f) && eq(grid.cells[4UZ], 1.f));
        expect(eq(grid.fullScale(DensityNormalisation::peak), 2.f)) << "τ = 0 accumulates: the peak counts every spectrum";
        expect(eq(grid.fullScale(DensityNormalisation::maximum), 1.f));
    };

    // ground truth by construction: three points fit ten pixel columns, so the band is y ± σ at each point
    "an error band follows each point while there are no more points than pixel columns"_test = [] {
        const std::array      x{0.0, 1.0, 2.0};
        const std::array      y{0.0, 1.0, 2.0};
        const std::array      sigma{0.5, 1.0, 0.5};
        const axis::ErrorBand band = axis::errorBand(x, y, sigma, 10UZ);
        expect(band.perPoint);
        expect(band.x == std::vector{0.0, 1.0, 2.0});
        expect(band.lower == std::vector{-0.5, 0.0, 1.5});
        expect(band.upper == std::vector{0.5, 2.0, 2.5});
    };

    // ground truth by hand: six points in two columns of three;
    // column 0: y − σ = {-1, 4, -1}, y + σ = {1, 6, 3};
    // column 1: y − σ = {2, 2, -1}, y + σ = {2, 2, 5}
    "a decimated error band keeps each pixel column's lowest y - sigma and highest y + sigma"_test = [] {
        const std::array      x{0.0, 1.0, 2.0, 3.0, 4.0, 5.0};
        const std::array      y{0.0, 5.0, 1.0, 2.0, 2.0, 2.0};
        const std::array      sigma{1.0, 1.0, 2.0, 0.0, 0.0, 3.0};
        const axis::ErrorBand band = axis::errorBand(x, y, sigma, 2UZ);
        expect(!band.perPoint) << "decimated: no bars";
        expect(band.x == std::vector{0.0, 3.0}) << "at each column's first point";
        expect(band.lower == std::vector{-1.0, -1.0});
        expect(band.upper == std::vector{6.0, 5.0});
    };

    // ground truth, arithmetic: 101 points over 400 px are 4 px apart, closer than 5 px caps; 51 points are 8 px apart
    "combo draws bars while their caps do not overlap, else the surface"_test = [] {
        expect(axis::capsOverlap(0.0, 400.0, 101UZ, 5.0)) << "4 px apart: the surface";
        expect(!axis::capsOverlap(0.0, 400.0, 51UZ, 5.0)) << "8 px apart: bars";
        expect(axis::capsOverlap(400.0, 0.0, 101UZ, 5.0)) << "descending x alike";
        expect(!axis::capsOverlap(0.0, 0.0, 1UZ, 5.0)) << "a single point: a bar";
    };

    // ground truth by construction: of {-inf, 3, NaN, -2, +inf} the finite values span -2 to 3
    "a colour scale follows the finite values only"_test = [] {
        constexpr float kInf  = std::numeric_limits<float>::infinity();
        const auto      range = finiteRange(std::array{-kInf, 3.f, std::numeric_limits<float>::quiet_NaN(), -2.f, kInf});
        expect(range.has_value() >> fatal);
        expect(eq(range->first, -2.0) && eq(range->second, 3.0));
        expect(!finiteRange(std::array{-kInf, kInf}).has_value()) << "none finite";

        SurfaceBuffer surface; // a dB spectrum with an empty bin: the first frame sets the scale to its finite span
        surface.init(3UZ, 2UZ);
        surface.updateAutoScale(std::array{-kInf, -60.f, -40.f}, 3UZ);
        expect(eq(surface._scaleMin, -60.0) && eq(surface._scaleMax, -40.0)) << "not -inf";
    };

    "capacity request expiry reduces capacity"_test = [] {
        auto sink = makeTestStreamingSink("expiry_test", 1000);

        const auto now = CapacityRequests::clock::now();
        sink->requestCapacity("chart1", 3000, std::chrono::seconds{60}, now);
        sink->requestCapacity("chart2", 5000, std::chrono::seconds{0}, now);
        expect(eq(sink->bufferCapacity(), 5000UZ));

        sink->expireCapacityRequests(now + std::chrono::milliseconds(10));
        expect(eq(sink->bufferCapacity(), 3000UZ)) << "after expiry, should fall back to chart1's request";
    };

    "SignalKind bitwise operators"_test = [] {
        expect((SignalKind::Streaming | SignalKind::Dataset1D) == SignalKind::All);
        expect((SignalKind::All & SignalKind::Streaming) == SignalKind::Streaming);
        expect((SignalKind::All & SignalKind::Dataset1D) == SignalKind::Dataset1D);
        expect((SignalKind::Streaming & SignalKind::Dataset1D) == SignalKind::None);
        expect((SignalKind::None | SignalKind::None) == SignalKind::None);
        expect((SignalKind::None & SignalKind::All) == SignalKind::None);
    };

    "detail::isCompatible truth table"_test = [] {
        using charts::detail::isCompatible;

        expect(isCompatible(SignalKind::Streaming, SignalKind::Streaming)) << "streaming vs streaming";
        expect(!isCompatible(SignalKind::Streaming, SignalKind::Dataset1D)) << "streaming vs dataset-only";
        expect(isCompatible(SignalKind::Dataset1D, SignalKind::Dataset1D)) << "dataset vs dataset";
        expect(!isCompatible(SignalKind::Dataset1D, SignalKind::Streaming)) << "dataset vs streaming-only";
        expect(isCompatible(SignalKind::Streaming, SignalKind::All)) << "streaming vs all";
        expect(isCompatible(SignalKind::Dataset1D, SignalKind::All)) << "dataset vs all";
        expect(!isCompatible(SignalKind::None, SignalKind::All)) << "none vs all";
        expect(!isCompatible(SignalKind::None, SignalKind::Streaming)) << "none vs streaming";
        expect(!isCompatible(SignalKind::None, SignalKind::Dataset1D)) << "none vs dataset";
        expect(isCompatible(SignalKind::All, SignalKind::All)) << "all vs all";
        expect(isCompatible(SignalKind::All, SignalKind::Streaming)) << "all vs streaming";
        expect(isCompatible(SignalKind::Image2D, SignalKind::Image2D)) << "image vs image";
        expect(!isCompatible(SignalKind::Image2D, SignalKind::All)) << "image vs 1D charts";
    };

    "TestDataSetSink and TestStreamingSink return correct signalKind"_test = [] {
        auto streamingSink = makeTestStreamingSink("streaming_kind_test");
        auto datasetSink   = makeTestDataSetSink("dataset_kind_test");
        expect(streamingSink->signalKind() == SignalKind::Streaming);
        expect(datasetSink->signalKind() == SignalKind::Dataset1D);
    };

    "displayedSignalKinds empty chart returns None"_test = [] {
        auto chart = makeXYChart("compat_empty");
        expect(chart->displayedSignalKinds() == SignalKind::None);
    };

    "displayedSignalKinds with streaming sink"_test = [] {
        auto chart = makeXYChart("compat_streaming");
        chart->addSignalSink(makeTestStreamingSink("s1"));
        expect(chart->displayedSignalKinds() == SignalKind::Streaming);
    };

    "displayedSignalKinds with dataset sink"_test = [] {
        auto chart = makeXYChart("compat_dataset");
        chart->addSignalSink(makeTestDataSetSink("d1"));
        expect(chart->displayedSignalKinds() == SignalKind::Dataset1D);
    };

    "displayedSignalKinds with mixed sinks returns All"_test = [] {
        auto chart = makeXYChart("compat_mixed");
        chart->addSignalSink(makeTestStreamingSink("s1"));
        chart->addSignalSink(makeTestDataSetSink("d1"));
        expect(chart->displayedSignalKinds() == SignalKind::All);
    };

    "chartSignalCompatibilityRegistry contains registered chart types"_test = [] {
        using namespace charts;

        gr::BlockRegistry blockRegistry;
        registerDashboardBlocks(blockRegistry);
        auto expectEntry = []<typename T>() {
            const auto&      registry = charts::chartTypes();
            std::string_view name     = registeredId<T>();
            auto             iterator = registry.find(name);
            if (iterator == registry.end()) {
                expect(false) << name << " should be registered";
            } else {
                expect(iterator->second == T::kSupportedSignals);
            }
        };

        expectEntry.template operator()<XYChart>();
        expectEntry.template operator()<YYChart>();
        expectEntry.template operator()<SpectrumPlot>();
        expectEntry.template operator()<SpectrumDensity>();
        expectEntry.template operator()<SpectrumView>();
        expectEntry.template operator()<SurfacePlot>();
        expectEntry.template operator()<WaterfallPlot>();
        expectEntry.template operator()<ImageChart>();
        expectEntry.template operator()<TensorRowsChart>();
    };

    "a lifecycle reset() on a scheduler thread leaves render state to the render thread's next draw"_test = [] {
        SpectrumPlot plot;
        plot._tracesPerSink["sink"]._frameCount = 7UZ;

        std::thread([&plot] { plot.reset(); }).join();
        expect(eq(plot._tracesPerSink.size(), 1UZ)) << "reset() must not touch draw state off the render thread";

        plot.applyPendingRenderReset();
        expect(plot._tracesPerSink.empty());

        plot._tracesPerSink["sink"]._frameCount = 7UZ;
        plot.applyPendingRenderReset();
        expect(eq(plot._tracesPerSink.size(), 1UZ)) << "one reset() clears once";
    };

    "an ImPlotSink<Tensor> keeps the newest frame, or max_backlog frames in order, and clamps a request into what it holds"_test = [] {
        using namespace std::string_literals;
        const auto framesThrough = [](gr::Size_t backlog, std::vector<std::vector<std::size_t>> shapes, auto check) {
            gr::Graph graph;
            auto&     source = graph.emplaceBlock<gr::testing::TensorSource<float>>();
            auto&     sink   = graph.emplaceBlock<ImPlotSink<gr::Tensor<float>>>({{"max_backlog", backlog}});
            for (std::size_t k = 0UZ; k < shapes.size(); ++k) {
                const std::size_t n = std::ranges::fold_left(shapes[k], 1UZ, std::multiplies{});
                source._frames.push_back({.extents = shapes[k], .elements = std::vector<float>(n, static_cast<float>(k + 1UZ))});
            }
            expect(graph.connect(source, "out"s, sink, "in"s).has_value());
            gr::scheduler::Simple sched;
            expect(sched.exchange(std::move(graph)).has_value());
            expect(sched.runAndWait().has_value());
            check(sink);
        };
        const std::vector<std::vector<std::size_t>> fiveFrames(5UZ, std::vector<std::size_t>{1UZ, 2UZ});
        const auto                                  valueOf = [](const auto& frame) { return std::get<std::span<const float>>(frame.values).front(); };

        framesThrough(2U, fiveFrames, [&](const auto& sink) {
            expect(eq(sink.signalSink()->frame().generation, 5U) && eq(valueOf(sink.signalSink()->frame()), 5.f)) << "the newest";
            expect(eq(sink.signalSink()->frame(1U).generation, 3U) && eq(valueOf(sink.signalSink()->frame(1U)), 3.f)) << "clamped to the oldest held";
            expect(eq(valueOf(sink.signalSink()->frame(4U)), 4.f) && eq(sink.signalSink()->frame(9U).generation, 5U));
            expect(eq(sink.signalSink()->totalSampleCount(), 5UZ)) << "every frame counted, also those not held";
        });
        framesThrough(0U, fiveFrames, [](const auto& sink) { expect(eq(sink.signalSink()->frame(1U).generation, 5U)) << "max_backlog 0 holds the newest only"; });
        framesThrough(0U, {{3UZ}}, [](const auto& sink) { expect(eq(sink.signalSink()->frame().generation, 0U)) << "a rank-1 tensor is no image"; });
    };

    "the image chart counts the frames between two shown ones"_test = [] {
        expect(eq(skippedBetween(0U, 7U), 0U)) << "none before the first frame";
        expect(eq(skippedBetween(3U, 4U), 0U) && eq(skippedBetween(3U, 9U), 5U) && eq(skippedBetween(9U, 9U), 0U));
    };

    "Arithmetic applies each operation and keeps the previous one for an unknown symbol"_test = [] {
        const auto apply = [](auto& block, std::string symbol) {
            std::ignore = block.settings().set({{"operation", std::move(symbol)}});
            std::ignore = block.settings().activateContext();
            std::ignore = block.settings().applyStagedParameters();
        };
        Arithmetic<double> block;
        block.init(std::make_shared<gr::Sequence>());
        expect(eq(block.processOne(6.0, 3.0), 9.0)) << "the default +";
        for (const auto& [symbol, expected] : {std::pair{"-", 3.0}, std::pair{"*", 18.0}, std::pair{"/", 2.0}, std::pair{"+", 9.0}}) {
            apply(block, symbol);
            expect(eq(block.processOne(6.0, 3.0), expected)) << symbol;
        }
        apply(block, "*");
        apply(block, "%");
        expect(eq(block.processOne(6.0, 3.0), 18.0) && block.operation.value == "*") << "an unknown symbol keeps the previous operation";
        Arithmetic<float> single;
        single.init(std::make_shared<gr::Sequence>());
        apply(single, "-");
        expect(eq(single.processOne(1.f, 3.f), -2.f));
    };

    "the dashboard registry provides ImPlotSink for float64, DataSet<float64> and events, Arithmetic for both float types, and the localisation blocks"_test = [] {
        gr::BlockRegistry registry;
        registerDashboardBlocks(registry);
        for (const std::string_view name : {"opendigitizer::ImPlotSink<float32>", "opendigitizer::ImPlotSink<float64>", "opendigitizer::ImPlotSink<gr::DataSet<float32>>", "opendigitizer::ImPlotSink<gr::DataSet<float64>>"}) {
            expect(registry.contains(name)) << name;
        }
        expect(registry.contains("opendigitizer::Arithmetic<float32>") && registry.contains("opendigitizer::Arithmetic<float64>"));
        for (const std::string_view name : {"opendigitizer::ImPlotSink<int16>", "opendigitizer::ImPlotSink<gr::UncertainValue<uint64>>", "opendigitizer::ImPlotSink<gr::DataSet<int8>>", "opendigitizer::ImPlotSink<gr::Tensor<uint16>>", "opendigitizer::ImPlotSink<gr::DataSet<gr::UncertainValue<float32>>>", "opendigitizer::ImPlotSink<gr::DataSet<complex<float32>>>", "opendigitizer::ImPlotSink<complex<float32>>", "opendigitizer::ImPlotSink<gr::UncertainValue<complex<float64>>>"}) {
            expect(registry.contains(name)) << name << "(every sample type in every form, spelt as GR4 spells it)";
        }
        for (const std::string_view name : {"opendigitizer::ImPlotSink<gr::pmt::ValueMap>", "opendigitizer::localisation::DelayLocator", "opendigitizer::localisation::PointSourceArrivals", "opendigitizer::charts::LocalisationChart"}) {
            expect(registry.contains(name)) << name << "(the event sink's key spells gr::property_map as gr::pmt::ValueMap)";
        }
    };

    "TensorRowsChart places and names traces by the frame's axis coordinates, else by index"_test = [] {
        const ImageAxis none{};
        const ImageAxis lag{.origin = -0.001, .step = 1.0 / 48000.0, .name = "lag", .unit = "s"};
        const ImageAxis pair{.origin = 0.0, .step = 1.0, .name = "pair", .unit = ""};
        expect(eq(axisCoordinate(none, 7UZ), 7.0) && lt(std::abs(axisCoordinate(lag, 48UZ)), 1e-12)) << "lag 0 in the middle of 97 columns";
        expect(axisTitle(none, "column") == "column" && axisTitle(lag, "column") == "lag [s]" && axisTitle(pair, "column") == "pair");
        expect(rowLabel(none, 3UZ) == "row 3" && rowLabel(pair, 3UZ) == "pair 3");
    };

    "TensorRowsChart with lag_columns draws a cross-correlation frame against the lag in s"_test = [] {
        const ImageAxis lag = charts::lagAxis(65UZ, 48000.0);
        expect(eq(lag.origin, -32.0 / 48000.0) && eq(lag.step, 1.0 / 48000.0) && lag.name == "lag" && lag.unit == "s");
        expect(lt(std::abs(axisCoordinate(lag, 32UZ)), 1e-12)) << "lag 0 in the middle of 2 * 32 + 1 columns";

        charts::TensorRowsChart chart;
        FrameView               untagged{.values = {}, .rows = 3UZ, .columns = 65UZ};
        expect(chart.columnAxis(untagged, 48000.f).name.empty()) << "off by default: the column index";
        chart.lag_columns = true;
        expect(chart.columnAxis(untagged, 48000.f).name == "lag" && eq(chart.columnAxis(untagged, 48000.f).step, 1.0 / 48000.0));
        expect(chart.columnAxis(untagged, 0.f).name.empty()) << "no sample rate: the column index";
        FrameView tagged = untagged;
        tagged.axes[1]   = {.origin = -0.5, .step = 0.25, .name = "delay", .unit = "ms"};
        expect(chart.columnAxis(tagged, 48000.f).name == "delay" && eq(chart.columnAxis(tagged, 48000.f).step, 0.25)) << "a gr:axis_* tag wins over lag_columns";
    };

#if __has_include(<gnuradio-4.0/localisation/PairCrossSpectra.hpp>)
    "GR4 PairCrossCorrelation's lag axis tag and TensorRowsChart's lagAxis agree: origin -L/fs, step 1/fs, in s"_test = [] {
        using namespace std::string_literals;
        constexpr gr::Size_t kMaxLag = 32U;
        constexpr float      kRate   = 48000.f;
        gr::Graph            graph;
        auto&                source      = graph.emplaceBlock<gr::testing::TensorSource<float>>();
        auto&                correlation = graph.emplaceBlock<gr::localisation::PairCrossCorrelation<float>>({{"max_lag", kMaxLag}});
        auto&                sink        = graph.emplaceBlock<ImPlotSink<gr::Tensor<float>>>();
        std::vector<float>   rows(3UZ * 256UZ);
        for (std::size_t i = 0UZ; i < rows.size(); ++i) {
            rows[i] = std::sin(0.37f * static_cast<float>(i)) + std::cos(1.3f * static_cast<float>(i % 256UZ));
        }
        source._frames.push_back({.extents = {3UZ, 256UZ}, .elements = rows});
        source._sampleRate = kRate;
        expect(graph.connect(source, "out"s, correlation, "in"s).has_value());
        expect(graph.connect(correlation, "out"s, sink, "in"s).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        const auto      frame = sink.signalSink()->frame();
        const ImageAxis ours  = charts::lagAxis(2UZ * kMaxLag + 1UZ, static_cast<double>(kRate));
        expect(eq(frame.columns, 2UZ * kMaxLag + 1UZ) && frame.axes[1].name == "lag" && frame.axes[1].unit == "s") << frame.axes[1].name;
        expect(lt(std::abs(frame.axes[1].origin - ours.origin), 1e-12) && lt(std::abs(frame.axes[1].step - ours.step), 1e-15)) << std::format("tag {} + {}, lagAxis {} + {}", frame.axes[1].origin, frame.axes[1].step, ours.origin, ours.step);
    };
#endif

    "an ImPlotSink<Tensor> takes a frame's axes from its gr:axis_* tags"_test = [] {
        using namespace std::string_literals;
        gr::Graph                  graph;
        auto&                      source = graph.emplaceBlock<gr::testing::TensorSource<float>>();
        auto&                      sink   = graph.emplaceBlock<ImPlotSink<gr::Tensor<float>>>();
        gr::Tensor<gr::pmt::Value> names(gr::extents_from, {2UZ});
        gr::Tensor<gr::pmt::Value> units(gr::extents_from, {2UZ});
        names[0]         = gr::pmt::Value{"pair"};
        names[1]         = gr::pmt::Value{"lag"};
        units[0]         = gr::pmt::Value{""};
        units[1]         = gr::pmt::Value{"s"};
        source._firstTag = gr::property_map{{std::string(gr::tag::kAxisOrigin.key()), gr::Tensor<double>(gr::data_from, {0.0, -0.001})}, {std::string(gr::tag::kAxisStep.key()), gr::Tensor<double>(gr::data_from, {1.0, 1.0 / 48000.0})}, {std::string(gr::tag::kAxisName.key()), names}, {std::string(gr::tag::kAxisUnit.key()), units}};
        source._frames.push_back({.extents = {2UZ, 97UZ}, .elements = std::vector<float>(2UZ * 97UZ, 1.f)});
        source._sampleRate = 48000.f;
        expect(graph.connect(source, "out"s, sink, "in"s).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        const auto frame = sink.signalSink()->frame();
        expect(eq(frame.axes[1].origin, -0.001) && eq(frame.axes[1].step, 1.0 / 48000.0) && frame.axes[1].name == "lag" && frame.axes[1].unit == "s");
        expect(frame.axes[0].name == "pair" && eq(frame.axes[0].step, 1.0));
        expect(eq(sink.signalSink()->sampleRate(), 48000.f)) << "the lag step of a TensorRowsChart with lag_columns";
    };

#if defined(OD_TRIGGER_BLOCKS)
    "GR4's float level and Schmitt triggers, coincidence and EventToTag are available to dashboards under GR4's names"_test = [] {
        gr::BlockRegistry registry;
        registerDashboardBlocks(registry);
        for (const std::string_view key : {"gr::blocks::trigger::LevelTrigger<float32, gr::blocks::trigger::ValueCondition::level>", "gr::blocks::trigger::SchmittTrigger<float32, gr::trigger::InterpolationMethod::LINEAR_INTERPOLATION>", "gr::blocks::trigger::Coincidence", "gr::blocks::trigger::EventToTag<float32>"}) {
            expect(registry.contains(key)) << key;
        }
    };
#endif

    "RDSView reads the tags of a TagSink"_test = [] {
        using gr::digital::RDSDecoder;
        const auto station = [](std::string_view radioText) { return gr::property_map{{RDSDecoder::kProgrammeIdentificationKey, std::uint16_t{0xD3C3U}}, {RDSDecoder::kProgrammeServiceNameKey, std::string("GR4 RDS")}, {RDSDecoder::kRadioTextKey, std::string(radioText)}}; };
        gr::Graph  graph;
        auto&      source = graph.emplaceBlock<gr::testing::TagSource<std::uint8_t>>({{"n_samples_max", gr::Size_t{100U}}, {"mark_tag", false}});
        source._tags.push_back({10UZ, station("first")});
        source._tags.push_back({20UZ, gr::property_map{{"other_key", 1.f}}});
        source._tags.push_back({30UZ, station("second")});
        source._tags.push_back({40UZ, gr::property_map{{RDSDecoder::kTrafficEventKey, std::uint16_t{101U}}}});
        auto& sink = graph.emplaceBlock<TagSink<std::uint8_t>>({{"tag_keys", gr::pmt::Value{std::vector{std::string(RDSDecoder::kProgrammeIdentificationKey), std::string(RDSDecoder::kTrafficEventKey)}}}});
        expect(graph.connect<"out", "in">(source, sink).has_value());
        gr::scheduler::Simple<> scheduler;
        expect(scheduler.exchange(std::move(graph)).has_value());
        expect(scheduler.runAndWait().has_value());

        charts::RDSView::Reception reception;
        std::size_t                kept = 0UZ;
        for (const auto& [utcSeconds, tagMap] : sink.signalSink()->events()) {
            reception.add(utcSeconds, tagMap);
            ++kept;
        }
        expect(sink.signalSink()->signalKind() == SignalKind::Events) << "an RDSView or a LocalisationChart takes it";
        expect(eq(sink.signalSink()->totalSampleCount(), 100UZ)) << "the stream's samples, for the bit rate";
        expect(eq(kept, 3UZ));
        expect(eq(reception.station.programmeServiceName, std::string("GR4 RDS")));
        expect(eq(reception.station.radioText, std::string("second")));
        expect(fatal(eq(reception.earlier.size(), 1UZ)));
        expect(eq(reception.earlier.front().title, std::string("first")));
        expect(eq(reception.trafficMessages.size(), 1UZ));
    };

    "RDSView tunes the tuner centre"_test = [] {
        charts::RDSView view(gr::property_map{{"tuner", std::string("RTLSDR:frequency")}, {"station_frequency", 89.3e6}, {"channel_frequency", -250e3}});
        view.init(view.progress);
        gr::MsgPortIn in;
        expect(fatal(view.msgOut.connect(in).has_value()));
        view.tune(89.5e6);
        auto messages = in.streamReader().get();
        expect(fatal(eq(messages.size(), 1UZ)));
        expect(eq(messages[0UZ].serviceName, std::string("RTLSDR")));
        expect(fatal(messages[0UZ].data.has_value()));
        expect(eq(messages[0UZ].data->value_or<double>("frequency", 0.0), 89.75e6));
        expect(messages.consume(messages.size()));
    };

    return 0;
}
