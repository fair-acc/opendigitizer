#ifndef OPENDIGITIZER_LOCALISATION_POINTSOURCEARRIVALS_HPP
#define OPENDIGITIZER_LOCALISATION_POINTSOURCEARRIVALS_HPP

#include "DelayAssociation.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <numbers>
#include <string>
#include <vector>

namespace opendigitizer::localisation {

GR_REGISTER_BLOCK(opendigitizer::localisation::PointSourceArrivals)

struct PointSourceArrivals : gr::Block<PointSourceArrivals> {
    using Description = gr::Doc<R""(@brief Arrival events of a point source at the receivers (and, bistatic, its emission), dated by the travel times, every `period` s of the `clk` stream.

TDOA: one event per receiver at emission + |p - r| / c; bistatic: the transmitter's emission event, then each receiver's echo at
emission + (|s - p| + |p - r|) / c. The source circles `source_position` in the xy plane with `orbit_radius` once per `orbit_period`.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<std::uint8_t> clk;
    gr::EventPortOut         evtOut{{.streamSlotsPerPublish = 8UZ}};

    ModeSetting                                                                                                                                                                    mode                  = Mode::tdoa;
    gr::algorithm::ElementPositions                                                                                                                                                element_positions     = kDeckSquare;
    ReceiverNames                                                                                                                                                                  receiver_names        = {};
    TransmitterPositions                                                                                                                                                           transmitter_positions = kOneTransmitter;
    TransmitterNames                                                                                                                                                               transmitter_names     = {};
    gr::algorithm::PropagationSpeed                                                                                                                                                propagation_speed     = 343.0;
    A<std::vector<double>, "source_position", gr::Unit<"m">, gr::Visible, gr::Doc<"x, y, z">>                                                                                      source_position       = std::vector{0.3, 0.2, 0.0};
    A<double, "orbit_radius", gr::Unit<"m">, gr::Visible>                                                                                                                          orbit_radius          = 0.0;
    A<double, "orbit_period", gr::Unit<"s">, gr::Limits<1e-3, 1e6>>                                                                                                                orbit_period          = 10.0;
    A<double, "period", gr::Unit<"s">, gr::Limits<1e-6, 1e6>, gr::Doc<"between emissions">>                                                                                        period                = 0.5;
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"of clk; forwarded from upstream">, gr::Limits<0.f, std::numeric_limits<float>::max(), [](float rate) { return 0.f < rate; }>> sample_rate           = 1000.f;
    A<gr::Size_t, "n_events", gr::Doc<"emissions before the block stops; 0: unlimited">>                                                                                           n_events              = 0U;

    GR_MAKE_REFLECTABLE(PointSourceArrivals, clk, evtOut, mode, element_positions, receiver_names, transmitter_positions, transmitter_names, propagation_speed, source_position, orbit_radius, orbit_period, period, sample_rate, n_events);

    std::uint64_t                _samples = 0U;
    std::uint64_t                _emitted = 0U;
    std::deque<gr::property_map> _pending;

    void start() {
        _samples = 0U;
        _emitted = 0U;
        _pending.clear();
    }

    [[nodiscard]] Point3 sourceAt(double t) const { return orbitPoint(source_position.value, orbit_radius.value, orbit_period.value, t); }

    void schedule(double emission) {
        const Point3 p = sourceAt(emission);
        const double c = propagation_speed.value;
        if (!(c > 0.0 && std::isfinite(c))) {
            return;
        }
        std::vector<std::pair<double, std::string>> events;
        double                                      outbound = 0.0;
        if (mode == Mode::bistatic && rowsOf(transmitter_positions.value) > 0UZ) {
            outbound = distance(rowOf(transmitter_positions.value, 0UZ), p) / c;
            events.emplace_back(emission, nameOf(transmitter_names.value, 0UZ, "tx"));
        }
        for (std::size_t m = 0UZ; m < rowsOf(element_positions.value); ++m) {
            events.emplace_back(emission + outbound + distance(p, rowOf(element_positions.value, m)) / c, nameOf(receiver_names.value, m, ""));
        }
        std::ranges::stable_sort(events, {}, &std::pair<double, std::string>::first);
        for (const auto& [time, eventName] : events) {
            _pending.push_back(datedEvent(eventName, time));
        }
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& clkSpan, gr::OutputSpanLike auto& evtSpan) {
        _samples += clkSpan.size();
        const double now = static_cast<double>(_samples) / static_cast<double>(sample_rate.value);
        while ((n_events == 0U || _emitted < n_events) && static_cast<double>(_emitted) * period.value <= now) {
            schedule(static_cast<double>(_emitted) * period.value);
            ++_emitted;
        }
        std::size_t emitted = 0UZ;
        while (!_pending.empty() && emitted < evtSpan.size()) {
            if (!gr::emit(evtSpan, emitted, gr::property_map_view{_pending.front()})) {
                break;
            }
            _pending.pop_front();
            ++emitted;
        }
        evtSpan.publish(emitted);
        const bool done = n_events > 0U && _emitted >= n_events && _pending.empty();
        return done ? gr::work::Status::DONE : gr::work::Status::OK;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_POINTSOURCEARRIVALS_HPP
