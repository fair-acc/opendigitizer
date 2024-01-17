#include "datasource.h"

#include <fmt/format.h>
#include <math.h>
#include <mutex>

#include <fmt/format.h>
#include <gnuradio-4.0/Block.hpp>

#include "../flowgraph.h"

template<typename T>
    requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>> {
    gr::PortOut<T>   out{};
    float            val       = 0;
    float            frequency = 1.f;
    std::mutex       mutex;
    std::deque<T>    samples;
    std::thread      thread;
    std::atomic_bool quit = false;

    SineSource()
        : thread([this]() {
            using namespace std::chrono_literals;
            while (!quit) {
                const double sec = double(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count()) * 1e-6;

                mutex.lock();
                samples.push_back(T(std::sin(sec * double(frequency) * double(2. * M_PI))));
                mutex.unlock();

                std::this_thread::sleep_for(20ms);
            }
        }) {
    }

    ~SineSource() {
        quit = true;
        thread.join();
    }

    T processOne() {
        std::lock_guard guard(mutex);
        assert(!samples.empty());
        if (samples.size() == 1) {
            this->requestStop();
        }
        T v = samples.front();
        samples.pop_front();
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
