#include "../include/Xoshiro256pp.hpp"

#include <boost/ut.hpp>

#include <cmath>
#include <numeric>
#include <vector>

using namespace opendigitizer;

const boost::ut::suite<"Xoshiro256pp"> tests = [] {
    using namespace boost::ut;

    "same seed produces identical sequence"_test = [] {
        Xoshiro256pp rng1(42);
        Xoshiro256pp rng2(42);
        for (int i = 0; i < 1000; ++i) {
            expect(rng1.nextU64() == rng2.nextU64());
        }
    };

    "different seeds produce different sequences"_test = [] {
        Xoshiro256pp rng1(1);
        Xoshiro256pp rng2(2);
        bool         anyDifferent = false;
        for (int i = 0; i < 10; ++i) {
            if (rng1.nextU64() != rng2.nextU64()) {
                anyDifferent = true;
                break;
            }
        }
        expect(anyDifferent);
    };

    "uniform01 double is in [0, 1)"_test = [] {
        Xoshiro256pp rng(123);
        for (int i = 0; i < 10000; ++i) {
            double v = rng.uniform01<double>();
            expect(v >= 0.0_d);
            expect(v < 1.0_d);
        }
    };

    "uniform01 float is in [0, 1)"_test = [] {
        Xoshiro256pp rng(456);
        for (int i = 0; i < 10000; ++i) {
            float v = rng.uniform01<float>();
            expect(v >= 0.0_f);
            expect(v < 1.0_f);
        }
    };

    "uniformM11 is in [-1, 1)"_test = [] {
        Xoshiro256pp rng(789);
        for (int i = 0; i < 10000; ++i) {
            double v = rng.uniformM11();
            expect(v >= -1.0_d);
            expect(v < 1.0_d);
        }
    };

    "triangularM11 is in [-1, 1)"_test = [] {
        Xoshiro256pp rng(101);
        for (int i = 0; i < 10000; ++i) {
            double v = rng.triangularM11();
            expect(v >= -1.0_d);
            expect(v < 1.0_d);
        }
    };

    "uniform01 mean converges to 0.5"_test = [] {
        Xoshiro256pp  rng(42);
        constexpr int N   = 100000;
        double        sum = 0.0;
        for (int i = 0; i < N; ++i) {
            sum += rng.uniform01<double>();
        }
        double mean = sum / static_cast<double>(N);
        expect(std::abs(mean - 0.5) < 0.01_d) << "mean=" << mean;
    };

    "uniformM11 mean converges to 0"_test = [] {
        Xoshiro256pp  rng(42);
        constexpr int N   = 100000;
        double        sum = 0.0;
        for (int i = 0; i < N; ++i) {
            sum += rng.uniformM11();
        }
        double mean = sum / static_cast<double>(N);
        expect(std::abs(mean) < 0.01_d) << "mean=" << mean;
    };

    "triangularM11 mean converges to 0"_test = [] {
        Xoshiro256pp  rng(42);
        constexpr int N   = 100000;
        double        sum = 0.0;
        for (int i = 0; i < N; ++i) {
            sum += rng.triangularM11();
        }
        double mean = sum / static_cast<double>(N);
        expect(std::abs(mean) < 0.01_d) << "mean=" << mean;
    };

    "constexpr evaluation"_test = [] {
        constexpr auto val = [] {
            Xoshiro256pp rng(42);
            return rng.nextU64();
        }();
        expect(val != 0_u);
    };

    "splitmix64 avoids zero state"_test = [] {
        Xoshiro256pp rng(0); // seed=0 should still produce non-zero state
        bool         anyNonZero = false;
        for (auto v : rng._s) {
            if (v != 0) {
                anyNonZero = true;
            }
        }
        expect(anyNonZero);
    };
};

int main() { /* not needed for ut */ }
