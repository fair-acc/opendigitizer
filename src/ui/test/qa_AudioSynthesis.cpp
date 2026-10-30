#include "../blocks/GainEnvelope.hpp"
#include "../blocks/LowPassFilter.hpp"
#include "../blocks/SineOscillator.hpp"

#include <gnuradio-4.0/algorithm/Measurement.hpp>
#include <gnuradio-4.0/device/DeviceSpans.hpp>

#include <boost/ut.hpp>

using namespace boost::ut;
using namespace opendigitizer;

namespace {

template<typename TBlock>
[[nodiscard]] gr::property_map applyAndReturnRefusedParameters(TBlock& block, const gr::property_map& settings) {
    expect(block.settings().set(settings).empty()) << fatal << "every key is a known setting";
    expect(block.settings().activateContext() != std::nullopt) << fatal;
    return block.settings().applyStagedParameters().refusedParameters;
}

template<typename TBlock>
void applySettings(TBlock& block, const gr::property_map& settings) {
    expect(applyAndReturnRefusedParameters(block, settings).empty()) << fatal << "every value is accepted";
}

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

[[nodiscard]] bool isAllFinite(const auto& samples) {
    return std::ranges::all_of(samples, [](auto sample) { return std::isfinite(static_cast<double>(sample)); });
}

/// SineOscillator releases samples according to wall-clock time, so this polls processBulk() until @param nSamples
/// have been published. The sample values do not depend on how the samples are split across calls.
std::vector<float> processSamplesForSineOscillator(SineOscillator<float>& oscillator, std::size_t nSamples) {
    std::vector<float> result(nSamples);
    std::size_t        produced = 0UZ;
    while (produced < nSamples) {
        gr::device::DeviceSpanAccounting    accounting;
        gr::device::DeviceOutputSpan<float> span{._data = result.data() + produced, ._size = nSamples - produced, ._acct = &accounting};
        std::ignore = oscillator.processBulk(span);
        produced += accounting.published;
    }
    return result;
}

[[nodiscard]] double pitchOfAudioHz(std::span<const float> samples, double sampleRate) {
    const auto cyclesPerSample = gr::algorithm::measurement::frequency(samples);
    expect(cyclesPerSample.has_value()) << fatal << "window must contain a full period";
    return *cyclesPerSample * sampleRate;
}

[[nodiscard]] std::size_t numSamplesDuringTime(double seconds, float sampleRate) { //
    return static_cast<std::size_t>(std::lround(seconds * static_cast<double>(sampleRate)));
}

template<typename T>
[[nodiscard]] std::vector<double> processSamplesForEnvelope(GainEnvelope<T>& envelope, std::size_t nSamples, T input) {
    std::vector<double> volumes(nSamples);
    for (double& volume : volumes) {
        volume = static_cast<double>(envelope.processOne(input)) / static_cast<double>(input);
    }
    return volumes;
}

/// volumes are read back from float samples, so they match a double level only to float precision
constexpr double   kVolumeTolerance = 1e-6;
[[nodiscard]] bool isVolumeEqual(double volume, double level) { return std::abs(volume - level) < kVolumeTolerance; }

[[nodiscard]] std::size_t indexOfFirstSampleAtVolume(const std::vector<double>& volumes, double level) {
    return static_cast<std::size_t>(std::ranges::distance(volumes.begin(), std::ranges::find_if(volumes, [level](double v) { return isVolumeEqual(v, level); })));
}

/// feeds @param seconds of a unit-amplitude sine at @param frequency through the filter and returns the output amplitude.
/// The amplitude is derived from the RMS rather than the largest sample, which depends on where the samples land within
/// each period. This is exact when @param seconds holds a whole number of periods. @param phase keeps the input
/// continuous across calls
[[nodiscard]] double outputAmplitude(LowPassFilter<float>& filter, double frequency, double seconds, double& phase) {
    std::vector<float> outputs(numSamplesDuringTime(seconds, filter.sample_rate));
    for (float& output : outputs) {
        output = filter.processOne(static_cast<float>(std::sin(phase)));
        phase += 2. * std::numbers::pi * frequency / static_cast<double>(filter.sample_rate);
    }
    return std::numbers::sqrt2 * gr::algorithm::measurement::rms(std::span<const float>(outputs)).value_or(std::numeric_limits<double>::quiet_NaN());
}

} // namespace

const suite<"GainEnvelope"> gainEnvelopeTests = [] {
    "while pressed, the attack ramps linearly to full volume in the attack time"_test = [] {
        for (const float sampleRate : {8000.f, 48000.f}) {
            for (const double attack : {.001, .01, .1}) {
                GainEnvelope<float> envelope({{"sample_rate", sampleRate}, {"attack", attack}, {"pressed", true}});
                envelope.init(envelope.progress);
                const std::size_t attackSamples = numSamplesDuringTime(attack, sampleRate);
                const auto        volumes       = processSamplesForEnvelope(envelope, 2UZ * attackSamples, 1.f);
                const std::size_t peakIndex     = indexOfFirstSampleAtVolume(volumes, 1.);
                const auto        context       = std::format("attack {} s at {} Hz", attack, sampleRate);

                expect(peakIndex + 1UZ >= attackSamples && peakIndex <= attackSamples) << context << std::format(": full volume after {} samples", peakIndex + 1UZ);
                expect(std::ranges::is_sorted(volumes)) << context << ": volume never decreases during attack with no decay";
                expect(std::ranges::all_of(volumes | std::views::drop(peakIndex), [](double v) { return isVolumeEqual(v, 1.); })) << context << ": stays at full volume once reached";
            }
        }
    };

    "zero attack reaches full volume on the first sample"_test = [] {
        GainEnvelope<float> envelope({{"attack", 0.}, {"pressed", true}});
        envelope.init(envelope.progress);
        expect(eq(envelope.processOne(1.f), 1.f));
    };

    "unpressed envelope is silent"_test = [] {
        GainEnvelope<float> envelope({{"pressed", false}});
        envelope.init(envelope.progress);
        const auto volumes = processSamplesForEnvelope(envelope, 100UZ, 1.f);
        expect(std::ranges::all_of(volumes, [](double v) { return isVolumeEqual(v, 0.); }));
    };

    "after the attack, the decay falls to the sustain level in the decay time and holds it"_test = [] {
        constexpr float  kTestingSampleRate = 8000.f;
        constexpr double kTestingDecay      = .05;
        for (const double sustain : {0., .3, .9}) {
            GainEnvelope<float> envelope({{"sample_rate", kTestingSampleRate}, {"attack", 0.}, {"decay", kTestingDecay}, {"sustain_fraction", sustain}, {"pressed", true}});
            envelope.init(envelope.progress);
            expect(eq(envelope.processOne(1.f), 1.f)) << fatal << "zero attack";
            const std::size_t decaySamples = numSamplesDuringTime(kTestingDecay, kTestingSampleRate);
            const auto        volumes      = processSamplesForEnvelope(envelope, 2UZ * decaySamples, 1.f);
            const std::size_t sustainIndex = indexOfFirstSampleAtVolume(volumes, sustain);

            expect(sustainIndex + 1UZ >= decaySamples && sustainIndex <= decaySamples) << std::format("sustain {}: reached after {} samples", sustain, sustainIndex + 1UZ);
            expect(std::ranges::is_sorted(volumes, std::greater{})) << "volume never rises during decay";
            expect(std::ranges::all_of(volumes | std::views::drop(sustainIndex), [sustain](double v) { return isVolumeEqual(v, sustain); })) << "holds the sustain level while pressed";
        }
    };

    "raising the sustain level while held raises the volume to the new level"_test = [] {
        constexpr float     kTestingSampleRate = 8000.f;
        GainEnvelope<float> envelope({{"sample_rate", kTestingSampleRate}, {"attack", 0.}, {"decay", .01}, {"sustain_fraction", .2}, {"pressed", true}});
        envelope.init(envelope.progress);
        const auto settled = processSamplesForEnvelope(envelope, numSamplesDuringTime(.1, kTestingSampleRate), 1.f);
        expect(isVolumeEqual(settled.back(), .2)) << fatal << "settled at the initial sustain level";

        constexpr double kTestingRaisedSustain = .7;
        applySettings(envelope, {{"sustain_fraction", kTestingRaisedSustain}});
        const auto volumes = processSamplesForEnvelope(envelope, numSamplesDuringTime(.1, kTestingSampleRate), 1.f);
        expect(volumes.front() > settled.back()) << "starts rising immediately";
        expect(std::ranges::is_sorted(volumes));
        expect(isVolumeEqual(volumes.back(), kTestingRaisedSustain)) << "settles at the new sustain level without overshooting";
    };

    "after release, the volume falls from the sustain level to silence in the release time"_test = [] {
        constexpr float  kTestingSampleRate = 8000.f;
        constexpr double kTestingRelease    = .05;
        constexpr double kTestingSustain    = .6;
        for (const double attack : {0., .01}) { // the release should start from the same point regardless of how the sustain level was reached
            GainEnvelope<float> envelope({{"sample_rate", kTestingSampleRate}, {"attack", attack}, {"sustain_fraction", kTestingSustain}, {"decay", .01}, {"release", kTestingRelease}, {"pressed", true}});
            envelope.init(envelope.progress);
            expect(isVolumeEqual(processSamplesForEnvelope(envelope, numSamplesDuringTime(.1, kTestingSampleRate), 1.f).back(), kTestingSustain)) << fatal;

            applySettings(envelope, {{"pressed", false}});
            const std::size_t releaseSamples = numSamplesDuringTime(kTestingRelease, kTestingSampleRate);
            const auto        volumes        = processSamplesForEnvelope(envelope, 2UZ * releaseSamples, 1.f);
            const std::size_t silenceIndex   = indexOfFirstSampleAtVolume(volumes, 0.);
            expect(silenceIndex + 1UZ >= releaseSamples && silenceIndex <= releaseSamples) << std::format("attack {}: silent after {} samples", attack, silenceIndex + 1UZ);
            expect(std::ranges::is_sorted(volumes, std::greater{}));
        }
    };

    "releasing during the attack fades out sooner, and pressing again resumes from the current volume"_test = [] {
        constexpr float     kTestingSampleRate = 8000.f;
        constexpr double    kTestingAttack     = .1;
        constexpr double    kTestingRelease    = .1;
        GainEnvelope<float> envelope({{"sample_rate", kTestingSampleRate}, {"attack", kTestingAttack}, {"release", kTestingRelease}, {"pressed", true}});
        envelope.init(envelope.progress);
        const std::size_t numAttackSamples = numSamplesDuringTime(kTestingAttack, kTestingSampleRate);
        const double      releasedAt       = processSamplesForEnvelope(envelope, numAttackSamples / 4UZ, 1.f).back();
        expect(releasedAt > 0. && releasedAt < 1.) << fatal << "released mid-attack";

        applySettings(envelope, {{"pressed", false}});
        const auto        fadeOut                  = processSamplesForEnvelope(envelope, numAttackSamples / 16UZ, 1.f);
        const std::size_t numSamplesForFullRelease = numSamplesDuringTime(kTestingRelease, kTestingSampleRate);
        const double      volumeBefore             = fadeOut.back();
        expect(volumeBefore < releasedAt && volumeBefore > 0.) << fatal << "partially faded";
        expect(lt(static_cast<double>(indexOfFirstSampleAtVolume(processSamplesForEnvelope(envelope, numSamplesForFullRelease, 1.f), 0.)), static_cast<double>(numSamplesForFullRelease))) << "reaches silence before a full-volume release would";

        // press again after a partial release
        GainEnvelope<float> second({{"sample_rate", kTestingSampleRate}, {"attack", kTestingAttack}, {"release", kTestingRelease}, {"pressed", true}});
        second.init(second.progress);
        std::ignore = processSamplesForEnvelope(second, numAttackSamples / 2UZ, 1.f);
        applySettings(second, {{"pressed", false}});
        const double volumeAtRepress = processSamplesForEnvelope(second, numAttackSamples / 16UZ, 1.f).back();
        applySettings(second, {{"pressed", true}});
        const auto reattack = processSamplesForEnvelope(second, numAttackSamples, 1.f);
        expect(reattack.front() > volumeAtRepress) << "attack continues upward from the current volume instead of restarting from silence";
        expect(lt(static_cast<double>(indexOfFirstSampleAtVolume(reattack, 1.)), static_cast<double>(numAttackSamples))) << "reaches full volume sooner than a full attack";
    };

    "a sample rate of zero or below is refused"_test = [] {
        constexpr float kTestingSampleRate = 8000.f;
        for (const float badSampleRate : {0.f, -kTestingSampleRate, kNaN}) {
            const auto          context = std::format("sample_rate {}", badSampleRate);
            GainEnvelope<float> envelope({{"sample_rate", kTestingSampleRate}, {"attack", .01}, {"release", .01}, {"pressed", true}});
            envelope.init(envelope.progress);

            expect(applyAndReturnRefusedParameters(envelope, {{"sample_rate", badSampleRate}}).contains("sample_rate")) << context << ": refused";
            const auto allValuesAreUnitValues = [](const std::vector<double>& volumes) { return std::ranges::all_of(volumes, [](double v) { return v >= 0. && v <= 1.; }); };
            expect(allValuesAreUnitValues(processSamplesForEnvelope(envelope, numSamplesDuringTime(.1, kTestingSampleRate), 1.f))) << context << ": attack";
            std::ignore = applyAndReturnRefusedParameters(envelope, {{"pressed", false}});
            expect(allValuesAreUnitValues(processSamplesForEnvelope(envelope, numSamplesDuringTime(.1, kTestingSampleRate), 1.f))) << context << ": release";
        }
    };

    "int16 samples"_test = [] {
        constexpr float            kTestingSampleRate  = 8000.f;
        constexpr std::int16_t     kTestingInputSample = 10000;
        constexpr double           kTestingSustain     = .5;
        GainEnvelope<std::int16_t> envelope({{"sample_rate", kTestingSampleRate}, {"attack", 0.}, {"sustain_fraction", kTestingSustain}, {"decay", 0.}, {"pressed", true}});
        envelope.init(envelope.progress);
        expect(eq(envelope.processOne(kTestingInputSample), kTestingInputSample)) << "full volume after zero attack";
        expect(eq(envelope.processOne(kTestingInputSample), static_cast<std::int16_t>(kTestingSustain * kTestingInputSample))) << "sustain level after zero decay";
    };
};

const suite<"LowPassFilter"> lowPassFilterTests = [] {
    constexpr float  kTestingSampleRate             = 48000.f;
    constexpr double kTestingTransientLengthSeconds = .1; // amount to discard from sampling to ignore frequency variation after lowpass changes
    constexpr double kTestingMeasureSeconds         = .05;
    const auto       makeFilter                     = [=](float cutoff, float smoothing = 0.f) {
        auto filter = std::make_unique<LowPassFilter<float>>(gr::property_map{{"sample_rate", kTestingSampleRate}, {"cutoff", cutoff}, {"cutoff_smoothing_time", smoothing}});
        filter->init(filter->progress);
        filter->start();
        return filter;
    };
    const auto amplitudeOfFrequencyWithFilterApplied = [](LowPassFilter<float>& filter, double frequency) {
        double phase = 0.;
        std::ignore  = outputAmplitude(filter, frequency, kTestingTransientLengthSeconds, phase);
        return outputAmplitude(filter, frequency, kTestingMeasureSeconds, phase);
    };

    "passes frequencies below the cutoff, -3 dB at the cutoff, and attenuates above it"_test = [&] {
        for (const float cutoff : {200.f, 1000.f, 4000.f}) {
            const auto   context             = std::format("cutoff {} Hz", cutoff);
            const double frequencyAtPassband = amplitudeOfFrequencyWithFilterApplied(*makeFilter(cutoff), static_cast<double>(cutoff) / 10.);
            const double frequencyAtCutoff   = amplitudeOfFrequencyWithFilterApplied(*makeFilter(cutoff), static_cast<double>(cutoff));
            const double frequencyAtStopBand = amplitudeOfFrequencyWithFilterApplied(*makeFilter(cutoff), static_cast<double>(cutoff) * 4.);
            expect(approx(frequencyAtPassband, 1., .01)) << context << ": passband";
            expect(approx(frequencyAtCutoff, 1. / std::numbers::sqrt2, .02)) << context << ": half power at the cutoff";
            expect(lt(frequencyAtStopBand, std::pow(4., -4.) * 1.1)) << context << ": 4th order rolloff, at least 24 dB per octave";
        }
    };

    "DC passes unchanged"_test = [&] {
        auto            filter = makeFilter(100.f);
        float           output = 0.f;
        constexpr float kDC    = .75f;
        for (std::size_t i = 0UZ; i < numSamplesDuringTime(kTestingTransientLengthSeconds, kTestingSampleRate); ++i) {
            output = filter->processOne(kDC);
        }
        expect(approx(output, kDC, 1e-4f));
    };

    "out of range cutoffs behave like the nearest valid cutoff"_test = [&] {
        const auto sameOutput = [](LowPassFilter<float>& a, LowPassFilter<float>& b) {
            for (std::size_t i = 0UZ; i < 1000UZ; ++i) {
                const float input = static_cast<float>(std::sin(.3 * static_cast<double>(i)) + std::sin(2.9 * static_cast<double>(i)));
                if (a.processOne(input) != b.processOne(input)) {
                    return false;
                }
            }
            return true;
        };
        constexpr float kTestingMaxCutoff = .49f * kTestingSampleRate;
        expect(sameOutput(*makeFilter(kTestingMaxCutoff), *makeFilter(10.f * kTestingSampleRate))) << "above 0.49 * sample_rate";
        expect(sameOutput(*makeFilter(1.f), *makeFilter(0.f))) << "zero";
        expect(sameOutput(*makeFilter(1.f), *makeFilter(-100.f))) << "negative";
    };
};

const suite<"LowPassFilterInvalidSettings"> lowPassFilterInvalidSettingsTests = [] {
    constexpr float kTestingSampleRate        = 48000.f;
    const auto      areAllOutputsInValidRange = [](LowPassFilter<float>& filter) {
        std::vector<float> outputs(1000UZ);
        for (std::size_t i = 0UZ; i < outputs.size(); ++i) {
            outputs[i] = filter.processOne(static_cast<float>(std::sin(.3 * static_cast<double>(i)) + std::sin(2.9 * static_cast<double>(i))));
        }
        return std::ranges::all_of(outputs, [](float v) { return std::isfinite(v) && std::abs(v) < 10.f; });
    };

    "for any positive sample rate, bounded input gives bounded output"_test = [&] {
        for (const float lowSampleRate : {1.f, 1.5f, 2.f}) { // 0.49 * sample_rate is below the 1 Hz cutoff floor
            for (const float cutoff : {.25f, 1.f, 1000.f}) {
                LowPassFilter<float> filter({{"sample_rate", lowSampleRate}, {"cutoff", cutoff}});
                filter.init(filter.progress);
                filter.start();
                expect(areAllOutputsInValidRange(filter)) << std::format("sample_rate {}, cutoff {}", lowSampleRate, cutoff);
            }
        }
    };

    "a sample rate of zero or below is refused and filtering continues"_test = [&] {
        for (const float badSampleRate : {0.f, -kTestingSampleRate, kNaN}) {
            LowPassFilter<float> filter({{"sample_rate", kTestingSampleRate}, {"cutoff", 1000.f}});
            filter.init(filter.progress);
            filter.start();
            expect(applyAndReturnRefusedParameters(filter, {{"sample_rate", badSampleRate}}).contains("sample_rate")) << std::format("sample_rate {} refused", badSampleRate);
            expect(areAllOutputsInValidRange(filter)) << std::format("sample_rate {}", badSampleRate);
        }
    };

    "a NaN cutoff is refused and filtering continues"_test = [&] {
        LowPassFilter<float> filter({{"sample_rate", kTestingSampleRate}, {"cutoff", 1000.f}});
        filter.init(filter.progress);
        filter.start();
        expect(applyAndReturnRefusedParameters(filter, {{"cutoff", kNaN}}).contains("cutoff")) << "refused";
        expect(areAllOutputsInValidRange(filter)) << "while the NaN was sent";
        std::ignore = applyAndReturnRefusedParameters(filter, {{"cutoff", 1000.f}});
        expect(areAllOutputsInValidRange(filter)) << "after a valid cutoff is set again";
    };
};

const suite<"SineOscillator"> sineOscillatorTests = [] {
    constexpr double kTestingSampleRate = 48000.;
    const auto       makeSineOscillator = [=](double frequency, float portamento = 0.f, float amplitude = 1.f) {
        auto oscillator = std::make_unique<SineOscillator<float>>(gr::property_map{{"sample_rate", kTestingSampleRate}, {"frequency", frequency}, {"portamento", portamento}, {"amplitude", amplitude}});
        oscillator->init(oscillator->progress);
        oscillator->start();
        return oscillator;
    };

    "produces a sine with the set frequency and amplitude, starting at zero phase"_test = [&] {
        constexpr double kEpsilon = 1e-4;
        for (const double frequency : {50., 440., 5000.}) {
            for (const float amplitude : {.25f, 1.f}) {
                auto        oscillator = makeSineOscillator(frequency, 0.f, amplitude);
                const auto  samples    = processSamplesForSineOscillator(*oscillator, 2000UZ);
                std::size_t mismatches = 0UZ;
                for (std::size_t i = 0UZ; i < samples.size(); ++i) {
                    const double expected = static_cast<double>(amplitude) * std::sin(2. * std::numbers::pi * frequency * static_cast<double>(i) / static_cast<double>(kTestingSampleRate));
                    mismatches += std::abs(static_cast<double>(samples[i]) - expected) > kEpsilon ? 1UZ : 0UZ;
                }
                expect(eq(mismatches, 0UZ)) << std::format("{} Hz, amplitude {}", frequency, amplitude);
                oscillator->stop();
            }
        }
    };

    "publishes nothing and reports insufficient items when there is no room for output"_test = [&] {
        auto                                oscillator = makeSineOscillator(440.);
        gr::device::DeviceSpanAccounting    accounting;
        gr::device::DeviceOutputSpan<float> empty{._acct = &accounting};
        expect(oscillator->processBulk(empty) == gr::work::Status::INSUFFICIENT_INPUT_ITEMS);
        expect(eq(accounting.published, 0UZ));
        oscillator->stop();
    };

    constexpr double      kLowFrequency  = 440.;
    constexpr double      kHighFrequency = 880.;
    const double          midFrequency   = std::sqrt(kLowFrequency * kHighFrequency); // halfway in pitch
    constexpr std::size_t kSampleWindow  = 480UZ;                                     // 10 ms, several periods at these frequencies

    "without portamento, a frequency change applies immediately"_test = [&] {
        auto oscillator = makeSineOscillator(kLowFrequency);
        std::ignore     = processSamplesForSineOscillator(*oscillator, kSampleWindow);
        applySettings(*oscillator, {{"frequency", kHighFrequency}});
        expect(approx(pitchOfAudioHz(processSamplesForSineOscillator(*oscillator, kSampleWindow), kTestingSampleRate), kHighFrequency, .01 * kHighFrequency));
        oscillator->stop();
    };

    constexpr float   kTestingPortamento = .05f;
    const std::size_t glideSamples       = numSamplesDuringTime(static_cast<double>(kTestingPortamento), kTestingSampleRate);

    "with portamento, the pitch glides evenly to the new frequency over the portamento time"_test = [&] {
        auto oscillator = makeSineOscillator(kLowFrequency, kTestingPortamento);
        std::ignore     = processSamplesForSineOscillator(*oscillator, kSampleWindow);
        applySettings(*oscillator, {{"frequency", kHighFrequency}});
        const auto glide = processSamplesForSineOscillator(*oscillator, glideSamples + 2UZ * kSampleWindow);
        const auto span  = std::span<const float>(glide);

        const double start     = pitchOfAudioHz(span.first(kSampleWindow), kTestingSampleRate);
        const auto   midPeriod = static_cast<std::size_t>(kTestingSampleRate / midFrequency); // the measured period starts at the first rising crossing, so starting one period early makes it straddle the midpoint
        const double midpoint  = pitchOfAudioHz(span.subspan(glideSamples / 2UZ - midPeriod, kSampleWindow), kTestingSampleRate);
        const double end       = pitchOfAudioHz(span.subspan(glideSamples, kSampleWindow), kTestingSampleRate);
        expect(start > kLowFrequency && start < midFrequency) << std::format("starts near the old frequency: {}", start);
        expect(approx(midpoint, midFrequency, .02 * midFrequency)) << std::format("halfway in pitch at half the portamento time: {}", midpoint);
        expect(approx(end, kHighFrequency, .01 * kHighFrequency)) << std::format("reaches the new frequency: {}", end);
        oscillator->stop();
    };

    "a frequency change during a glide starts the new glide from the current pitch"_test = [&] {
        auto oscillator = makeSineOscillator(kLowFrequency, kTestingPortamento);
        applySettings(*oscillator, {{"frequency", kHighFrequency}});
        const auto   firstHalf   = processSamplesForSineOscillator(*oscillator, glideSamples / 2UZ);
        const double pitchBefore = pitchOfAudioHz(std::span<const float>(firstHalf).last(kSampleWindow / 3UZ), kTestingSampleRate);

        applySettings(*oscillator, {{"frequency", kLowFrequency}});
        const auto   reversed   = processSamplesForSineOscillator(*oscillator, glideSamples + kSampleWindow);
        const double pitchAfter = pitchOfAudioHz(std::span<const float>(reversed).first(kSampleWindow / 2UZ), kTestingSampleRate);
        expect(approx(pitchBefore, midFrequency, .05 * midFrequency)) << std::format("before the change: {}", pitchBefore);
        expect(approx(pitchAfter, midFrequency, .05 * midFrequency)) << std::format("no jump to either end of the glide after the change: {}", pitchAfter);
        expect(approx(pitchOfAudioHz(std::span<const float>(reversed).last(kSampleWindow), kTestingSampleRate), kLowFrequency, .01 * kLowFrequency)) << "reaches the new target";
        oscillator->stop();
    };

    "changing an unrelated setting during a glide does not restart the glide"_test = [&] {
        auto oscillator = makeSineOscillator(kLowFrequency, kTestingPortamento);
        applySettings(*oscillator, {{"frequency", kHighFrequency}});
        std::ignore = processSamplesForSineOscillator(*oscillator, glideSamples / 2UZ);
        applySettings(*oscillator, {{"amplitude", .5f}});
        std::ignore     = processSamplesForSineOscillator(*oscillator, glideSamples / 2UZ);
        const auto tail = processSamplesForSineOscillator(*oscillator, kSampleWindow);
        expect(approx(pitchOfAudioHz(tail, kTestingSampleRate), kHighFrequency, .01 * kHighFrequency)) << "the glide ends one portamento time after the frequency change";
        expect(approx(*std::ranges::max_element(tail), .5f, .01f)) << "the amplitude change applies";
        oscillator->stop();
    };

    "a frequency of zero or below is refused, and the oscillator keeps working"_test = [&] {
        for (const float glide : {0.f, kTestingPortamento}) {
            for (const double badFrequency : {0., -100., static_cast<double>(kNaN)}) {
                const auto context    = std::format("portamento {}, frequency {}", glide, badFrequency);
                auto       oscillator = makeSineOscillator(kLowFrequency, glide);
                std::ignore           = processSamplesForSineOscillator(*oscillator, kSampleWindow);

                expect(applyAndReturnRefusedParameters(*oscillator, {{"frequency", badFrequency}}).contains("frequency")) << context << ": refused";
                expect(isAllFinite(processSamplesForSineOscillator(*oscillator, kSampleWindow))) << context << ": while the bad frequency was sent";

                std::ignore      = applyAndReturnRefusedParameters(*oscillator, {{"frequency", kHighFrequency}});
                const auto after = processSamplesForSineOscillator(*oscillator, glideSamples + kSampleWindow);
                expect(isAllFinite(after)) << context << ": after a valid frequency is set again";
                if (isAllFinite(after)) {
                    expect(approx(pitchOfAudioHz(std::span<const float>(after).last(kSampleWindow), kTestingSampleRate), kHighFrequency, .01 * kHighFrequency)) << context << ": plays the valid frequency";
                }
                oscillator->stop();
            }
        }
    };

    "a sample rate of zero or below is refused"_test = [&] {
        for (const float badSampleRate : std::array{0.f, -static_cast<float>(kTestingSampleRate), kNaN}) {
            auto oscillator = makeSineOscillator(kLowFrequency);
            // generating with an accepted sample rate of zero would wait forever for wall-clock samples, so a non-refusal ends this test case
            expect(applyAndReturnRefusedParameters(*oscillator, {{"sample_rate", badSampleRate}}).contains("sample_rate")) << fatal << std::format("sample_rate {} refused", badSampleRate);
            expect(isAllFinite(processSamplesForSineOscillator(*oscillator, kSampleWindow))) << std::format("sample_rate {}", badSampleRate);
            oscillator->stop();
        }
    };
};

int main() { return 0; }
