#ifndef OPENDIGITIZER_BLOCKS_LOW_PASS_FILTER_HPP
#define OPENDIGITIZER_BLOCKS_LOW_PASS_FILTER_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/annotated.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::LowPassFilter, [T], [float])

/// Lowpass filter which, unlike gr::filter::BasicFilter, has interpolation and does not reconfigure itself when
/// its settings change. This is useful for changing the cutoff value live / in real time. However, it lacks
/// features that BasicFilter has such as decimation and support for types other than float.
///
/// V. Zavalishin, "The Art of VA Filter Design", rev. 2.1.2, Native Instruments, 2020, ch. 4.
/// A. Simper, "Linear Trapezoidal Integrated State Variable Filter", Cytomic technical paper, 2013.
template<typename T>
struct LowPassFilter : gr::Block<LowPassFilter<T>> {
    using Description = gr::Doc<"4th-order Butterworth lowpass filter">;

    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    using NonNegative = gr::Limits<0.f, std::numeric_limits<float>::max()>;

    gr::Annotated<float, "sample_rate", gr::Visible, gr::Unit<"Hz">, gr::Doc<"sample rate of the input">>                                               sample_rate           = 48000.f;
    gr::Annotated<float, "cutoff", gr::Visible, gr::Unit<"Hz">, gr::Doc<"-3 dB frequency. Clamped to [1 Hz, 0.49 sample_rate]">>                        cutoff                = 1000.f;
    gr::Annotated<float, "cutoff_smoothing_time", gr::Visible, gr::Unit<"s">, gr::Doc<"time to reach new cutoff after a settings change">, NonNegative> cutoff_smoothing_time = .02f;

    GR_MAKE_REFLECTABLE(LowPassFilter, in, out, sample_rate, cutoff, cutoff_smoothing_time);

    struct SectionState {
        double ic1eq = 0.;
        double ic2eq = 0.;
    };

    // Q of the two second-order sections of a 4th-order Butterworth filter
    // - 1 / (2 cos(pi/8))
    // - 1 / (2 cos(3 pi/8))
    static constexpr std::array<double, 2> kSectionQ{0.54119610014619698, 1.3065629648763766};

    std::array<SectionState, 2> _sections{};
    double                      _cutoffFrequencyLn = 0.; // natural log of frequency

    void start() { _cutoffFrequencyLn = std::log(clampedCutoff()); }

    [[nodiscard]] T processOne(T input) noexcept {
        _cutoffFrequencyLn += glideFractionPerSample() * (std::log(clampedCutoff()) - _cutoffFrequencyLn);
        const double g = std::tan(std::numbers::pi * std::exp(_cutoffFrequencyLn) / static_cast<double>(sample_rate));

        double signal = static_cast<double>(input);
        for (std::size_t i = 0UZ; i < _sections.size(); ++i) {
            signal = lowPassSection(signal, g, 1. / kSectionQ[i], _sections[i]);
        }
        return static_cast<T>(signal);
    }

    [[nodiscard]] double clampedCutoff() const noexcept { return std::clamp(static_cast<double>(cutoff), 1., .49 * static_cast<double>(sample_rate)); }

    [[nodiscard]] double glideFractionPerSample() const noexcept {
        if (cutoff_smoothing_time <= 0.f) {
            return 1.;
        }
        return 1. - std::exp(-1. / (static_cast<double>(cutoff_smoothing_time) * static_cast<double>(sample_rate)));
    }

    /// One sample through a trapezoidal-integrated state-variable filter. Returns the output gain
    /// g == tan(pi * cutoff / sample_rate)
    /// k == is 1 / Q (the damping)
    [[nodiscard]] static double lowPassSection(double input, double g, double k, SectionState& state) noexcept {
        const double a1 = 1. / (1. + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = input - state.ic2eq;
        const double v1 = a1 * state.ic1eq + a2 * v3;
        const double v2 = state.ic2eq + a2 * state.ic1eq + a3 * v3;
        state.ic1eq     = 2. * v1 - state.ic1eq;
        state.ic2eq     = 2. * v2 - state.ic2eq;
        return v2;
    }
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_BLOCKS_LOW_PASS_FILTER_HPP
