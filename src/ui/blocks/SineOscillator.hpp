#ifndef OPENDIGITIZER_BLOCKS_SINE_OSCILLATOR_HPP
#define OPENDIGITIZER_BLOCKS_SINE_OSCILLATOR_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/BlockingSync.hpp>
#include <gnuradio-4.0/annotated.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::SineOscillator, [T], [float])

/// This block is intended to be used specifically for creating musical synthesizers, hence "oscillator" rather than "generator" in the name
template<typename T>
struct SineOscillator : gr::Block<SineOscillator<T>>, gr::BlockingSync<SineOscillator<T>> {
    using Description = gr::Doc<"Sine wave generator with portamento">;

    gr::PortOut<T> out;

    using NonNegativeFloat        = gr::Limits<0.f, std::numeric_limits<float>::max()>;
    static constexpr double twopi = 2. * std::numbers::pi;

    gr::Annotated<float, "sample_rate", gr::Visible, gr::Unit<"Hz">>                                                                                                sample_rate = 48000.f;
    gr::Annotated<gr::Size_t, "chunk_size", gr::Doc<"samples per update">>                                                                                          chunk_size  = 480;
    gr::Frequency                                                                                                                                                   frequency   = 440;
    gr::Annotated<float, "amplitude", gr::Visible>                                                                                                                  amplitude   = 1.f;
    gr::Annotated<float, "portamento", gr::Visible, gr::Unit<"s">, gr::Doc<"time required to change to the target pitch from the current pitch">, NonNegativeFloat> portamento  = 0.f;

    GR_MAKE_REFLECTABLE(SineOscillator, out, sample_rate, chunk_size, frequency, amplitude, portamento);

    double      _phaseRadians           = 0.;
    double      _glideStartFrequencyLn  = 0.; // do math on frequencies at log scale so that interpolation is perceived as linear
    std::size_t _samplesSinceGlideStart = 0UZ;

    void start() {
        _glideStartFrequencyLn  = std::log(static_cast<double>(frequency.value));
        _samplesSinceGlideStart = 0UZ;
        this->blockingSyncStart();
    }

    void stop() { this->blockingSyncStop(); }

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) {
        const std::size_t nSamples = this->syncSamples(output);
        if (nSamples == 0UZ) {
            output.publish(0UZ);
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS;
        }
        for (std::size_t i = 0UZ; i < nSamples; ++i) {
            output[i]                    = static_cast<T>(static_cast<double>(amplitude) * std::sin(_phaseRadians));
            const double targetFrequency = std::exp(portamentoInterpolate(frequency, static_cast<double>(portamento)));
            const double phaseIncrement  = twopi * targetFrequency / static_cast<double>(sample_rate);
            _phaseRadians                = std::fmod(_phaseRadians + phaseIncrement, twopi);
            ++_samplesSinceGlideStart;
        }
        output.publish(nSamples);
        return gr::work::Status::OK;
    }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& /*newSettings*/) {
        const double oldFrequency  = oldSettings.value_or<double>("frequency", frequency.value);
        const double oldPortamento = static_cast<double>(oldSettings.value_or<float>("portamento", portamento.value));
        if (oldFrequency == frequency && oldPortamento == portamento) {
            return;
        }
        // prevent jumping around since the correct location in the current slide will have changed
        _glideStartFrequencyLn = portamentoInterpolate(oldFrequency, oldPortamento); // reads _samplesSinceGlideStart

        _samplesSinceGlideStart = 0UZ;
    }

    [[nodiscard]] double portamentoInterpolate(double targetFrequency, double glideSeconds) const noexcept {
        const double glideSamples  = static_cast<double>(glideSeconds) * static_cast<double>(sample_rate);
        const double glideProgress = glideSamples <= 0. ? 1. : std::min(1., static_cast<double>(_samplesSinceGlideStart) / glideSamples);
        return std::lerp(_glideStartFrequencyLn, std::log(static_cast<double>(targetFrequency)), glideProgress);
    }
};

} // namespace opendigitizer

inline auto registerPortamentoSineOscillator = gr::registerBlock<opendigitizer::SineOscillator, float>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_BLOCKS_SINE_OSCILLATOR_HPP
