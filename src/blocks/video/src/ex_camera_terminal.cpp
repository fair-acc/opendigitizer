#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/algorithm/ImChart.hpp>

#include <opendigitizer/video/CameraSource.hpp>
#include <opendigitizer/video/ImageTransforms.hpp>

#include <charconv>
#include <print>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kChartColumns = 120UZ;
constexpr std::size_t kChartRows    = 40UZ;

struct TerminalViewer : gr::Block<TerminalViewer> {
    using Description = gr::Doc<"Draws the latest luma frame as a UTF-8 heatmap in the terminal.">;
    gr::TensorPortIn<double> in;
    float                    source_width  = 640.f;
    float                    source_height = 480.f;

    GR_MAKE_REFLECTABLE(TerminalViewer, in, source_width, source_height);

    std::vector<double> _flipped;
    std::string         _device;
    std::string         _status;

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan) {
        for (const auto& tag : inSpan.rawTags()) {
            const gr::property_map map(tag.map);
            if (map.contains(gr::tag::kSignalName.key())) {
                _device = map.value_or<std::string_view>(gr::tag::kSignalName.key(), "?");
            }
            if (map.contains(gr::tag::kTriggerMetaInfo.key())) {
                const auto meta = map.value_or<gr::property_map>(gr::tag::kTriggerMetaInfo.key(), {});
                _status         = std::format("{} | frame {} | seq {} | {:.1f} fps | timestamps {}", _device, meta.value_or<std::uint64_t>("frame_index", 0U), meta.value_or<std::uint64_t>("sequence", 0U), map.value_or<float>(gr::tag::kEstFrameRate.key(), 0.f), meta.value_or<std::string_view>("timestamp_source", "?"));
            }
        }
        if (!inSpan.empty()) {
            draw(inSpan[inSpan.size() - 1UZ]);
        }
        std::ignore = inSpan.consume(inSpan.size());
        return gr::work::Status::OK;
    }

    void draw(const gr::TensorView<double>& frame) {
        const std::size_t rows    = frame.extents()[0];
        const std::size_t columns = frame.extents()[1];
        _flipped.resize(frame.size());
        for (std::size_t r = 0UZ; r < rows; ++r) { // image row 0 is the top, heatmap row 0 the bottom
            std::copy_n(frame.data() + (rows - 1UZ - r) * columns, columns, _flipped.data() + r * columns);
        }
        gr::graphs::ImChart<kChartColumns, kChartRows> chart({{0.0, static_cast<double>(source_width)}, {0.0, static_cast<double>(source_height)}});
        chart.axis_name_x = "x [px]";
        chart.axis_name_y = "y [px]";
        std::print("\033[H");
        if (auto drawn = chart.drawHeatmap(std::span<const double>(_flipped), rows, columns, {0.0, 1.0}); !drawn) {
            std::println("heatmap: {}", drawn.error());
        }
        std::println("{}\033[K", _status);
        std::print("\033[J");
    }
};

} // namespace

int main(int argc, char** argv) {
    using namespace std::string_literals;
    using namespace opendigitizer::video;

    const std::string device  = argc > 1 ? argv[1] : "";
    gr::Size_t        nFrames = 0U;
    if (argc > 2) {
        std::from_chars(argv[2], argv[2] + std::char_traits<char>::length(argv[2]), nFrames);
    }
    std::println("cameras:");
    for (const auto& info : CameraSource::listDevices()) {
        std::println("  {} [{}] {}", info.name, info.id, info.path);
    }
    std::println("usage: {} [device: name substring or @id:<id>] [n_frames, 0 = until Ctrl-C]", argv[0]);

    gr::Graph graph;
    auto&     camera = graph.emplaceBlock<CameraSource>({{"device", device}, {"priority", "requested"}, {"width", gr::Size_t{640U}}, {"height", gr::Size_t{480U}}, {"frame_rate", 30.f}, {"n_frames", nFrames}});
    auto&     grey   = graph.emplaceBlock<GreyScale<double>>();
    auto&     resize = graph.emplaceBlock<ImageResize<double>>({{"width", gr::Size_t{2U * kChartColumns}}, {"height", gr::Size_t{4U * kChartRows}}, {"interpolation", "bilinear"}});
    auto&     viewer = graph.emplaceBlock<TerminalViewer>();
    if (!graph.connect(camera, "out"s, grey, "in"s, {.minBufferSize = 8UZ}) || !graph.connect(grey, "out"s, resize, "in"s, {.minBufferSize = 8UZ}) || !graph.connect(resize, "out"s, viewer, "in"s, {.minBufferSize = 8UZ})) {
        std::println(stderr, "failed to connect the graph");
        return 1;
    }

    std::print("\033[2J");
    gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> scheduler;
    if (auto exchanged = scheduler.exchange(std::move(graph)); !exchanged) {
        std::println(stderr, "scheduler: {}", exchanged.error().message);
        return 1;
    }
    if (auto ran = scheduler.runAndWait(); !ran) {
        std::println(stderr, "run: {}", ran.error().message);
        return 1;
    }
    std::println("{} frames published, {} dropped, {} sequence gaps", camera.publishedFrames(), camera.droppedFrames(), camera.sequenceGaps());
    return 0;
}
