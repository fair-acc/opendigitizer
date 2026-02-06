#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../blocks/TestSpectrumGenerator.hpp"

#include <boost/ut.hpp>

#include <algorithm>
#include <cmath>

using namespace opendigitizer;

const boost::ut::suite<"TestSpectrumGenerator"> tests = [] {
    using namespace boost::ut;

    "spectrum has correct size and axis layout"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size    = 512U;
        gen.center_freq      = 100e6f;
        gen.signal_bandwidth = 1e6f;
        gen.clock_rate       = 25.f;
        gen.seed             = 42ULL;
        gen.start();

        auto ds = gen.createSpectrum(512);
        expect(ds.axis_values.size() == 1_u);
        expect(ds.axis_values[0].size() == 512_u);
        expect(ds.signal_values.size() == 512_u);

        float fMin = ds.axis_values[0].front();
        float fMax = ds.axis_values[0].back();
        expect(fMin > 99.4e6f);
        expect(fMax < 100.6e6f);
    };

    "noise floor level is within expected range"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size           = 1024U;
        gen.center_freq             = 100e6f;
        gen.signal_bandwidth        = 1e6f;
        gen.clock_rate              = 25.f;
        gen.seed                    = 42ULL;
        gen.noise_floor_db          = -80.f;
        gen.noise_spread_db         = 0.2f;
        gen.show_schottky           = false;
        gen.show_interference_lines = false;
        gen.show_sweep_line         = false;
        gen.active_duration         = 10.f;
        gen.pause_duration          = 0.f;
        gen.start();

        auto   ds         = gen.createSpectrum(1024);
        auto   magnitudes = ds.signalValues(0);
        double sum        = 0.0;
        for (auto v : magnitudes) {
            sum += static_cast<double>(v);
        }
        double mean = sum / static_cast<double>(magnitudes.size());
        expect(std::abs(mean - (-80.0)) < 2.0) << "mean=" << mean;
    };

    "Schottky peak rises above noise floor"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size           = 1024U;
        gen.center_freq             = 100e6f;
        gen.signal_bandwidth        = 1e6f;
        gen.clock_rate              = 25.f;
        gen.seed                    = 42ULL;
        gen.noise_floor_db          = -80.f;
        gen.show_schottky           = true;
        gen.initial_peak_db         = 20.f;
        gen.show_interference_lines = false;
        gen.show_sweep_line         = false;
        gen.active_duration         = 10.f;
        gen.pause_duration          = 0.f;
        gen.start();

        // advance a few samples into the active phase
        gen._sampleCount = 50;
        auto  ds         = gen.createSpectrum(1024);
        auto  magnitudes = ds.signalValues(0);
        float peakVal    = *std::max_element(magnitudes.begin(), magnitudes.end());
        expect(peakVal > -70.f) << "peak should be well above noise floor, got " << peakVal;
    };

    "interference lines appear at expected positions"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size           = 4096U;
        gen.center_freq             = 100e6f;
        gen.signal_bandwidth        = 1e6f;
        gen.clock_rate              = 25.f;
        gen.seed                    = 42ULL;
        gen.noise_floor_db          = -80.f;
        gen.line_amplitude_db       = 20.f;
        gen.show_schottky           = false;
        gen.show_interference_lines = true;
        gen.show_sweep_line         = false;
        gen.active_duration         = 10.f;
        gen.pause_duration          = 0.f;
        gen.start();

        auto ds         = gen.createSpectrum(4096);
        auto magnitudes = ds.signalValues(0);

        // lines are at positions 0.12, 0.25, 0.85 of spectrum
        for (double pos : {0.12, 0.25, 0.85}) {
            auto bin = static_cast<std::size_t>(pos * 4096.0);
            expect(magnitudes[bin] > -70.f) << "interference line at pos=" << pos << " bin=" << bin;
        }
    };

    "morse keying toggles third interference line"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size       = 1024U;
        gen.center_freq         = 100e6f;
        gen.signal_bandwidth    = 1e6f;
        gen.clock_rate          = 1.f;
        gen.seed                = 42ULL;
        gen.morse_pattern       = "-";
        gen.morse_unit_duration = 1.f;
        gen.start();

        // "dash" = 3 ON units + 1 OFF unit + 6 OFF word gap = 10 units total
        expect(gen.isMorseKeyOn(0.0) == true);
        expect(gen.isMorseKeyOn(2.5) == true);  // still in the dash
        expect(gen.isMorseKeyOn(4.0) == false); // past the dash+gap
    };

    "reproducibility with same seed"_test = [] {
        auto makeGen = [] {
            TestSpectrumGenerator<float> gen;
            gen.spectrum_size           = 256U;
            gen.center_freq             = 100e6f;
            gen.signal_bandwidth        = 1e6f;
            gen.clock_rate              = 25.f;
            gen.seed                    = 42ULL;
            gen.show_schottky           = true;
            gen.show_interference_lines = true;
            gen.show_sweep_line         = false;
            gen.active_duration         = 10.f;
            gen.pause_duration          = 0.f;
            gen.start();
            return gen;
        };

        auto gen1 = makeGen();
        auto gen2 = makeGen();
        auto ds1  = gen1.createSpectrum(256);
        auto ds2  = gen2.createSpectrum(256);
        auto m1   = ds1.signalValues(0);
        auto m2   = ds2.signalValues(0);

        expect(m1.size() == m2.size());
        for (std::size_t i = 0; i < m1.size(); ++i) {
            expect(m1[i] == m2[i]) << "mismatch at bin " << i;
        }
    };

    "pause phase produces noise-only spectrum"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.spectrum_size           = 512U;
        gen.center_freq             = 100e6f;
        gen.signal_bandwidth        = 1e6f;
        gen.clock_rate              = 1.f;
        gen.seed                    = 42ULL;
        gen.noise_floor_db          = -80.f;
        gen.noise_spread_db         = 0.2f;
        gen.show_schottky           = true;
        gen.initial_peak_db         = 30.f;
        gen.show_interference_lines = false;
        gen.show_sweep_line         = false;
        gen.active_duration         = 1.f;
        gen.pause_duration          = 10.f;
        gen.start();

        // advance into the pause phase: active=1s, clock_rate=1Hz, so sample 2 is at t=2s (in pause)
        gen._sampleCount = 2;
        auto  ds         = gen.createSpectrum(512);
        auto  magnitudes = ds.signalValues(0);
        float maxVal     = *std::max_element(magnitudes.begin(), magnitudes.end());
        expect(maxVal < -75.f) << "pause phase should be noise only, max=" << maxVal;
    };

    "rebuildMorseKey handles empty pattern"_test = [] {
        TestSpectrumGenerator<float> gen;
        gen.morse_pattern = "";
        gen.start();

        expect(gen.isMorseKeyOn(0.0) == true);
        expect(gen.isMorseKeyOn(100.0) == true);
    };
};

int main() { /* not needed for ut */ }
