#ifndef OPENDIGITIZER_SINESOURCE_HPP
#define OPENDIGITIZER_SINESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>

#include <mutex>
#include <string>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::SineSource, [ float, double ]);
template<typename T>
requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>, gr::BlockingIO<true>> {
    using super_t = gr::Block<SineSource<T>, gr::BlockingIO<true>>;
    gr::MsgPortIn  freqIn;
    gr::MsgPortOut freqOut;
    gr::PortOut<T> out{};
    float          val = 0;

    gr::Annotated<float, "frequency", gr::Unit<"Hz">, gr::Visible> frequency{1.f};

    std::mutex              mutex;
    std::condition_variable conditionvar;
    std::deque<T>           samples;
    std::thread             thread;
    std::atomic_bool        quit = false;

    GR_MAKE_REFLECTABLE(SineSource, out, freqIn, freqOut, frequency);

    void start() {
        thread = std::thread([this]() {
            using namespace std::chrono_literals;
            while (!quit) {
                {
                    std::unique_lock lock(mutex);
                    const double     sec = double(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count()) * 1e-6;
                    samples.push_back(T(std::sin(sec * double(frequency) * double(2. * M_PI))));

                    out.max_samples = samples.size();
                    conditionvar.notify_all();
                }

                std::this_thread::sleep_for(2ms);
            }
        });
    }

    void stop() {
        std::unique_lock guard(mutex);
        quit = true;
        thread.join();
        conditionvar.notify_all();
    }

    auto processBulk(gr::OutputSpanLike auto& output) {
        // technically, this wouldn't have to block, but could just publish 0 samples,
        // but keep it as test case for BlockingIO<true>.
        std::unique_lock guard(mutex);
        while (samples.empty() && !quit) {
            conditionvar.wait(guard);
        }

        const auto n = std::min(output.size(), samples.size());
        if (n == 0) {
            output.publish(0);
            return gr::work::Status::OK;
        }

        std::copy(samples.begin(), samples.begin() + cast_to_signed(n), output.begin());
        samples.erase(samples.begin(), samples.begin() + cast_to_signed(n));
        output.publish(n);
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

auto registerSineSource = gr::registerBlock<opendigitizer::SineSource, float>(gr::globalBlockRegistry());

#endif
