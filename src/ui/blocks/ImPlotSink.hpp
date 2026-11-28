#ifndef OPENDIGITIZER_IMPLOTSINK_HPP
#define OPENDIGITIZER_IMPLOTSINK_HPP

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/meta/UncertainValue.hpp>

#include "../common/Imw.hpp"
#include "../common/LookAndFeel.hpp"

#include "../components/SignalColours.hpp"

#include <implot.h>

#include "stdex/cast.hpp"
#include <PluginPaths.hpp>

#include "../charts/Chart.hpp"
#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"
#include "../utils/Browser.hpp"

namespace gr::od {

using charts::AxisScale;
using charts::tags::kFishyTagKey;

template<typename T>
T getValueOrDefault(const gr::property_map& map, std::string_view key, const T& defaultValue) noexcept {
    if (auto it = map.find(key); it != map.end()) {
        return gr::pmt::convert_safely<T, false>(it->second).value_or(defaultValue);
    }
    return defaultValue;
}

template<typename T>
struct ImPlotSink : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::Content, "ImGui">> {
    using ValueType = gr::meta::fundamental_base_value_type_t<T>;
    using LineStyle = gr::od::LineStyle;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    using Description = gr::Doc<"Buffers the samples, DataSets, Tensor frames or events of its input as a chart signal with a name, colour, line style, quantity and unit; streaming samples are dated in UTC from the trigger tags.">;

    static constexpr bool kIsStreaming     = std::is_arithmetic_v<T> || gr::UncertainValueLike<T> || gr::meta::complex_like<T>;
    static constexpr bool kIsComplexStream = []() consteval {
        if constexpr (gr::UncertainValueLike<T>) {
            return gr::meta::complex_like<typename T::value_type>;
        } else {
            return gr::meta::complex_like<T>;
        }
    }();
    static constexpr bool kExactInFloat = std::is_same_v<ValueType, float> || (std::is_integral_v<ValueType> && sizeof(ValueType) <= 2UZ);
    static constexpr bool kHasErrors    = []() consteval {
        if constexpr (gr::DataSetLike<T>) {
            return gr::UncertainValueLike<typename T::value_type>;
        } else {
            return gr::UncertainValueLike<T>;
        }
    }();
    static constexpr bool kIsDataSet = gr::DataSetLike<T>;
    static constexpr bool kIsTensor  = gr::TensorLike<T>;
    static constexpr bool kIsEvents  = std::is_same_v<T, gr::property_map>;

    static constexpr SignalKind kSignalKind = []() consteval {
        if constexpr (kIsEvents) {
            return SignalKind::Events;
        } else if constexpr (kIsTensor) {
            return SignalKind::Image2D;
        } else if constexpr (kIsDataSet) {
            return SignalKind::Dataset1D;
        } else {
            return SignalKind::Streaming;
        }
    }();
    static constexpr gr::Size_t kDefaultRequiredSize = []() consteval -> gr::Size_t {
        if constexpr (kIsTensor) {
            return 1U;
        } else if constexpr (kIsEvents) {
            return 64U;
        } else if constexpr (kIsDataSet) {
            return static_cast<gr::Size_t>(charts::Chart::kDefaultDataSetHistorySize);
        } else {
            return 2048U;
        }
    }();
    using Stored = std::conditional_t<kIsDataSet, gr::DataSet<float>, std::conditional_t<kIsStreaming, std::conditional_t<gr::UncertainValueLike<T>, ValueType, T>, std::monostate>>;

    std::conditional_t<kIsTensor, gr::TensorPortIn<ValueType>, std::conditional_t<kIsEvents, gr::EventPortIn, gr::PortIn<T>>> in;

    A<bool, "visible", gr::Doc<"Whether the signal is drawn in charts">>                                 visible = true;
    A<std::pmr::string, "name", gr::Doc<"Human-readable identifier for the signal">, gr::Visible>        signal_name;
    A<uint32_t, "colour", gr::Unit<"Color">, gr::Doc<"RGB colour for the plot">>                         color      = 0U;
    A<LineStyle, "style", gr::Doc<"Line drawing style: 0=Solid, 1=Dashed, 2=Dotted, 3=DashDot, 4=None">> line_style = LineStyle::Solid;
    A<float, "width", gr::Doc<"Line width in pixels">, gr::Unit<"px">, gr::Limits<0.1f, 10.f>>           line_width = 1.0f;
    A<std::pmr::string, "quantity", gr::Doc<"Physical quantity represented by the signal">, gr::Visible> signal_quantity;
    A<std::pmr::string, "unit", gr::Doc<"Unit of measurement for the signal values">, gr::Visible>       signal_unit;

    A<float, "min", gr::Doc<"Minimum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min = std::numeric_limits<float>::lowest();
    A<float, "max", gr::Doc<"Maximum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max = std::numeric_limits<float>::max();

    A<float, "sample rate", gr::Doc<"Sampling frequency in Hz">, gr::Unit<"Hz">, gr::Limits<float(0), std::numeric_limits<float>::max()>>                             sample_rate       = 1000.0f;
    A<std::string, "abscissa quantity", gr::Doc<"Physical quantity of the primary (X) axis">>                                                                         abscissa_quantity = "time";
    A<std::string, "abscissa unit", gr::Doc<"Unit of measurement of the primary (X) axis">>                                                                           abscissa_unit     = "s";
    A<gr::Size_t, "required buffer size", gr::Doc<"Minimum number of samples to retain">>                                                                             required_size     = kDefaultRequiredSize;
    A<gr::Size_t, "dataset index", gr::Doc<"Index of the dataset, if applicable">>                                                                                    dataset_index     = std::numeric_limits<gr::Size_t>::max();
    A<gr::Size_t, "history length", gr::Doc<"Number of samples retained for historical visualization">>                                                               n_history         = 3U;
    A<float, "history offset", gr::Doc<"Time offset for historical data display">, gr::Unit<"s">, gr::Limits<float(0.0), std::numeric_limits<float>::max()>>          history_offset    = 0.01f;
    A<bool, "plot tags", gr::Doc<"true: draw the timing tags">>                                                                                                       plot_tags         = true;
    A<gr::Size_t, "max backlog", gr::Limits<0U, 256U>, gr::Doc<"Tensor frames queued for in-order display; 0 = newest only (skipped frames are shown by the chart)">> max_backlog       = 0U;
    A<bool, "packed colour", gr::Doc<"Tensor<uint32> frames as packed 0xAARRGGBB colour; false: as integer values, colour-mapped (other types ignore it)">>           packed_colour     = true;

    GR_MAKE_REFLECTABLE(ImPlotSink, in, visible, signal_name, color, line_style, line_width, signal_quantity, signal_unit, signal_min, signal_max, sample_rate, abscissa_quantity, abscissa_unit, required_size, //
        dataset_index, n_history, history_offset, plot_tags, max_backlog, packed_colour);

    ManagedColour _colour;

    double _xUtcOffset = [] {
        using namespace std::chrono;
        auto now = system_clock::now().time_since_epoch();
        return duration<double, std::nano>(now).count() * 1e-9;
    }(); // utc timestamp of the last tag or first sample
    bool        _xUtcOffsetInitialised = false; // after the first kTriggerTime tag
    double      _samplePeriod          = 1.0 / static_cast<double>(sample_rate);
    std::size_t _sampleCount           = 0UZ; // since the last trigger

    std::shared_ptr<SignalSink>  _sink;
    std::shared_ptr<SignalSink>  _imaginarySink;
    std::optional<ManagedColour> _imaginaryColour; // its own, so the two traces differ
    bool                         _registered = false;

    ImPlotSink(gr::property_map initParameters) : gr::Block<ImPlotSink<T>, gr::Drawable<gr::UICategory::Content, "ImGui">>(std::move(initParameters)), _sink(makeSignalSink("")) {
        if constexpr (kIsComplexStream) {
            _imaginarySink = makeSignalSink(kImaginaryPartSuffix);
            _imaginaryColour.emplace();
        }
    }

    ~ImPlotSink() {
        if (_registered) {
            charts::SinkRegistry::instance().unregisterSink(this->unique_name);
            if (_imaginarySink) {
                charts::SinkRegistry::instance().unregisterSink(_imaginarySink->uniqueName());
            }
        }
    }

    [[nodiscard]] std::shared_ptr<SignalSink> makeSignalSink(std::string_view suffix) const { return std::make_shared<SignalSink>(SignalSink::Config{.kind = kSignalKind, .uniqueName = std::format("{}{}", this->unique_name, suffix), .name = std::format("{}{}", this->name.value, suffix), .capacity = kDefaultRequiredSize, .capacityFloor = kIsDataSet ? charts::Chart::kDefaultDataSetHistorySize : (kIsStreaming ? 2048UZ : 0UZ), .doubleSamples = !kExactInFloat, .withErrors = kHasErrors}); }

    /// in the order charts lock them (lockAll)
    [[nodiscard]] std::vector<DataGuard> lockSignals() const { return _imaginarySink ? lockAll(std::array{_sink.get(), _imaginarySink.get()}) : lockAll(std::array{_sink.get()}); }

    void forEachSignal(auto&& fn) {
        fn(*_sink);
        if (_imaginarySink) {
            fn(*_imaginarySink);
        }
    }

    [[nodiscard]] const std::shared_ptr<SignalSink>& signalSink() const noexcept { return _sink; }

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        if constexpr (kIsEvents) {
            {
                const DataGuard guard      = _sink->dataGuard();
                const double    utcSeconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
                for (const gr::property_map_view& event : input) {
                    _sink->pushEvent(utcSeconds, gr::property_map(event));
                }
                _sink->keepNewestEvents(_sink->bufferCapacity());
            }
            std::ignore = input.consume(input.size());
            return gr::work::Status::OK;
        } else {
            return appendSamples(input);
        }
    }

    void settingsChanged(const gr::property_map& /* oldSettings */, const gr::property_map& newSettings) {
        if (newSettings.contains("color") || color == 0U) {
            if (color == 0U) {
                _colour.updateColour();
            } else {
                _colour.setColour(color);
            }
            color = _colour.colour();
        }
        _samplePeriod = 1.0 / static_cast<double>(sample_rate);

        {
            const auto guards = lockSignals();
            copyMetadata();
            forEachSignal([this](SignalSink& signal) { signal.setConfiguredCapacity(static_cast<std::size_t>(required_size.value)); });
        }
        if (!_registered) {
            _sink->_name = this->name.value; // the name charts bind by, as the block had it when it first appeared
            _registered  = charts::SinkRegistry::instance().registerSink(_sink);
            if (_imaginarySink) {
                _imaginarySink->_name = std::format("{}{}", this->name.value, kImaginaryPartSuffix);
                std::ignore           = charts::SinkRegistry::instance().registerSink(_imaginarySink);
            }
        }
    }

    void copyMetadata() {
        _sink->metadata = {.signalName = std::string(signal_name.value),
            .signalQuantity            = std::string(signal_quantity.value),
            .signalUnit                = std::string(signal_unit.value),
            .abscissaQuantity          = abscissa_quantity,
            .abscissaUnit              = abscissa_unit, //
            .color                     = color,
            .lineWidth                 = line_width,
            .sampleRate                = sample_rate,
            .visible                   = visible,
            .datasetIndex              = static_cast<std::size_t>(dataset_index.value)};
        if (_imaginarySink) { // as GR4's FFT names its rows
            const std::string signal            = signal_name.value.empty() ? std::string(this->name.value) : std::string(signal_name.value);
            _imaginarySink->metadata            = _sink->metadata;
            _sink->metadata.signalName          = std::format("Re({})", signal);
            _imaginarySink->metadata.signalName = std::format("Im({})", signal);
            _imaginarySink->metadata.color      = _imaginaryColour->colour();
        }
    }

    void updateAxisMetadataFromDataSet(const T& ds) {
        if constexpr (kIsDataSet) {
            const auto autoUpdate = this->settings().autoUpdateParameters(this->settings().activeContext());
            const auto follow     = [&autoUpdate](auto& setting, std::string_view key, const auto& names) {
                if (std::ranges::contains(autoUpdate, key) && !names.empty() && !names[0].empty() && std::string_view(setting.value) != std::string_view(names[0])) {
                    setting = names[0];
                    return true;
                }
                return false;
            };
            bool changed = follow(abscissa_quantity, "abscissa_quantity", ds.axis_names);
            changed      = follow(abscissa_unit, "abscissa_unit", ds.axis_units) || changed;
            changed      = follow(signal_quantity, "signal_quantity", ds.signal_quantities) || changed;
            changed      = follow(signal_unit, "signal_unit", ds.signal_units) || changed;
            if (changed) {
                copyMetadata();
            }
        }
    }

    void updateAxes(const gr::property_map& tag) {
        const auto                     field   = [&tag](std::string_view key) { return tag.find_value(key).value_or(gr::pmt::Value{}); };
        const auto                     doubles = [&field](std::string_view key) { return gr::pmt::convertTo_or<std::vector<double>>(field(key), std::vector<double>{}); };
        const auto                     strings = [&field](std::string_view key) { return gr::pmt::convertTo_or<std::vector<std::string>>(field(key), std::vector<std::string>{}); };
        const std::vector<double>      origins = doubles(gr::tag::kAxisOrigin.key());
        const std::vector<double>      steps   = doubles(gr::tag::kAxisStep.key());
        const std::vector<std::string> names   = strings(gr::tag::kAxisName.key());
        const std::vector<std::string> units   = strings(gr::tag::kAxisUnit.key());
        for (std::size_t d = 0UZ; d < _sink->_axes.size(); ++d) {
            ImageAxis& axis = _sink->_axes[d];
            if (d < origins.size()) {
                axis.origin = origins[d];
            }
            if (d < steps.size()) {
                axis.step = steps[d];
            }
            if (d < names.size()) {
                axis.name = names[d];
            }
            if (d < units.size()) {
                axis.unit = units[d];
            }
        }
    }

    [[nodiscard]] static float valueOf(const auto& x) { return static_cast<float>(std::real(gr::value(x))); }

    [[nodiscard]] static DataSetErrors errorsOf(const T& dataSet) {
        const auto deviations = [](const auto& values) { return values | std::views::transform([](const auto& x) { return static_cast<float>(gr::uncertainty(x)); }) | std::ranges::to<std::vector>(); };
        return {.signals = deviations(dataSet.signal_values), .axes = dataSet.axis_values | std::views::transform(deviations) | std::ranges::to<std::vector>()};
    }

    [[nodiscard]] static Stored toStored(const auto& item) { // a tensor sink's port hands TensorViews; the tensor path never stores here
        if constexpr (gr::UncertainValueLike<T>) {
            return item.value;
        } else if constexpr (kIsDataSet && !std::is_same_v<T, gr::DataSet<float>>) {
            const auto         toFloats = [](const auto& values) { return values | std::views::transform([](const auto& x) { return valueOf(x); }); };
            gr::DataSet<float> converted;
            converted.default_value    = valueOf(item.default_value);
            converted.timestamp        = item.timestamp;
            converted.axis_names       = item.axis_names;
            converted.axis_units       = item.axis_units;
            converted.extents          = item.extents;
            converted.layout           = item.layout;
            converted.meta_information = item.meta_information;
            converted.timing_events    = item.timing_events;
            for (const auto& axis : item.axis_values) {
                std::ranges::copy(toFloats(axis), std::back_inserter(converted.axis_values.emplace_back()));
            }
            if constexpr (gr::meta::complex_like<typename T::value_type>) { // Re and Im rows, named as GR4's FFT does
                const std::size_t signals = item.signal_names.size();
                const std::size_t points  = signals == 0UZ ? 0UZ : item.signal_values.size() / signals;
                for (std::size_t s = 0UZ; s < signals; ++s) {
                    const auto values = std::span(item.signal_values).subspan(s * points, points);
                    for (const bool imaginary : {false, true}) {
                        const auto part = [imaginary](const auto& z) { return static_cast<float>(imaginary ? z.imag() : z.real()); };
                        converted.signal_names.emplace_back(std::format("{}({})", imaginary ? "Im" : "Re", item.signal_names[s]));
                        converted.signal_quantities.push_back(s < item.signal_quantities.size() ? item.signal_quantities[s] : std::pmr::string{});
                        converted.signal_units.push_back(s < item.signal_units.size() ? item.signal_units[s] : std::pmr::string{});
                        std::ranges::copy(values | std::views::transform(part), std::back_inserter(converted.signal_values));
                        if (s < item.signal_ranges.size()) {
                            converted.signal_ranges.push_back({part(item.signal_ranges[s].min), part(item.signal_ranges[s].max)});
                        }
                    }
                }
            } else {
                converted.signal_names      = item.signal_names;
                converted.signal_quantities = item.signal_quantities;
                converted.signal_units      = item.signal_units;
                std::ranges::copy(toFloats(item.signal_values), std::back_inserter(converted.signal_values));
                for (const auto& range : item.signal_ranges) {
                    converted.signal_ranges.push_back({valueOf(range.min), valueOf(range.max)});
                }
            }
            return converted;
        } else if constexpr (kIsStreaming || kIsDataSet) {
            return item;
        } else {
            return Stored{};
        }
    }

    gr::work::Status appendSamples(gr::InputSpanLike auto& input) noexcept {
        const auto guards = lockSignals();
        if constexpr (kIsTensor) {
            for (const auto& [relIndex, tagMapRef] : input.tags()) {
                updateAxes(tagMapRef.get());
            }
            const std::size_t keep = static_cast<std::size_t>(max_backlog.value) + 1UZ;
            if constexpr (std::is_same_v<ValueType, std::uint32_t>) {
                if (packed_colour) {
                    _sink->template pushFrames<std::uint32_t>(input, keep);
                    return gr::work::Status::OK;
                }
            }
            _sink->template pushFrames<float>(input, keep);
            return gr::work::Status::OK;
        }

        for (const auto& [relIndex, tagMapRef] : input.tags()) {
            if (relIndex < 0) {
                continue; // skip unconsumed pre-span tags
            }
            if (relIndex > 0) {
                break; // only process tags at the start of the span
            }
            const gr::property_map& tagMap = tagMapRef.get();
            if (tagMap.contains(gr::tag::kTriggerTime)) {
                const auto   offsetSeconds = static_cast<double>(getValueOrDefault<float>(tagMap, gr::tag::kTriggerOffset, 0.f));
                const auto   triggerTimeNs = getValueOrDefault<std::uint64_t>(tagMap, gr::tag::kTriggerTime, 0U);
                const double tagEventTime  = static_cast<double>(triggerTimeNs) * 1e-9 + offsetSeconds;
                bool         tagOK         = true;

                const bool firstTrigger = !_xUtcOffsetInitialised;

                if ((triggerTimeNs > 0U || tagEventTime > 0.0) && (firstTrigger || tagEventTime > _xUtcOffset)) {
                    if constexpr (kIsStreaming) {
                        if (firstTrigger) {
                            // the provisional history rebased so that the next sample starts at the first valid trigger time
                            const double shift = tagEventTime - (_xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod);
                            forEachSignal([shift](SignalSink& signal) { signal.shiftTimes(shift); });
                        }
                        _xUtcOffset            = tagEventTime;
                        _sampleCount           = 0UZ;
                        _xUtcOffsetInitialised = true;
                    }
                } else {
                    tagOK = false;
                }

                if (plot_tags && kIsStreaming) {
                    forEachSignal([&](SignalSink& signal) {
                        gr::property_map& kept = signal.pushTag(_xUtcOffset, tagMap);
                        if (!tagOK) {
                            kept[std::pmr::string(kFishyTagKey)] = true;
                        }
                    });
                }
            }

            // surface dropped-sample tags at the current position so the gap is visible in the plot
            if (plot_tags && kIsStreaming && tagMap.contains(gr::tag::kNDroppedSamples)) {
                forEachSignal([&](SignalSink& signal) { signal.pushTag(_xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod, tagMap); });
            }
        }

        for (const auto& sample : input) {
            if constexpr (kIsStreaming) {
                const double utcSeconds = _xUtcOffset + static_cast<double>(_sampleCount) * _samplePeriod;
                if constexpr (kIsComplexStream && kHasErrors) {
                    _sink->pushSample(utcSeconds, sample.value.real(), sample.uncertainty.real());
                    _imaginarySink->pushSample(utcSeconds, sample.value.imag(), sample.uncertainty.imag());
                } else if constexpr (kIsComplexStream) {
                    _sink->pushSample(utcSeconds, sample.real());
                    _imaginarySink->pushSample(utcSeconds, sample.imag());
                } else if constexpr (kHasErrors) {
                    _sink->pushSample(utcSeconds, sample.value, sample.uncertainty);
                } else {
                    _sink->pushSample(utcSeconds, toStored(sample));
                }
            } else if constexpr (std::is_same_v<T, gr::DataSet<float>>) {
                _sink->pushDataSet(sample);
            } else if constexpr (kIsDataSet) {
                if constexpr (kHasErrors) {
                    _sink->pushDataSet(toStored(sample), errorsOf(sample));
                } else {
                    _sink->pushDataSet(toStored(sample));
                }
            }
            ++_sampleCount;

            if constexpr (kIsDataSet) {
                updateAxisMetadataFromDataSet(sample);
            }
        }

        if constexpr (kIsStreaming) {
            forEachSignal([](SignalSink& signal) { signal.dropTagsBeforeOldestSample(); });
        }

        return gr::work::Status::OK;
    }

    // the Drawable trait: charts draw a sink's data, the sink itself draws nothing
    gr::work::Status draw(const gr::property_map& /*config*/ = {}) noexcept { return gr::work::Status::OK; }
};

template<typename... T>
void registerPlotSinks(gr::BlockRegistry& registry) {
    (plotSinkTypes().insert_or_assign(registerWithIdPrefix<ImPlotSink<T>>(registry, kOpendigitizerIdPrefix), gr::meta::is_instantiation_of<T, gr::DataSet>), ...);
}

template<typename... T>
void registerStreamSinks(gr::BlockRegistry& registry) {
    registerPlotSinks<T..., gr::UncertainValue<T>...>(registry);
}

template<typename... T>
void registerFrameSinks(gr::BlockRegistry& registry) {
    registerPlotSinks<gr::DataSet<T>..., gr::Tensor<T>...>(registry);
}

} // namespace gr::od

#endif
