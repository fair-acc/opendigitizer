#ifndef OPENDIGITIZER_UI_BLOCKS_VALUESOURCE_HPP
#define OPENDIGITIZER_UI_BLOCKS_VALUESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::ValueSource, [T], [ float, double ])

template<typename T>
struct ValueSource : gr::Block<ValueSource<T>> {
    using Description = gr::Doc<"Publishes its `value` setting as one asynchronous sample each time it is set, dated by the wall clock at publication, e.g. a chart's dragged range sent by message">;

    gr::PortOut<T, gr::Async> out;

    gr::Annotated<T, "value", gr::Doc<"the last value set; each Set publishes it">> value = T{};

    GR_MAKE_REFLECTABLE(ValueSource, out, value);

    std::optional<T> _pending;

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (newSettings.contains("value")) {
            _pending = value.value;
        }
    }

    gr::work::Status processBulk(gr::OutputSpanLike auto& outSpan) {
        if (!_pending || outSpan.empty()) {
            outSpan.publish(0UZ);
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS; // nothing set: lets the scheduler idle, as ClockSource does
        }
        const auto nowNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        outSpan[0UZ]     = *_pending;
        outSpan.publishTag(gr::property_map{{std::string(gr::tag::kTriggerName.key()), std::string(this->name.value)}, {std::string(gr::tag::kTriggerTime.key()), nowNs}, {std::string(gr::tag::kTriggerOffset.key()), 0.f}}, 0UZ);
        outSpan.publish(1UZ);
        _pending.reset();
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_UI_BLOCKS_VALUESOURCE_HPP
