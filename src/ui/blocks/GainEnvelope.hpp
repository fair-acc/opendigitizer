#ifndef OPENDIGITIZER_BLOCKS_GAIN_ENVELOPE_HPP
#define OPENDIGITIZER_BLOCKS_GAIN_ENVELOPE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/annotated.hpp>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>

namespace opendigitizer {

template<typename T>
concept AudioSample = std::same_as<T, float> || std::same_as<T, std::int16_t>;

GR_REGISTER_BLOCK(opendigitizer::GainEnvelope, [T], [ float, int16_t ])

template<AudioSample T>
struct GainEnvelope : gr::Block<GainEnvelope<T>> {
    using Description = gr::Doc<"Scales input values by an attack/decay/sustain/release envelope, according to the \"pressed\" trigger setting">;

    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    using Fraction       = gr::Limits<0., 1.>;
    using PositiveDouble = gr::Limits<0., std::numeric_limits<double>::max()>;
    using Seconds        = gr::Unit<"seconds">;

    // TODO: in the future, it would be ideal to be accepting tags on an event port or something like that
    gr::Annotated<bool, "pressed", gr::Visible, gr::Doc<"whether the key is considered pressed">> pressed = false;

    // time passed for the envelope is derived from sample_rate and number of samples
    gr::Annotated<float, "sample_rate", gr::Visible, gr::Unit<"Hz">, gr::Doc<"sample rate of the input signal">>                                                                                               sample_rate      = 48000.f;
    gr::Annotated<double, "attack", gr::Visible, gr::Doc<"time in seconds to reach max amplitude (1) from zero">, PositiveDouble, Seconds>                                                                     attack           = .1;
    gr::Annotated<double, "decay", gr::Visible, gr::Doc<"time to decrease to sustain amplitude after reaching the max amplitude">, PositiveDouble, Seconds>                                                    decay            = 0.;
    gr::Annotated<double, "sustain_fraction", gr::Visible, gr::Doc<"portion of amplitude at which the signal remains when held toggled on">, Fraction>                                                         sustain_fraction = 1.;
    gr::Annotated<double, "release", gr::Visible, gr::Doc<"time to fade out from sustain volume. If the trigger is released early, at a lower volume, then release will be shorter">, PositiveDouble, Seconds> release          = 0.;

    GR_MAKE_REFLECTABLE(GainEnvelope, in, out, pressed, sample_rate, attack, decay, sustain_fraction, release);

    double _current_volume_fraction              = 0.;
    bool   _has_seen_max_amplitude_since_pressed = false;

    // returns the fractional volume change per sample needed to create a linear volume ramp between two volumes @param distance apart within @param seconds time window
    [[nodiscard]] constexpr double changeNeededInVolumeFractionPerSample(double distance, double seconds) const noexcept {
        if (seconds <= 0.) {
            return 1.; // 1 == 100%, ie. changes completely and instantly
        }
        return (distance > 0. ? distance : 1.) / (seconds * static_cast<double>(sample_rate));
    }

    [[nodiscard]] constexpr T processOne(T input) noexcept {
        if (pressed) {
            if (!_has_seen_max_amplitude_since_pressed) { // attack
                _current_volume_fraction              = std::min(1., _current_volume_fraction + changeNeededInVolumeFractionPerSample(1., attack));
                _has_seen_max_amplitude_since_pressed = _current_volume_fraction == 1.;
            } else { // decay and sustain
                const double decayDelta = changeNeededInVolumeFractionPerSample(1. - sustain_fraction, decay);
                if (_current_volume_fraction > sustain_fraction) {
                    _current_volume_fraction = std::max(static_cast<double>(sustain_fraction), _current_volume_fraction - decayDelta);
                } else {
                    _current_volume_fraction = std::min(static_cast<double>(sustain_fraction), _current_volume_fraction + decayDelta);
                }
            }
        } else { // release
            _has_seen_max_amplitude_since_pressed = false;
            _current_volume_fraction              = std::max(0., _current_volume_fraction - changeNeededInVolumeFractionPerSample(sustain_fraction, release));
        }

        return static_cast<T>(_current_volume_fraction * static_cast<double>(input));
    }
};

} // namespace opendigitizer

inline auto registerGainEnvelope = gr::registerBlock<opendigitizer::GainEnvelope, float, int16_t>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_BLOCKS_GAIN_ENVELOPE_HPP
