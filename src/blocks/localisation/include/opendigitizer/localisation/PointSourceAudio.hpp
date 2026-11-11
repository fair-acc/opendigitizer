#ifndef OPENDIGITIZER_LOCALISATION_POINTSOURCEAUDIO_HPP
#define OPENDIGITIZER_LOCALISATION_POINTSOURCEAUDIO_HPP

#include "DelayAssociation.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/algorithm/rng/Xoshiro256pp.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <ranges>
#include <span>
#include <vector>

namespace opendigitizer::localisation {

GR_REGISTER_BLOCK(opendigitizer::localisation::PointSourceAudio)

struct PointSourceAudio : gr::Block<PointSourceAudio> {
    using Description = gr::Doc<R""(@brief What the microphones at `element_positions` record of a point source that clicks every `period` s, paced by `clk`.
Each click is a raised-cosine pulse, the same amplitude at every microphone (so a threshold trigger dates every channel alike), delayed by |p - r| / c to the sample's fraction.
Outputs without a microphone are silent, so `element_positions` may change while connected.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<std::uint8_t>        clk;
    std::vector<gr::PortOut<float>> out;

    gr::algorithm::ElementPositions                                                                                                                                                element_positions = kDeckSquare;
    A<gr::Size_t, "n_outputs", gr::Doc<"fixed number of outputs, 0: one per microphone">, gr::Limits<gr::Size_t(0), gr::Size_t(gr::algorithm::kMaxElements)>>                      n_outputs         = 0U;
    gr::algorithm::PropagationSpeed                                                                                                                                                propagation_speed = kSpeedOfSoundInAir;
    SourcePosition                                                                                                                                                                 source_position   = std::vector{0.3, 0.2, 0.0};
    SourceMotionSetting                                                                                                                                                            source_motion     = SourceMotion::fixed;
    MotionExtent                                                                                                                                                                   motion_extent     = std::vector{0.0, 0.0};
    MotionPeriod                                                                                                                                                                   motion_period     = 10.0;
    Waypoints                                                                                                                                                                      waypoints         = {};
    A<double, "period", gr::Unit<"s">, gr::Limits<1e-3, 1e6>, gr::Doc<"between clicks">>                                                                                           period            = 0.5;
    A<double, "pulse_width", gr::Unit<"s">, gr::Limits<1e-6, 1.0>, gr::Doc<"of the raised-cosine click">>                                                                          pulse_width       = 0.5e-3;
    A<double, "tone_frequency", gr::Unit<"Hz">, gr::Doc<"0: a click; else a tone under the pulse, e.g. a struck tuning fork">>                                                     tone_frequency    = 0.0;
    A<float, "amplitude">                                                                                                                                                          amplitude         = 0.8f;
    A<float, "noise", gr::Doc<"uniform noise amplitude">>                                                                                                                          noise             = 0.0f;
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"of clk; forwarded from upstream">, gr::Limits<0.f, std::numeric_limits<float>::max(), [](float rate) { return 0.f < rate; }>> sample_rate       = 48000.f;
    A<float, "tag_interval", gr::Unit<"s">, gr::Limits<1e-3f, 1e6f>, gr::Doc<"between each output's sample_rate and gr:trigger_time tags, dating a reader that joins later">>      tag_interval      = 1.0f;

    GR_MAKE_REFLECTABLE(PointSourceAudio, clk, out, element_positions, n_outputs, propagation_speed, source_position, source_motion, motion_extent, motion_period, waypoints, period, pulse_width, tone_frequency, amplitude, noise, sample_rate, tag_interval);

    std::uint64_t         _samples  = 0U;
    std::uint64_t         _anchorNs = 1'000'000'000U; // the clock's first gr:trigger_time, dating sample 0
    bool                  _anchored = false;
    std::size_t           _untilTag = 0UZ;
    gr::rng::Xoshiro256pp _rng;

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) { out.resize(n_outputs.value > 0U ? n_outputs.value : std::min(rowsOf(element_positions.value), gr::algorithm::kMaxElements)); }

    void start() {
        _samples  = 0U;
        _anchored = false;
        _untilTag = 0UZ;
        _rng      = gr::rng::Xoshiro256pp{};
    }

    [[nodiscard]] Point3 sourceAt(double t) const { return sourcePoint(source_motion.value, source_position.value, motion_extent.value, motion_period.value, waypoints.value, t); }

    [[nodiscard]] double clickAt(double t, const Point3& receiver) const {
        if (!(propagation_speed.value > 0.0)) {
            return 0.0;
        }
        const double emission = std::floor(t / period.value) * period.value;
        const double since    = t - emission - distance(sourceAt(emission), receiver) / propagation_speed.value;
        if (since < 0.0 || since > pulse_width.value) {
            return 0.0;
        }
        const double envelope = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * since / pulse_width.value));
        return tone_frequency.value > 0.0 ? envelope * std::sin(2.0 * std::numbers::pi * tone_frequency.value * since) : envelope;
    }

    template<typename TOutputSpan>
    gr::work::Status processBulk(gr::InputSpanLike auto& clkSpan, const std::span<TOutputSpan>& outputs) {
        if (!_anchored) {
            for (const auto& [index, tagMap] : clkSpan.tags()) {
                if (const auto* time = gr::property_map_view{tagMap.get()}.template get_if<std::uint64_t>(gr::tag::kTriggerTime.key()); time != nullptr && index <= 0) {
                    _anchorNs = *time;
                }
            }
        }
        const std::size_t         n         = std::ranges::fold_left(outputs | std::views::transform([](auto& output) { return output.size(); }), clkSpan.size(), std::ranges::min);
        const std::vector<Point3> receivers = pointsOf(element_positions.value);
        const double              rate      = static_cast<double>(sample_rate.value);
        const std::size_t         interval  = std::max(1UZ, static_cast<std::size_t>(std::llround(static_cast<double>(tag_interval.value) * rate)));
        std::vector<std::size_t>  tagOffsets;
        for (std::size_t offset = _untilTag; offset < n; offset += interval) {
            tagOffsets.push_back(offset);
        }
        for (std::size_t m = 0UZ; m < outputs.size(); ++m) {
            for (const std::size_t offset : tagOffsets) {
                const auto timeNs = _anchorNs + static_cast<std::uint64_t>(std::llround(static_cast<double>(_samples + offset) / rate * 1e9));
                outputs[m].publishTag(gr::property_map{{std::string(gr::tag::kSampleRate.key()), sample_rate.value}, {std::string(gr::tag::kTriggerTime.key()), timeNs}, {std::string(gr::tag::kTriggerOffset.key()), 0.f}}, offset);
            }
            if (m < receivers.size()) {
                for (std::size_t i = 0UZ; i < n; ++i) {
                    const double t = static_cast<double>(_samples + i) / rate;
                    outputs[m][i]  = amplitude.value * static_cast<float>(clickAt(t, receivers[m])) + noise.value * _rng.uniformM11<float>();
                }
            } else {
                std::ranges::fill_n(outputs[m].begin(), static_cast<std::ptrdiff_t>(n), 0.f);
            }
            outputs[m].publish(n);
        }
        _untilTag = tagOffsets.empty() ? _untilTag - n : tagOffsets.back() + interval - n;
        _anchored = true;
        _samples += n;
        std::ignore = clkSpan.consume(n);
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_POINTSOURCEAUDIO_HPP
