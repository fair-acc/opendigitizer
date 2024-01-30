#include "datasource.h"

#include <fmt/format.h>
#include <math.h>
#include <mutex>

#include <fmt/format.h>
#include <gnuradio-4.0/Block.hpp>

#include "../flowgraph.h"

template<typename T>
    requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>, gr::BlockingIO<true>> {
    gr::PortOut<T>   out{};
    float            val       = 0;
    float            frequency = 1.f;
    std::mutex       mutex;
    std::condition_variable conditionvar;
    std::deque<T>    samples;
    std::thread      thread;
    std::atomic_bool quit = false;

    SineSource()
        : thread([this]() {
            using namespace std::chrono_literals;
            while (!quit) {
                {
                    std::unique_lock lock(mutex);
                    const double     sec = double(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count()) * 1e-6;
                    samples.push_back(T(std::sin(sec * double(frequency) * double(2. * M_PI))));

                    out.max_samples = samples.size();
                    conditionvar.notify_all();
                }

                std::this_thread::sleep_for(20ms);
            }
        }) {
    }

    ~SineSource() {
        quit = true;
        thread.join();
    }

    T processOne() {
        std::unique_lock guard(mutex);
        if (samples.size() == 0) {
            conditionvar.wait(guard);
        }

        T v = samples.front();
        samples.pop_front();
        out.max_samples = std::max<int>(1, samples.size());
        return v;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (SineSource<T>), out, frequency);
static_assert(gr::traits::block::can_processOne<SineSource<float>>);
static_assert(gr::traits::block::can_processOne<SineSource<double>>);

namespace DigitizerUi {

void DataSource::registerBlockType() {
    BlockType::registry().addBlockType<SineSource>("sine_source");
}

} // namespace DigitizerUi
