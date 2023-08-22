#include "datasource.h"

#include <fmt/format.h>
#include <math.h>
#include <mutex>

#include <node.hpp>

template<typename T>
struct SineSource : public fair::graph::node<SineSource<T>> {
    fair::graph::OUT<T> out{};
    float               val       = 0;
    float               frequency = 1.f;
    std::mutex          mutex;
    std::deque<T>       samples;
    std::jthread        thread;

    SineSource()
        : thread([this](std::stop_token stoken) {
            using namespace std::chrono_literals;
            while (!stoken.stop_requested()) {
                double sec = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now()).time_since_epoch().count() * 1e-6;

                mutex.lock();
                samples.push_back(std::sin(sec * double(frequency) * (2. * M_PI)));
                mutex.unlock();

                std::this_thread::sleep_for(20ms);
            }
        }) {
    }

    void settings_changed(const fair::graph::property_map &old_settings, const fair::graph::property_map &new_settings) noexcept {
        // frequency = new_settings["frequency"];
    }

    std::make_signed_t<std::size_t>
    available_samples(const SineSource & /*d*/) noexcept {
        std::lock_guard lock(mutex);
        const auto      ret = samples.size();
        return ret > 0 ? ret : -1;
    }

    T
    process_one() {
        std::lock_guard guard(mutex);

        T               v = samples.front();
        samples.pop_front();
        return v;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (SineSource<T>), out, frequency);

namespace DigitizerUi {

namespace {
BlockType *g_blockType = nullptr;
}

DataSource::DataSource(std::string_view name)
    : Block(name, "sine_source", g_blockType) {
    m_data.resize(8192);
    // parameters()

    // initialize the parameters
    SineSource<float> ss;
    ss.settings().update_active_parameters();
    m_parameters = ss.settings().get();

    for (auto &p : m_parameters)
        fmt::print("pp {} {}\n", p.first, p.second);

    // processData();
}

// void DataSource::processData() {
//     float freq = std::get<NumberParameter<float>>(parameters()[0]).value;
//     for (int i = 0; i < m_data.size(); ++i) {
//         m_data[i] = std::sin((m_offset + i) * freq);
//     }
//     outputs()[0].dataSet = m_data;
//     m_offset += 1;
// }

std::unique_ptr<fair::graph::node_model> DataSource::createGraphNode() {
    return std::make_unique<fair::graph::node_wrapper<SineSource<float>>>();
}

void DataSource::registerBlockType() {
    auto t = std::make_unique<BlockType>("sine_source", "Sine wave", "Local signals", true);
    t->outputs.resize(1);
    t->outputs[0].name = "out";
    t->outputs[0].type = "float";
    t->parameters.push_back({ "frequency", "frequency", BlockType::NumberParameter<float>(0.1) });
    t->createBlock = [](std::string_view n) {
        static int created = 0;
        ++created;
        if (n.empty()) {
            std::string name = fmt::format("sine source {}", created);
            return std::make_unique<DataSource>(name);
        }
        return std::make_unique<DataSource>(n);
    };
    g_blockType = t.get();

    BlockType::registry().addBlockType(std::move(t));
}

} // namespace DigitizerUi
