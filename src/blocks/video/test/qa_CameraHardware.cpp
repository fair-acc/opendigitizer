#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/testing/TensorSource.hpp>

#include <opendigitizer/video/CameraSource.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

constexpr std::array<std::string_view, 2> kCameras{"Anker PowerConf C200", "Global Shutter Camera"};

/// keeps counts, the last frame's size and the driver timestamps only: full-HD frames are 8 MB each
struct FrameCounter : gr::Block<FrameCounter> {
    gr::TensorPortIn<std::uint32_t> in;
    GR_MAKE_REFLECTABLE(FrameCounter, in);

    std::size_t                frames = 0UZ;
    std::size_t                rows = 0UZ, columns = 0UZ;
    std::vector<std::int64_t>  driverTimesNs;
    std::vector<std::uint64_t> sequences;

    [[nodiscard]] gr::work::Status processBulk(gr::InputSpanLike auto& inSpan) {
        for (const auto& tag : inSpan.rawTags()) {
            if (const gr::property_map map(tag.map); map.contains(gr::tag::kTriggerMetaInfo.key())) {
                const auto meta = map.value_or<gr::property_map>(gr::tag::kTriggerMetaInfo.key(), {});
                driverTimesNs.push_back(meta.value_or<std::int64_t>("driver_time_ns", 0));
                sequences.push_back(meta.value_or<std::uint64_t>("sequence", 0U));
            }
        }
        if (!inSpan.empty()) {
            rows    = inSpan.back().extents()[0];
            columns = inSpan.back().extents()[1];
        }
        frames += inSpan.size();
        std::ignore = inSpan.consume(inSpan.size());
        return gr::work::Status::OK;
    }

    /// over the second half of the run, after the stream has settled
    [[nodiscard]] std::uint64_t steadyGaps() const {
        std::uint64_t gaps = 0U;
        for (std::size_t i = sequences.size() / 2UZ + 1UZ; i < sequences.size(); ++i) {
            gaps += sequences[i] > sequences[i - 1UZ] + 1U ? sequences[i] - sequences[i - 1UZ] - 1U : 0U;
        }
        return gaps;
    }

    /// over the second half of the run, after exposure and rate have settled
    [[nodiscard]] double measuredRate() const {
        const std::size_t first = driverTimesNs.size() / 2UZ;
        return driverTimesNs.size() < 4UZ ? 0.0 : 1e9 * static_cast<double>(driverTimesNs.size() - 1UZ - first) / static_cast<double>(driverTimesNs.back() - driverTimesNs[first]);
    }
};

} // namespace

const boost::ut::suite<"CameraSource (hardware)"> cameraHardwareTests = [] {
    using namespace boost::ut;
    using namespace std::string_literals;
    using namespace opendigitizer::video;

    "an attached camera captures 640x480 YUYV frames with driver timestamps"_test = [](std::string_view cameraName) {
        const std::vector<DeviceInfo> devices = CameraSource::listDevices();
        if (!resolveDevice(cameraName, devices)) {
            std::println("skipped: no '{}' attached", cameraName);
            return;
        }
        constexpr gr::Size_t kFrames = 15U;
        gr::Graph            graph;
        auto&                camera = graph.emplaceBlock<CameraSource>({{"device", std::string(cameraName)}, {"priority", "requested"}, {"width", gr::Size_t{640U}}, {"height", gr::Size_t{480U}}, {"frame_rate", 30.f}, {"n_frames", kFrames}});
        auto&                sink   = graph.emplaceBlock<gr::testing::TensorCollector<std::uint32_t>>();
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 8UZ}).has_value());

        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        const auto t0 = std::chrono::steady_clock::now();
        expect(sched.runAndWait().has_value());
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        expect(eq(camera.actual_width.value, gr::Size_t{640U})) << cameraName;
        expect(eq(camera.actual_height.value, gr::Size_t{480U})) << cameraName;
        expect(gt(camera.actual_frame_rate.value, 0.f)) << cameraName;
        expect(eq(sink._elements.size() + camera.droppedFrames(), std::size_t{kFrames})) << cameraName;
        expect(!sink._shapes.empty() && sink._shapes.front() == std::vector<std::size_t>{480UZ, 640UZ}) << cameraName;
        std::string timestampSource;
        for (const auto& [index, map] : sink._tags) {
            if (map.contains(gr::tag::kTriggerMetaInfo.key())) {
                timestampSource = std::string(map.value_or<gr::property_map>(gr::tag::kTriggerMetaInfo.key(), {}).value_or<std::string_view>("timestamp_source", ""));
            }
        }
        expect(timestampSource != "unknown" && !timestampSource.empty()) << cameraName << " timestamp source " << timestampSource;
        std::println("{}: {} frames in {:.2f} s, negotiated {:.1f} fps, dropped {}, sequence gaps {}, timestamps '{}'", cameraName, sink._elements.size(), seconds, camera.actual_frame_rate.value, camera.droppedFrames(), camera.sequenceGaps(), timestampSource);
    } | std::vector<std::string_view>(kCameras.begin(), kCameras.end());

    "a control set on the camera is applied and reported, and restored to its default"_test = [](std::string_view cameraName) {
        if (!resolveDevice(cameraName, CameraSource::listDevices())) {
            std::println("skipped: no '{}' attached", cameraName);
            return;
        }
        const auto brightnessAfter = [cameraName](gr::property_map controls) {
            gr::Graph graph;
            auto&     camera = graph.emplaceBlock<CameraSource>({{"device", std::string(cameraName)}, {"priority", "requested"}, {"width", gr::Size_t{640U}}, {"height", gr::Size_t{480U}}, {"n_frames", gr::Size_t{5U}}, {"controls", std::move(controls)}});
            auto&     sink   = graph.emplaceBlock<FrameCounter>();
            expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 4UZ}).has_value());
            gr::scheduler::Simple sched;
            expect(sched.exchange(std::move(graph)).has_value());
            expect(sched.runAndWait().has_value());
            return camera.available_controls.value.value_or<gr::property_map>("brightness", {});
        };
        const gr::property_map initial = brightnessAfter({});
        expect(!initial.empty()) << fatal << cameraName << "offers brightness";
        const double minimum      = initial.value_or<double>("min", 0.0);
        const double defaultValue = initial.value_or<double>("default", 0.0);
        expect(eq(brightnessAfter({{"brightness", minimum}}).value_or<double>("value", -1.0), minimum)) << cameraName << "set to its minimum";
        expect(eq(brightnessAfter({{"brightness", defaultValue}}).value_or<double>("value", -1.0), defaultValue)) << cameraName << "restored";
    } | std::vector<std::string_view>(kCameras.begin(), kCameras.end());

    "each mode priority reaches full HD or its camera's frame rate, without drops"_test = [](std::tuple<std::string_view, std::string_view, double> testCase) {
        const auto [cameraName, priority, expectedHz] = testCase;
        if (!resolveDevice(cameraName, CameraSource::listDevices())) {
            std::println("skipped: no '{}' attached", cameraName);
            return;
        }
        constexpr gr::Size_t kFrames = 90U;
        gr::Graph            graph;
        auto&                camera = graph.emplaceBlock<CameraSource>({{"device", std::string(cameraName)}, {"priority", std::string(priority)}, {"width", gr::Size_t{1920U}}, {"height", gr::Size_t{1080U}}, {"frame_rate", 25.f}, {"n_frames", kFrames}});
        auto&                sink   = graph.emplaceBlock<FrameCounter>();
        expect(graph.connect(camera, "out"s, sink, "in"s, {.minBufferSize = 4UZ}).has_value());
        gr::scheduler::Simple<gr::scheduler::ExecutionPolicy::multiThreaded> sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());

        const double rate = sink.measuredRate();
        std::println("{} [{}]: {}x{} @ {:.1f} fps negotiated, {:.1f} fps measured, {} frames, {} dropped, {} sequence gaps, {}", cameraName, priority, camera.actual_width.value, camera.actual_height.value, camera.actual_frame_rate.value, rate, sink.frames, camera.droppedFrames(), camera.sequenceGaps(), camera.fallback_reason.value.empty() ? "no fallback" : camera.fallback_reason.value);
        expect(camera.fallback_reason.value.empty()) << cameraName << camera.fallback_reason.value;
        expect(eq(sink.rows, std::size_t{camera.actual_height.value}) && eq(sink.columns, std::size_t{camera.actual_width.value}));
        if (priority != "rate") {
            expect(ge(std::size_t{camera.actual_width.value} * camera.actual_height.value, 1920UZ * 1080UZ)) << cameraName << priority << "at least full HD";
        }
        expect(eq(camera.droppedFrames(), 0UZ)) << cameraName << priority;
        expect(eq(sink.steadyGaps(), 0U)) << cameraName << priority << "capture keeps up with the camera once the stream has settled";
        expect(ge(rate, 0.9 * expectedHz)) << cameraName << priority << "rate" << rate << "expected" << expectedHz;
    } | std::vector<std::tuple<std::string_view, std::string_view, double>>{{"Anker PowerConf C200", "requested", 15.0}, {"Anker PowerConf C200", "bandwidth", 15.0}, {"Global Shutter Camera", "requested", 25.0}, {"Global Shutter Camera", "bandwidth", 88.0}, {"Global Shutter Camera", "resolution", 88.0}, {"Global Shutter Camera", "rate", 88.0}}; // the Anker delivers 15 Hz in every mode, below its advertised 30
};

int main() {
    const auto devices = opendigitizer::video::CameraSource::listDevices();
    if (std::ranges::none_of(kCameras, [&devices](std::string_view name) { return opendigitizer::video::resolveDevice(name, devices).has_value(); })) {
        std::println("skipped: none of the test cameras is attached");
        return 77;
    }
    return boost::ut::cfg<boost::ut::override>.run();
}
