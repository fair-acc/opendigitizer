#ifndef OPENDIGITIZER_TESTSOURCE_HPP
#define OPENDIGITIZER_TESTSOURCE_HPP

#include <gnuradio-4.0/Block.hpp>

namespace opendigitizer {

template<typename T>
struct TestSource : public gr::Block<TestSource<T>> {
    using clock      = std::chrono::system_clock;
    using time_point = clock::time_point;
    gr::PortOut<T>            out;
    float                     sample_rate = 20000;
    std::size_t               _produced   = 0;
    std::optional<time_point> _start;

    GR_MAKE_REFLECTABLE(TestSource, out, sample_rate);

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& /*new_settings*/) { _produced = 0; }

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        using enum gr::work::Status;
        auto       n   = output.size();
        const auto now = clock::now();
        if (_start) {
            const std::chrono::duration<float> duration = now - *_start;
            n                                           = std::min(static_cast<std::size_t>(duration.count() * sample_rate) - _produced, n);
        } else {
            _start = now;
            output.publish(0);
            return gr::work::Status::OK;
        }

        if (_produced == 0 && n > 0) {
            this->publishTag({{std::string(gr::tag::SIGNAL_MIN.key()), -0.3f}, {std::string(gr::tag::SIGNAL_MAX.key()), 0.3f}}, 0);
        }

        const auto edgeLength = static_cast<std::size_t>(sample_rate / 200.f);
        auto       low        = (_produced / edgeLength) % 2 == 0;
        auto       firstChunk = std::span(output).first(std::min(n, edgeLength - (_produced % edgeLength)));
        std::fill(firstChunk.begin(), firstChunk.end(), low ? static_cast<T>(-0.3) : static_cast<T>(0.3));
        auto written = firstChunk.size();
        while (written < n) {
            low              = !low;
            const auto num   = std::min(n - written, edgeLength);
            auto       chunk = std::span(output).subspan(written, num);
            std::fill(chunk.begin(), chunk.end(), low ? static_cast<T>(-0.3) : static_cast<T>(0.3));
            written += num;
        }
        _produced += n;
        output.publish(n);
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

#endif
