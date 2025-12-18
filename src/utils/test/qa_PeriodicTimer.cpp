#include <boost/ut.hpp>

#include <chrono>
#include <filesystem>
#include <print>
#include <random>
#include <thread>

#include "../include/PeriodicTimer.hpp"
#include <gnuradio-4.0/Profiler.hpp>

using namespace std::chrono_literals;

namespace gr::profiling {

void simulateWork(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }

void simulateWorkJittered(std::chrono::milliseconds base, std::chrono::milliseconds jitter) {
    thread_local std::mt19937          gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, static_cast<int>(jitter.count()));
    std::this_thread::sleep_for(base + std::chrono::milliseconds(dist(gen)));
}

// Demonstrates basic labeled snapshots with relative timing
void basicLabeledSnapshots(Profiler& profiler, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), std::format("basic_loop#{}", std::this_thread::get_id()), "diag", 100ms, true};

    for (int i = 0; i < iterations; ++i) {
        tim.begin();
        simulateWork(5ms);
        tim.snapshot("blocked"); // relative to begin (previous)
        simulateWork(15ms);
        tim.snapshot("work"); // relative to blocked (previous)
        simulateWork(2ms);
        tim.snapshot("cleanup"); // relative to work (previous)
    }
    tim.flush();
}

// Demonstrates explicit reference indices
void explicitReferenceIndices(Profiler& profiler, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), "ref_loop", "diag", 100ms, true};

    for (int i = 0; i < iterations; ++i) {
        tim.begin(); // t[0]
        simulateWork(5ms);
        tim.snapshot("phase1", kPrevious); // t[1], duration = t[1] - t[0]
        simulateWork(10ms);
        tim.snapshot("phase2", kPrevious); // t[2], duration = t[2] - t[1]
        simulateWork(3ms);
        tim.snapshot("total", kBegin); // t[3], duration = t[3] - t[0] (cumulative)
    }
    tim.flush();
}

// Demonstrates user-defined metrics
void userMetrics(Profiler& profiler, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), "metrics_loop", "diag", 100ms, true};

    std::mt19937                          gen{42};
    std::uniform_real_distribution<float> cpu_dist(0.1f, 0.9f);
    std::uniform_int_distribution<int>    buf_dist(100, 10000);

    for (int i = 0; i < iterations; ++i) {
        tim.begin();

        const float cpu_usage = cpu_dist(gen);
        const int   buf_size  = buf_dist(gen);

        simulateWork(5ms);
        tim.snapshot("io");

        tim.metric("cpu", cpu_usage);
        tim.metric("buffer_size", buf_size);

        simulateWork(10ms);
        tim.snapshot("compute");
    }
    tim.flush();
}

// Demonstrates threshold alerts with predicates
void thresholdAlerts(Profiler& profiler, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), "alert_loop", "diag", 100ms, true};

    for (int i = 0; i < iterations; ++i) {
        tim.begin();

        // simulate variable work - some iterations will exceed threshold
        const auto work_time = (i % 5 == 0) ? 25ms : 8ms;

        simulateWork(5ms);
        tim.snapshot("blocked");

        simulateWork(work_time);
        tim.snapshot("work", kPrevious, [](auto d) { return d > 20ms; }); // alert if > 20ms

        // metric with threshold
        const float cpu = (i % 3 == 0) ? 0.95f : 0.5f;
        tim.metric("cpu", cpu, [](auto v) { return v > 0.9f; }); // alert if > 90%
    }
    tim.flush();
}

// Multi-threaded usage with independent timers per thread
void multiThreadedWorker(Profiler& profiler, int id, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), std::format("worker{}", id), "diag", 50ms, true};

    for (int i = 0; i < iterations; ++i) {
        tim.begin();
        simulateWorkJittered(std::chrono::milliseconds(3 + id), 2ms);
        tim.snapshot("acquire");
        simulateWorkJittered(std::chrono::milliseconds(5 + id), 3ms);
        tim.snapshot("process");
        tim.metric("items", 10 + id * 5 + (i % 3));
    }
    tim.flush();
}

void multiThreadedTest(Profiler& profiler) {
    std::jthread t1(multiThreadedWorker, std::ref(profiler), 0, 15);
    std::jthread t2(multiThreadedWorker, std::ref(profiler), 1, 12);
    std::jthread t3(multiThreadedWorker, std::ref(profiler), 2, 10);
}

// Demonstrates mixed absolute/relative snapshots
void mixedReferenceSnapshots(Profiler& profiler, int iterations) {
    thread_local static PeriodicTimer tim{profiler.forThisThread(), "mixed_ref", "diag", 100ms, true};

    for (int i = 0; i < iterations; ++i) {
        tim.begin(); // t[0]
        simulateWork(3ms);
        tim.snapshot("step1"); // t[1], relative to t[0]
        simulateWork(4ms);
        tim.snapshot("step2"); // t[2], relative to t[1]
        simulateWork(5ms);
        tim.snapshot("step3"); // t[3], relative to t[2]
        simulateWork(2ms);
        tim.snapshot("from_begin", kBegin); // t[4], relative to t[0]
        tim.snapshot("from_step1", 1);      // t[5], relative to t[1]
    }
    tim.flush();
}

const static boost::ut::suite<"PeriodicTimer"> periodicTimerTests = [] {
    using namespace boost::ut;

    "basic labeled snapshots"_test = [] {
        std::println(stderr, "\n=== Basic Labeled Snapshots ===");
        std::println(stderr, "Testing: snapshot(label) with relative timing\n");

        Profiler profiler{Options{}};
        basicLabeledSnapshots(profiler, 10);
    };

    "explicit reference indices"_test = [] {
        std::println(stderr, "\n=== Explicit Reference Indices ===");
        std::println(stderr, "Testing: snapshot(label, kBegin) for cumulative timing\n");

        Profiler profiler{Options{}};
        explicitReferenceIndices(profiler, 10);
    };

    "user-defined metrics"_test = [] {
        std::println(stderr, "\n=== User-Defined Metrics ===");
        std::println(stderr, "Testing: metric(label, value) for non-timing data\n");

        Profiler profiler{Options{}};
        userMetrics(profiler, 15);
    };

    "threshold alerts"_test = [] {
        std::println(stderr, "\n=== Threshold Alerts ===");
        std::println(stderr, "Testing: snapshot/metric with predicate for alerts\n");

        Profiler profiler{Options{}};
        thresholdAlerts(profiler, 12);
    };

    "multi-threaded timers"_test = [] {
        std::println(stderr, "\n=== Multi-Threaded Timers ===");
        std::println(stderr, "Testing: thread_local static with multiple workers\n");

        Profiler profiler{Options{}};
        multiThreadedTest(profiler);
    };

    "mixed reference snapshots"_test = [] {
        std::println(stderr, "\n=== Mixed Reference Snapshots ===");
        std::println(stderr, "Testing: combination of relative and absolute references\n");

        Profiler profiler{Options{}};
        mixedReferenceSnapshots(profiler, 8);
    };

    "null profiler no-ops"_test = [] {
        std::println(stderr, "\n=== Null Profiler No-ops ===");

        null::Profiler                    profiler;
        thread_local static PeriodicTimer tim{profiler.forThisThread(), "null_loop", "diag", 1ms};

        for (int i = 0; i < 100; ++i) {
            tim.begin();
            tim.snapshot("test");
            tim.metric("val", 42);
        }
        tim.flush();

        std::println(stderr, "Null profiler test completed (no output expected)\n");
    };

    "uninitialized timer safety"_test = [] {
        std::println(stderr, "\n=== Uninitialized Timer Safety ===");

        PeriodicTimer tim;
        tim.begin();
        tim.snapshot("test");
        tim.snapshot();
        tim.metric("val", 123);
        tim.flush();

        std::println(stderr, "Uninitialized timer test completed (no crash)\n");
    };

    "auto-incrementing indices"_test = [] {
        std::println(stderr, "\n=== Auto-Incrementing Indices ===");
        std::println(stderr, "Testing: snapshot() with no arguments\n");

        Profiler                          profiler{Options{}};
        thread_local static PeriodicTimer tim{profiler.forThisThread(), "auto_idx", "diag", 50ms, true};

        for (int i = 0; i < 8; ++i) {
            tim.begin();
            simulateWork(2ms);
            tim.snapshot(); // s0
            simulateWork(3ms);
            tim.snapshot(); // s1
            simulateWork(4ms);
            tim.snapshot(); // s2
        }
        tim.flush();
    };

    "high-frequency iterations"_test = [] {
        std::println(stderr, "\n=== High-Frequency Iterations ===");
        std::println(stderr, "Testing: many iterations with short flush interval\n");

        Profiler                          profiler{Options{}};
        thread_local static PeriodicTimer tim{profiler.forThisThread(), "high_freq", "diag", 20ms, true};

        for (int i = 0; i < 50; ++i) {
            tim.begin();
            simulateWork(1ms);
            tim.snapshot("fast_op");
        }
        tim.flush();
    };
};

} // namespace gr::profiling

int main() { /* tests run via ut */ }
