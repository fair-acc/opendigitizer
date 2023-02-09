#ifndef OPENDIGITIZER_SERVICE_MOCK_SOURCE_H
#define OPENDIGITIZER_SERVICE_MOCK_SOURCE_H

namespace opendigitizer::acq {

using namespace std::chrono_literals;

template<typename T>
class mock_source {
    struct sinks_with_writers{
        T::sink_buffer &sink;
        T::tagbuffer::template buffer_writer<typename T::padded_tag_map> tag_writer;
        T::streambuffer::template buffer_writer<float> stream_writer;

        explicit sinks_with_writers(T::sink_buffer &s) : sink{s}, tag_writer{s.tag.new_writer()},  stream_writer{s.stream.new_writer()} { }
    };
    std::vector<sinks_with_writers> sinks{};
public:
    mock_source() = delete;
    mock_source(mock_source&) = delete;
    explicit mock_source(std::vector<typename T::sink_buffer> &s) {
        for (typename T::sink_buffer &sink : s) {
            sinks.emplace_back(sink);
        }
    }

    void operator()(std::stop_token stop_token) {
        fmt::print("Starting mock stream signals with sinks: {}\n", sinks | std::ranges::views::transform([](auto &s){return s.sink.name;}));
        while (!stop_token.stop_requested()) {
            for (sinks_with_writers &sink : sinks) {
                std::size_t n_samples = 64;
                std::int64_t write_pos;
                bool write_success = sink.stream_writer.try_publish([&write_pos](std::span<float> &writable_data, std::int64_t writePos){
                    write_pos = writePos;
                    for (std::size_t i = 0; i < writable_data.size(); i++) {
                        writable_data[i] = std::sin(0.05f * static_cast<float>(i));
                    }
                }, n_samples);
                if (!write_success){
                    fmt::print("Error writing into the stream buffer\n");
                    break;
                }
                bool tag_write_success = sink.tag_writer.try_publish([&write_pos](std::span<typename T::padded_tag_map> &writable_data, std::int64_t /*writePos*/){
                    writable_data[0] = typename T::padded_tag_map{.map = {{"time", static_cast<double>(write_pos) * 0.05}, {"random_fact", 4.2f}}, .seq = write_pos};
                }, 1);
                if (!tag_write_success){
                    fmt::print("Error writing into the tag buffer\n");
                    break;
                }
            }
            std::this_thread::sleep_for(1s);
        }
    }
};

}
#endif //OPENDIGITIZER_SERVICE_MOCK_SOURCE_H
