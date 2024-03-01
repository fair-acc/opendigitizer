
#ifndef OPENDIGITIZER_SINESOURCE_HPP
#define OPENDIGITIZER_SINESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>

#include <mutex>
#include <string>

namespace opendigitizer {

template<typename T>
    requires std::is_arithmetic_v<T>
struct SineSource : public gr::Block<SineSource<T>, gr::BlockingIO<true>> {
    gr::MsgPortIn           freqIn;
    gr::MsgPortOut          freqOut;
    gr::PortOut<T>          out{};
    float                   val       = 0;
    float                   frequency = 1.f;
    std::mutex              mutex;
    std::condition_variable conditionvar;
    std::deque<T>           samples;
    std::thread             thread;
    std::atomic_bool        quit = false;

    void
    settingsChanged(const gr::property_map &oldSettings, const gr::property_map &newSettings) {
        // TODO: enable and fix settings update
        // fmt::println("{}::settingsChanged(..) - called", this->name); // N.B. for debugging purposes to see how often setting changes are forced
        if (newSettings.contains("frequency") && newSettings.at("frequency") != oldSettings.at("frequency")) {
            using namespace std::string_literals;
            fmt::println("{}::settingsChanged(..): frequency to: {}", this->name, frequency);
            this->emitMessage(freqOut, { { "frequency"s, frequency } });
        }
    }

    void
    processMessages(auto &, std::span<const gr::Message> message) {
        fmt::print("received Message: {}", message.size());
        std::ranges::for_each(message, [this](auto &m) {
            if (m.contains("frequency")) {
                using namespace std::string_literals;
                const auto newFrequency = std::get<float>(m.at("frequency"));
                fmt::println("{}::processMessages(..): changed frequency to: {}", this->name, newFrequency);
                this->settings().set({ { "frequency"s, newFrequency } });
            }
        });
    }

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

                std::this_thread::sleep_for(20ms);
            }
        });
    }

    void stop() {
        std::unique_lock guard(mutex);
        quit = true;
        thread.join();
        conditionvar.notify_all();
    }

    auto processBulk(gr::PublishableSpan auto &output) {
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

        std::copy(samples.begin(), samples.begin() + n, output.begin());
        samples.erase(samples.begin(), samples.begin() + n);
        output.publish(n);
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::SineSource, out, freqIn, freqOut, frequency)

#endif
