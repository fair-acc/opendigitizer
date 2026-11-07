#ifndef OPENDIGITIZER_VIDEO_CAMERASOURCE_HPP
#define OPENDIGITIZER_VIDEO_CAMERASOURCE_HPP

#include "CameraTypes.hpp"
#include "TestCard.hpp"
#if defined(__EMSCRIPTEN__)
#include "WasmCamera.hpp"
#elif defined(__linux__)
#include "Mjpeg.hpp"
#include "V4l2Camera.hpp"
#endif

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace opendigitizer::video {

#if defined(__EMSCRIPTEN__)
using NativeCamera = WasmCamera;
#elif defined(__linux__)
using NativeCamera = V4l2Camera;
#endif
#if defined(__EMSCRIPTEN__) || defined(__linux__)
#define OPENDIGITIZER_VIDEO_NATIVE_CAMERA 1
#endif
#if defined(__EMSCRIPTEN__)
inline constexpr std::string_view kNativeBackend = "browser";
#else
inline constexpr std::string_view kNativeBackend = "v4l2";
#endif

/// resolves `spec` against `devices`: empty → first, '@id:<id>' → exact id, else case-insensitive name substring (no index: one
/// changes when cameras are plugged in or out)
[[nodiscard]] inline std::expected<std::size_t, std::string> resolveDevice(std::string_view spec, std::span<const DeviceInfo> devices) {
    if (devices.empty()) {
        return std::unexpected("no camera device available");
    }
    if (spec.empty()) {
        return 0UZ;
    }
    const auto lower = [](std::string_view s) { return s | std::views::transform([](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }) | std::ranges::to<std::string>(); };
    for (std::size_t i = 0UZ; i < devices.size(); ++i) {
        if (spec.starts_with(kDeviceIdPrefix) ? devices[i].id == spec.substr(kDeviceIdPrefix.size()) : lower(devices[i].name).contains(lower(spec))) {
            return i;
        }
    }
    return std::unexpected(std::format("no camera matches '{}'", spec));
}

struct CameraSource : gr::Block<CameraSource> {
    using Description = gr::Doc<R""(@brief Captures camera frames as Tensor<std::uint32_t> {height, width} of packed 0xAARRGGBB pixels, paced by the camera.

A failing, absent or lost camera is replaced by a test card (flagged in `gr:acquisition_flags`) until a device appears.
Every frame tag carries timing; device, mode and controls follow in `gr:user_data` on the first frame and after a change.
Tags on the optional `clk` input go unmerged onto the next frame; its end-of-stream ends this stream once they are out.)"">;
    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::size_t kMaxPendingClkTags = 64UZ;

    gr::PortIn<std::uint8_t, gr::Optional> clk;
    gr::TensorPortOut<std::uint32_t>       out;

    A<std::string, "device", gr::Visible, gr::Doc<"name substring or '@id:<id>' of available_devices; empty = first">>                     device;
    A<std::vector<std::string>, "available_devices", gr::Doc<"detected cameras as 'name [id]'">>                                           available_devices;
    A<ModePriority, "priority", gr::Visible, gr::Doc<"maximise bandwidth (w·h·fps), resolution or rate, or the mode nearest the request">> priority   = ModePriority::bandwidth;
    A<gr::Size_t, "width", gr::Unit<"px">, gr::Limits<kMinImageSide, kMaxImageSide>, gr::Doc<"requested width (priority 'requested')">>    width      = 640U;
    A<gr::Size_t, "height", gr::Unit<"px">, gr::Limits<kMinImageSide, kMaxImageSide>, gr::Doc<"requested height (priority 'requested')">>  height     = 480U;
    A<float, "frame_rate", gr::Unit<"Hz">, gr::Limits<0.01f, 1000.f>, gr::Doc<"requested rate (priority 'requested'; test card rate)">>    frame_rate = 25.f;
    A<gr::property_map, "controls", gr::Doc<"control name → value to apply, e.g. {exposure_auto: false, exposure_time: 100}">>             controls;
    A<gr::property_map, "available_controls", gr::Doc<"control name → {value, default, min, max, step, kind} of the active camera">>       available_controls;
    A<std::string, "active_device", gr::Doc<"device delivering frames; the test card while falling back">>                                 active_device;
    A<std::string, "fallback_reason", gr::Doc<"why the test card replaces the requested camera; empty = none">>                            fallback_reason;
    A<float, "actual_frame_rate", gr::Unit<"Hz">, gr::Doc<"negotiated rate, 0 if the driver does not report it">>                          actual_frame_rate;
    A<gr::Size_t, "actual_width", gr::Unit<"px">>                                                                                          actual_width;
    A<gr::Size_t, "actual_height", gr::Unit<"px">>                                                                                         actual_height;
    A<float, "measured_frame_rate", gr::Unit<"Hz">, gr::Doc<"published frame rate, refreshed at most once per second">>                    measured_frame_rate;
    A<gr::Size_t, "dropped_frames", gr::Doc<"frames lost to a full output or driver sequence gaps since start">>                           dropped_frames;
    A<gr::Size_t, "n_frames", gr::Doc<"stop after this many captured (published or dropped) frames; 0 = unlimited">>                       n_frames = 0U;

    GR_MAKE_REFLECTABLE(CameraSource, clk, out, device, available_devices, priority, width, height, frame_rate, controls, available_controls, active_device, fallback_reason, actual_frame_rate, actual_width, actual_height, measured_frame_rate, dropped_frames, n_frames);

    /// the settings the IO thread acts on, snapshot when they change
    struct Request {
        std::string      device;
        ModePriority     priority = ModePriority::bandwidth;
        CameraMode       mode;
        gr::property_map controls;
        gr::Size_t       nFrames = 0U;
    };

    /// what the IO thread opened; copied into the settings by `work()`, the only thread that writes them
    struct Status {
        std::vector<std::string> devices;
        gr::property_map         controls;
        std::string              device;
        std::string              fallbackReason;
        CameraMode               mode;
    };

#if OPENDIGITIZER_VIDEO_NATIVE_CAMERA
    std::variant<std::monostate, TestCardCamera, NativeCamera> _camera;
#else
    std::variant<std::monostate, TestCardCamera> _camera;
#endif
    std::mutex                            _cameraMutex; // guards swapping `_camera` against `interrupt()` from other threads
    DeviceInfo                            _active;
    CameraMode                            _mode;
    std::string                           _fallbackReason;
    std::vector<std::string>              _deviceLabels; // as listed when the camera was last opened
    gr::property_map                      _userData;
    bool                                  _userDataChanged = false;
    std::uint64_t                         _reportRevision  = 0U; // of the browser camera's last reported controls and camera list
    std::vector<gr::property_map>         _clkTags;
    bool                                  _clockEnded = false; // the clk input's end-of-stream: the camera stream ends too
    std::atomic<bool>                     _reopenRequested{false};
    std::atomic<bool>                     _stopping{false}; // set before the camera is interrupted, so the IO loop cannot miss it
    std::atomic<bool>                     _controlsChanged{false};
    std::atomic<bool>                     _statusChanged{false};
    std::mutex                            _statusMutex; // guards `_request` and `_status`
    Request                               _request;
    Status                                _status;
    std::atomic<std::uint64_t>            _published{0U};
    std::atomic<std::uint64_t>            _dropped{0U};
    std::atomic<std::uint64_t>            _sequenceGaps{0U};
    std::atomic<std::uint64_t>            _droppedClkTags{0U};
    std::atomic<std::uint64_t>            _opens{0U};
    std::uint64_t                         _lostSincePublished = 0U;
    std::optional<std::uint64_t>          _lastSequence;
    std::optional<std::int64_t>           _lastDriverTimeNs;
    bool                                  _decodeFailing = false; // reported once per run of undecodable frames
    double                                _framePeriodNs = 0.0;
    std::atomic<float>                    _measuredRate{0.f};
    std::chrono::steady_clock::time_point _lastStatusPush;
    gr::thread_pool::PooledIoTask         _ioTask;

    [[nodiscard]] static std::vector<DeviceInfo> listDevices() {
        std::vector<DeviceInfo> devices;
#if OPENDIGITIZER_VIDEO_NATIVE_CAMERA
        devices = NativeCamera::listDevices();
#endif
        devices.push_back(TestCardCamera::device());
        return devices;
    }

    [[nodiscard]] std::uint64_t publishedFrames() const noexcept { return _published.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept { return _dropped.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t sequenceGaps() const noexcept { return _sequenceGaps.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t droppedClkTags() const noexcept { return _droppedClkTags.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t opens() const noexcept { return _opens.load(std::memory_order_relaxed); } // camera (re)opens incl. fallbacks

    void start() {
        _published      = 0U;
        _dropped        = 0U;
        _sequenceGaps   = 0U;
        _droppedClkTags = 0U;
        _clockEnded     = false;
        _opens          = 0U;
        _clkTags.clear();
        _reopenRequested = false;
        _stopping        = false;
        snapshotRequest();
        openCamera(); // synchronously, so the actual mode is known once start() returns
        applyStatus();
        _ioTask.start([this] { ioLoop(); });
    }

    void stop() {
        _stopping = true;
        interruptCamera();
        if (!_ioTask.stopAndJoin()) {
            return;
        }
        std::lock_guard lock(_cameraMutex);
        _camera.emplace<std::monostate>();
    }

    gr::work::Result work(std::size_t requestedWork = std::numeric_limits<std::size_t>::max(), [[maybe_unused]] gr::device::DeviceContext& computeBackend = gr::device::hostBackend()) noexcept {
        if (gr::lifecycle::isActive(this->state())) { // before ioTaskDone(): the IO task's last status still reaches the settings
            this->applyChangedSettings(false);        // a custom work() bypasses the base class apply; only the IO thread publishes on `out`
            if (_statusChanged.load(std::memory_order_acquire) || std::chrono::steady_clock::now() - _lastStatusPush > std::chrono::seconds(1)) {
                applyStatus();
            }
        }
        if (auto done = this->ioTaskDone(requestedWork, _ioTask)) {
            return *done;
        }
        return {requestedWork, 1UZ, gr::work::Status::OK};
    }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        snapshotRequest();
        if (!_ioTask.isRunning()) {
            return;
        }
        if (std::ranges::any_of(std::array{"device", "priority", "width", "height", "frame_rate"}, [&](const char* key) { return newSettings.contains(key); })) {
            _reopenRequested = true;
            interruptCamera();
        }
        if (newSettings.contains("controls")) {
            _controlsChanged = true;
        }
    }

private:
    void snapshotRequest() {
        std::lock_guard lock(_statusMutex);
        _request = {.device = device.value, .priority = priority.value, .mode = {.width = width.value, .height = height.value, .frameRate = static_cast<double>(frame_rate.value), .format = PixelFormat::yuyv}, .controls = controls.value, .nFrames = n_frames.value};
    }

    [[nodiscard]] Request request() {
        std::lock_guard lock(_statusMutex);
        return _request;
    }

    void interruptCamera() {
        std::lock_guard lock(_cameraMutex);
        std::visit(
            [](auto& camera) {
                if constexpr (!std::is_same_v<std::decay_t<decltype(camera)>, std::monostate>) {
                    camera.interrupt();
                }
            },
            _camera);
    }

    /// staged and applied (not assigned), so that settings subscribers such as the UI are notified
    void applyStatus() {
        _lastStatusPush          = std::chrono::steady_clock::now();
        const float      rate    = _measuredRate.load(std::memory_order_relaxed);
        const auto       dropped = static_cast<gr::Size_t>(droppedFrames() + sequenceGaps());
        gr::property_map status{{"measured_frame_rate", rate}, {"dropped_frames", dropped}};
        if (_statusChanged.exchange(false, std::memory_order_acquire)) {
            std::lock_guard lock(_statusMutex);
            status.insert_or_assign("available_devices", gr::Tensor<gr::pmt::Value>(gr::data_from, _status.devices | std::views::transform([](const std::string& d) { return gr::pmt::Value(d); }) | std::ranges::to<std::vector>()));
            status.insert_or_assign("available_controls", _status.controls);
            status.insert_or_assign("active_device", _status.device);
            status.insert_or_assign("fallback_reason", _status.fallbackReason);
            status.insert_or_assign("actual_width", gr::Size_t{_status.mode.width});
            status.insert_or_assign("actual_height", gr::Size_t{_status.mode.height});
            status.insert_or_assign("actual_frame_rate", static_cast<float>(_status.mode.frameRate));
        } else if (rate == measured_frame_rate.value && dropped == dropped_frames.value) {
            return;
        }
        std::ignore = this->settings().setStaged(std::move(status));
        this->applyChangedSettings(false);
    }

    /// falls back to the test card watching for devices; a test card that cannot open stops the block
    void openCamera(std::string failure = {}) {
        const std::vector<DeviceInfo> devices = listDevices();
        const Request                 wanted  = request();
        const CameraMode&             request = wanted.mode;
        std::string                   reason  = std::move(failure);
        std::lock_guard               lock(_cameraMutex);
        _deviceLabels = devices | std::views::transform(deviceLabel) | std::ranges::to<std::vector>();
        if (reason.empty()) {
            if (auto index = resolveDevice(wanted.device, devices); !index) {
                reason = index.error();
            } else if (_active = devices[*index]; _active.id == TestCardCamera::device().id) {
                if (auto mode = _camera.emplace<TestCardCamera>().open(_active, request); mode) {
                    return opened(*mode, {});
                }
            } else {
#if OPENDIGITIZER_VIDEO_NATIVE_CAMERA
                auto mode = _camera.emplace<NativeCamera>().open(_active, request, wanted.priority);
                if (mode) {
                    return opened(*mode, {});
                }
                reason = mode.error().message;
#else
                reason = "no native camera backend on this platform";
#endif
            }
        }
        // after a camera ran, keep its size and rate, so downstream blocks see no mode change
        const double     lastFrameRate = _mode.frameRate > 0.0 ? _mode.frameRate : request.frameRate;
        const CameraMode fallback      = _opens.load() == 0U ? request : CameraMode{.width = _mode.width, .height = _mode.height, .frameRate = lastFrameRate};
        _active                        = TestCardCamera::device();
        if (auto mode = _camera.emplace<TestCardCamera>().open(_active, fallback, true); mode) {
            return opened(*mode, reason.empty() ? "camera failed" : reason);
        }
        _camera.emplace<std::monostate>();
        this->emitErrorMessage("CameraSource::openCamera()", gr::Error(reason));
    }

    void opened(const CameraMode& mode, std::string reason) {
        _decodeFailing  = false;
        _mode           = mode;
        _fallbackReason = std::move(reason);
        _lastSequence.reset();
        _lastDriverTimeNs.reset();
        _framePeriodNs = mode.frameRate > 0.0 ? 1e9 / mode.frameRate : 0.0;
        _opens.fetch_add(1U, std::memory_order_relaxed);
        _controlsChanged = true; // a (re)opened camera starts from its own control values
        updateUserData();
    }

    void applyControls() {
        for (const auto& [key, value] : request().controls) {
            const auto number = gr::pmt::convert_safely<double>(value);
            std::visit(
                [&](auto& camera) {
                    if constexpr (requires { camera.setControl(std::string_view{}, 0.0); }) {
                        if (auto set = number ? camera.setControl(std::string_view(key), *number) : std::unexpected(CameraError{CameraErrc::unsupported, "not a number"}); !set) {
                            this->emitErrorMessage("CameraSource::applyControls()", gr::Error(std::format("control '{}': {}", std::string_view(key), set.error().message)));
                        }
                    }
                },
                _camera);
        }
        updateUserData();
    }

    void updateUserData() {
        const std::vector<CameraControl> cameraControls = std::visit(
            [](auto& camera) -> std::vector<CameraControl> {
                if constexpr (requires { camera.controls(); }) {
                    return camera.controls();
                } else {
                    return {};
                }
            },
            _camera);
        gr::property_map controlMap;
        for (const CameraControl& c : cameraControls) {
            controlMap.insert_or_assign(c.name, gr::property_map{{"value", c.value}, {"default", c.defaultValue}, {"min", c.min}, {"max", c.max}, {"step", c.step}, {"kind", std::string(gr::meta::enumName(c.kind).value_or(""))}});
        }
        _userData        = gr::property_map{{"device", _active.name}, {"device_id", _active.id}, {"backend", std::string(std::holds_alternative<TestCardCamera>(_camera) ? "test_card" : kNativeBackend)}, {"width", gr::Size_t{_mode.width}}, {"height", gr::Size_t{_mode.height}}, {"source_format", std::string(formatName(_mode.format))}, {"fallback_reason", _fallbackReason}, {"controls", controlMap}};
        _userDataChanged = true;
        {
            std::lock_guard lock(_statusMutex);
            _status = {.devices = _deviceLabels, .controls = std::move(controlMap), .device = _active.name, .fallbackReason = _fallbackReason, .mode = _mode};
        }
        _statusChanged.store(true, std::memory_order_release);
    }

    void ioLoop() {
        gr::thread_pool::thread::setThreadName("camera-src");
        while (!_stopping && !_ioTask.stopRequested() && gr::lifecycle::isActive(this->state())) {
            if (_reopenRequested.exchange(false)) {
                openCamera();
            }
            if (_controlsChanged.exchange(false)) {
                applyControls();
            }
            if (_stopping || _reopenRequested) { // its interrupt may have hit the camera this pass replaced
                continue;
            }
            const auto frame = std::visit(
                [this](auto& camera) -> std::expected<bool, CameraError> {
                    if constexpr (std::is_same_v<std::decay_t<decltype(camera)>, std::monostate>) {
                        return std::unexpected(CameraError{CameraErrc::io, "no camera"});
                    } else {
                        auto view = camera.waitFrame();
                        if (!view) {
                            return std::unexpected(view.error());
                        }
                        if constexpr (requires { camera.currentMode(); }) { // the browser settles its mode and reports its controls asynchronously
                            const CameraMode m = camera.currentMode();
                            if (const std::uint64_t revision = camera.reportRevision(); m.width != _mode.width || m.height != _mode.height || m.frameRate != _mode.frameRate || revision != _reportRevision) {
                                _mode           = m;
                                _reportRevision = revision;
                                _deviceLabels   = listDevices() | std::views::transform(deviceLabel) | std::ranges::to<std::vector>();
                                updateUserData();
                            }
                        }
                        drainClock();
                        publishFrame(*view);
                        camera.release();
                        const bool       clocked = !_clockEnded || !_clkTags.empty(); // after the clock's last tags have gone out
                        const gr::Size_t limit   = request().nFrames;
                        return clocked && (limit == 0U || publishedFrames() + droppedFrames() < limit);
                    }
                },
                _camera);
            if (frame) {
                if (!*frame) {
                    break;
                }
                continue;
            }
            switch (frame.error().code) {
            case CameraErrc::interrupted: continue; // stop, reopen or a stale interrupt: the loop re-checks
            case CameraErrc::deviceChanged: _reopenRequested = true; continue;
            case CameraErrc::disconnected:
            case CameraErrc::unsupported:
            case CameraErrc::io:
                if (std::holds_alternative<TestCardCamera>(_camera) || std::holds_alternative<std::monostate>(_camera)) {
                    this->emitErrorMessage("CameraSource::ioLoop()", gr::Error(frame.error().message));
                    break;
                }
                openCamera(frame.error().message);
                if (std::holds_alternative<std::monostate>(_camera)) {
                    break;
                }
                continue;
            }
            break;
        }
        this->publishEoS();
    }

    void drainClock() {
        if (!clk.isConnected()) {
            return;
        }
        auto& tagReader = clk.tagReader();
        auto  tags      = tagReader.get(tagReader.available());
        for (const auto& tag : tags) {
            if (tag.map.contains(gr::tag::kEndOfStream.key())) {
                _clockEnded = true; // not forwarded: the camera ends its own stream once the clock's tags are out
                continue;
            }
            if (_clkTags.size() == kMaxPendingClkTags) {
                _clkTags.erase(_clkTags.begin());
                _droppedClkTags.fetch_add(1U, std::memory_order_relaxed);
            }
            _clkTags.emplace_back(tag.map);
        }
        std::ignore   = tags.consume(tags.size());
        auto& reader  = clk.streamReader();
        auto  clkSpan = reader.get(reader.available());
        std::ignore   = clkSpan.consume(clkSpan.size());
    }

    void publishFrame(const FrameView& frame) {
        if (_lastSequence && frame.info.sequence > *_lastSequence + 1U) {
            const std::uint64_t gap = frame.info.sequence - *_lastSequence - 1U;
            _sequenceGaps.fetch_add(gap, std::memory_order_relaxed);
            _lostSincePublished += gap;
        }
        _lastSequence = frame.info.sequence;
        if (_lastDriverTimeNs && frame.info.driverTimeNs > *_lastDriverTimeNs) {
            const double period = static_cast<double>(frame.info.driverTimeNs - *_lastDriverTimeNs);
            _framePeriodNs      = _framePeriodNs > 0.0 ? 0.9 * _framePeriodNs + 0.1 * period : period;
            _measuredRate.store(static_cast<float>(1e9 / _framePeriodNs), std::memory_order_relaxed);
        }
        _lastDriverTimeNs = frame.info.driverTimeNs;

        const auto drop = [this] {
            _dropped.fetch_add(1U, std::memory_order_relaxed);
            ++_lostSincePublished;
        };
        if (!out.isConnected()) {
            return drop();
        }
        auto span = out.streamWriter().template tryReserve<gr::SpanReleasePolicy::ProcessNone>(1UZ);
        if (span.size() == 0UZ) {
            return drop();
        }
        const std::array<std::size_t, 2> extents{_mode.height, _mode.width};
        const auto                       emitted = gr::emit(span, 0UZ, std::span<const std::size_t>(extents));
        if (!emitted) {
            return drop();
        }
        const std::span<std::uint32_t> pixels = *emitted;
        switch (_mode.format) {
        case PixelFormat::yuyv: yuyvToRgba(frame.data, frame.stride, _mode.width, _mode.height, pixels); break;
        case PixelFormat::mjpeg:
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
            if (auto decoded = decodeMjpeg(frame.data, _mode.width, _mode.height, pixels); !decoded) {
                std::ranges::fill(pixels, 0xFF000000U);
                if (!std::exchange(_decodeFailing, true)) {
                    this->emitErrorMessage("CameraSource::publishFrame()", gr::Error(decoded.error()));
                }
            } else {
                _decodeFailing = false;
            }
#endif
            break;
        case PixelFormat::rgba8:
            for (std::size_t row = 0UZ; row < _mode.height; ++row) {
                std::memcpy(pixels.data() + row * _mode.width, frame.data.data() + row * frame.stride, std::size_t{_mode.width} * sizeof(std::uint32_t));
            }
            break;
        }

        out.publishTag(frameTag(frame.info), 0UZ);
        if (!_clkTags.empty()) { // one per frame: the tag ring is only as large as the frame ring
            out.publishTag(_clkTags.front(), 0UZ);
            _clkTags.erase(_clkTags.begin());
        }
        span.publish(1UZ);
        _published.fetch_add(1U, std::memory_order_relaxed);
        this->notifyProgress();
    }

    [[nodiscard]] gr::property_map frameTag(const FrameInfo& info) {
        const auto       utcNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        gr::property_map flags;
        if (std::holds_alternative<TestCardCamera>(_camera)) {
            flags.insert_or_assign("test_card", true);
        }
        gr::property_map meta{{"frame_index", publishedFrames()}, {"sequence", info.sequence}, {"driver_time_ns", info.driverTimeNs}, {"timestamp_source", std::string(kTimestampSourceNames[std::to_underlying(info.timestampSource)])}, {std::string(gr::tag::kLocalTime.key()), utcNs}, {std::string(gr::tag::kAcquisitionFlags.key()), std::move(flags)}};
        gr::property_map tag;
        gr::tag::put(tag, gr::tag::kTriggerName, std::string("CAMERA_FRAME"));
        gr::tag::put(tag, gr::tag::kTriggerTime, utcNs);
        gr::tag::put(tag, gr::tag::kTriggerOffset, 0.f);
        gr::tag::put(tag, gr::tag::kEstFrameRate, _framePeriodNs > 0.0 ? static_cast<float>(1e9 / _framePeriodNs) : 0.f);
        gr::tag::put(tag, gr::tag::kTriggerMetaInfo, std::move(meta));
        if (_lostSincePublished > 0U) {
            gr::tag::put(tag, gr::tag::kNDroppedSamples, static_cast<gr::Size_t>(std::exchange(_lostSincePublished, 0U)));
        }
        if (std::exchange(_userDataChanged, false)) {
            gr::Tensor<gr::pmt::Value> names(gr::extents_from, {2UZ});
            gr::Tensor<gr::pmt::Value> units(gr::extents_from, {2UZ});
            names[0] = gr::pmt::Value{"y"};
            names[1] = gr::pmt::Value{"x"};
            units[0] = gr::pmt::Value{"px"};
            units[1] = gr::pmt::Value{"px"};
            gr::tag::put(tag, gr::tag::kSignalName, _active.name);
            gr::tag::put(tag, gr::tag::kFrameRate, static_cast<float>(_mode.frameRate));
            gr::tag::put(tag, gr::tag::kAxisName, std::move(names));
            gr::tag::put(tag, gr::tag::kAxisUnit, std::move(units));
            gr::tag::put(tag, gr::tag::kUserData, _userData);
        }
        return tag;
    }
};

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_CAMERASOURCE_HPP
