#ifndef OPENDIGITIZER_VIDEO_WASMCAMERA_HPP
#define OPENDIGITIZER_VIDEO_WASMCAMERA_HPP

#if defined(__EMSCRIPTEN__)

#include "CameraTypes.hpp"

#include <gnuradio-4.0/meta/reflection.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <emscripten.h>

namespace opendigitizer::video {

namespace wasm {

EMSCRIPTEN_KEEPALIVE inline char __em_lib_deps_od_camera[] __attribute__((section("em_lib_deps"), aligned(1))) = "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString";

// EM_ASM in inline functions: EM_JS would define its symbol in every including TU; bodies are parenthesised since the
// preprocessor splits macro arguments at unparenthesised commas
// clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
inline void openCamera(std::uintptr_t camera, const char* devicePath, int width, int height, double frameRate, const char* priorityName) {
    MAIN_THREAD_EM_ASM({ ((camera, devicePath, width, height, frameRate, priorityName) => {
    const device = UTF8ToString(devicePath); // an index before camera permission, a device id after
    const kControls = [['brightness'], ['contrast'], ['saturation'], ['sharpness'], ['exposureMode', 'continuous'], ['exposureTime'], ['whiteBalanceMode', 'continuous'], ['colorTemperature'], ['focusMode', 'continuous'], ['focusDistance'], ['zoom']]; // kCommonControls, by index
    const cams  = globalThis.__odCameras || (globalThis.__odCameras = new Map());
    const state = { stopped : false, stream : null, video : null, reader : null, track : null, controls : kControls };
    cams.set(camera, state);
    const fail = (message) => {
        if (state.stopped) { return; }
        const text = String(message);
        const size = lengthBytesUTF8(text) + 1;
        stringToUTF8(text, _od_camera_error_buffer(camera, size), size);
        _od_camera_failed(camera);
    };
    state.report = () => {
        if (state.stopped) { return; } // a settled applyConstraints after close: the camera object may be gone
        const caps = state.track.getCapabilities ? state.track.getCapabilities() : {};
        const now  = state.track.getSettings();
        kControls.forEach(([name, auto], index) => {
            const cap = caps[name];
            if (cap === undefined || now[name] === undefined) { return; }
            if (auto) {
                if (Array.isArray(cap) && cap.includes('manual') && cap.includes(auto)) { _od_camera_control(camera, index, 1, now[name] === 'manual' ? 0 : 1, 0, 1, 1); }
            } else if (typeof cap.min === 'number') {
                _od_camera_control(camera, index, 0, now[name], cap.min, cap.max, cap.step || 1);
            }
        });
    };
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) { fail('getUserMedia unavailable (needs HTTPS or localhost)'); return; }
    navigator.mediaDevices.enumerateDevices().then((all) => {
        const inputs = all.filter((d) => d.kind === 'videoinput');
        const large  = { ideal : 8192 };
        const priority = UTF8ToString(priorityName);
        const video  = priority === 'requested' ? { width : { ideal : width }, height : { ideal : height }, frameRate : { ideal : frameRate } }
                     : priority === 'rate' ? { frameRate : { ideal : 1000 } }
                     : priority === 'resolution' ? { width : large, height : large }
                     : { width : large, height : large, frameRate : { ideal : 1000 } };
        const byIndex = /^[0-9]+$/.test(device);
        const chosen  = byIndex ? inputs[Number(device)] : inputs.find((d) => d.deviceId === device);
        if (!chosen) { throw new Error(byIndex ? 'no browser camera ' + device + ' (' + inputs.length + ' available)' : 'the selected browser camera is not available'); }
        if (chosen.deviceId) { video.deviceId = { exact : chosen.deviceId }; }
        return navigator.mediaDevices.getUserMedia({ video : video, audio : false });
    }).then((stream) => {
        if (state.stopped) { stream.getTracks().forEach((t) => t.stop()); return; }
        state.stream = stream;
        const report = () => navigator.mediaDevices.enumerateDevices().then((all) => {
            const text = all.filter((d) => d.kind === 'videoinput' && d.deviceId).map((d) => d.deviceId + '\t' + (d.label || 'camera').replaceAll('\t', ' ').replaceAll('\n', ' ')).join('\n');
            const size = lengthBytesUTF8(text) + 1;
            stringToUTF8(text, _od_camera_devices_buffer(size), size);
            _od_camera_devices_reported();
        });
        if (!globalThis.__odCameraListWatch) { globalThis.__odCameraListWatch = true; navigator.mediaDevices.addEventListener('devicechange', report); }
        report();
        const track  = state.track = stream.getVideoTracks()[0];
        track.addEventListener('ended', () => fail('camera track ended'));
        const s = track.getSettings();
        const w = (s.width || width) | 0, h = (s.height || height) | 0; // the int32 values od_camera_mode checks
        if (!_od_camera_mode(camera, w, h, s.frameRate || frameRate)) { stream.getTracks().forEach((t) => t.stop()); return; }
        state.report();
        const canvas = new OffscreenCanvas(w, h);
        const ctx    = canvas.getContext('2d', { willReadFrequently : true });
        let sequence = 0;
        const deliver = (source, timeNs) => {
            const ptr = _od_camera_acquire(camera);
            if (ptr !== 0) {
                ctx.drawImage(source, 0, 0, w, h);
                new Uint8Array(wasmMemory.buffer, ptr, w * h * 4).set(ctx.getImageData(0, 0, w, h).data);
                _od_camera_frame(camera, sequence, timeNs);
            }
            ++sequence;
        };
        if (typeof MediaStreamTrackProcessor === 'function') {
            state.reader = new MediaStreamTrackProcessor({ track : track }).readable.getReader();
            const pump = () => state.reader.read().then(({ value, done }) => {
                if (done || state.stopped) { if (value) { value.close(); } return; }
                deliver(value, value.timestamp * 1e3);
                value.close();
                pump();
            }).catch((error) => fail(error.name + ': ' + error.message));
            pump();
            return;
        }
        if (!('requestVideoFrameCallback' in HTMLVideoElement.prototype)) { fail('neither MediaStreamTrackProcessor nor requestVideoFrameCallback available'); return; }
        const v = state.video = document.createElement('video');
        v.muted = true; v.playsInline = true; v.srcObject = stream;
        const onFrame = (now, meta) => {
            if (state.stopped) { return; }
            sequence = meta.presentedFrames;
            deliver(v, meta.mediaTime * 1e9);
            v.requestVideoFrameCallback(onFrame);
        };
        v.requestVideoFrameCallback(onFrame);
        return v.play();
    }).catch((error) => fail(error.name + ': ' + error.message));
    })($0, $1, $2, $3, $4, $5); }, camera, devicePath, width, height, frameRate, priorityName);
}

inline void setCameraControl(std::uintptr_t camera, int index, double value) {
    MAIN_THREAD_EM_ASM({ ((camera, index, value) => {
    const state = globalThis.__odCameras && globalThis.__odCameras.get(camera);
    if (!state || !state.track) { return; }
    const [name, auto] = state.controls[index];
    state.track.applyConstraints({ advanced : [ { [name] : auto ? (value !== 0 ? auto : 'manual') : value } ] }).then(() => state.report()).catch(() => state.report());
    })($0, $1, $2); }, camera, index, value);
}

inline void closeCamera(std::uintptr_t camera) {
    MAIN_THREAD_EM_ASM({ ((camera) => {
    const cams  = globalThis.__odCameras;
    const state = cams && cams.get(camera);
    if (!state) { return; }
    state.stopped = true;
    if (state.reader) { state.reader.cancel().catch(() => {}); }
    if (state.stream) { state.stream.getTracks().forEach((t) => t.stop()); }
    if (state.video) { state.video.srcObject = null; }
    cams.delete(camera);
    })($0); }, camera);
}
#pragma clang diagnostic pop
// clang-format on

} // namespace wasm

/// the cameras the browser lists once camera permission is granted: its device id (stable per site) and label
struct BrowserCameraList {
    std::mutex                 mutex;
    std::vector<DeviceInfo>    devices;
    std::string                text; // written by JS on the main thread: "<device id>\t<label>" lines
    std::atomic<std::uint64_t> revision{0U};

    void reported() {
        std::vector<DeviceInfo> parsed;
        for (const auto line : std::views::split(std::string_view(text.c_str()), '\n')) {
            const std::string_view entry(line.begin(), line.end());
            if (const auto tab = entry.find('\t'); tab != std::string_view::npos) {
                parsed.push_back({.id = std::format("browser:{}", entry.substr(0UZ, std::min<std::size_t>(tab, 12UZ))), .name = std::string(entry.substr(tab + 1UZ)), .path = std::string(entry.substr(0UZ, tab))});
            }
        }
        std::lock_guard lock(mutex);
        devices = std::move(parsed);
        ++revision;
    }
};

[[nodiscard]] inline BrowserCameraList& browserCameras() {
    static BrowserCameraList list;
    return list;
}

/// browser capture into one staging frame: free → (JS) writing → ready → (IO thread) consuming → free
struct WasmCamera {
    enum class Slot : int { free, writing, ready, consuming };

    std::vector<std::uint32_t> _staging;
    std::atomic<Slot>          _slot{Slot::free};
    CameraMode                 _mode;
    std::uint64_t              _sequence = 0U;
    std::int64_t               _timeNs   = 0;
    std::string                _error;
    std::string                _errorText; // written by JS on the main thread
    std::vector<CameraControl> _controls;  // reported by JS on the main thread, guarded by _mutex
    std::uint64_t              _controlsRevision = 0U;
    bool                       _interrupted      = false;
    bool                       _open             = false;
    std::mutex                 _mutex;
    std::condition_variable    _wakeUp;

    WasmCamera()                             = default;
    WasmCamera(const WasmCamera&)            = delete;
    WasmCamera& operator=(const WasmCamera&) = delete;
    ~WasmCamera() { close(); }

    [[nodiscard]] static std::vector<DeviceInfo> listDevices() {
        // labels are only exposed after permission; the index into the browser's videoinput list is the handle
        std::lock_guard lock(browserCameras().mutex);
        if (!browserCameras().devices.empty()) {
            return browserCameras().devices;
        }
        return {DeviceInfo{.id = "browser:0", .name = "Browser camera 0", .path = "0"}, DeviceInfo{.id = "browser:1", .name = "Browser camera 1", .path = "1"}};
    }

    /// asynchronous: the browser's mode follows with the first frame (`currentMode()`), a refusal as a `waitFrame` error
    [[nodiscard]] std::expected<CameraMode, CameraError> open(const DeviceInfo& device, const CameraMode& requested, ModePriority priority) {
        close();
        _mode        = requested;
        _mode.format = PixelFormat::rgba8;
        _slot        = Slot::free;
        _interrupted = false;
        _error.clear();
        _controls.clear();
        wasm::openCamera(reinterpret_cast<std::uintptr_t>(this), device.path.c_str(), static_cast<int>(_mode.width), static_cast<int>(_mode.height), _mode.frameRate, std::string(gr::meta::enumName(priority).value_or("")).c_str());
        _open = true;
        return _mode;
    }

    [[nodiscard]] CameraMode currentMode() {
        std::lock_guard lock(_mutex);
        return _mode;
    }

    [[nodiscard]] std::vector<CameraControl> controls() {
        std::lock_guard lock(_mutex);
        return _controls;
    }

    /// changes with each control report and each new browser camera list
    [[nodiscard]] std::uint64_t reportRevision() {
        std::lock_guard lock(_mutex);
        return _controlsRevision + browserCameras().revision.load();
    }

    [[nodiscard]] std::expected<void, CameraError> setControl(std::string_view name, double value) {
        const auto it = std::ranges::find(kCommonControls, name);
        if (it == kCommonControls.end()) {
            return std::unexpected(CameraError{CameraErrc::unsupported, std::format("no control '{}' in the browser", name)});
        }
        wasm::setCameraControl(reinterpret_cast<std::uintptr_t>(this), static_cast<int>(it - kCommonControls.begin()), value);
        return {};
    }

    [[nodiscard]] std::expected<FrameView, CameraError> waitFrame() {
        std::unique_lock lock(_mutex);
        _wakeUp.wait(lock, [this] { return _interrupted || !_error.empty() || _slot.load(std::memory_order_acquire) == Slot::ready; });
        if (std::exchange(_interrupted, false)) {
            return std::unexpected(CameraError{CameraErrc::interrupted, "interrupted"});
        }
        if (!_error.empty()) {
            return std::unexpected(CameraError{CameraErrc::disconnected, _error});
        }
        _slot.store(Slot::consuming, std::memory_order_release);
        const FrameView frame{.data = std::as_bytes(std::span(_staging)), .stride = std::size_t{_mode.width} * sizeof(std::uint32_t), .info = {.sequence = _sequence, .driverTimeNs = _timeNs, .timestampSource = TimestampSource::browserMedia}};
        lock.unlock(); // a consuming slot keeps JS off _staging, and JS reports its mode before its first frame
        std::ranges::transform(_staging, _staging.begin(), swapRedBlue);
        return frame;
    }

    void release() noexcept {
        Slot expected = Slot::consuming;
        _slot.compare_exchange_strong(expected, Slot::free, std::memory_order_acq_rel);
    }

    void interrupt() {
        {
            std::lock_guard lock(_mutex);
            _interrupted = true;
        }
        _wakeUp.notify_all();
    }

    void close() noexcept {
        if (!std::exchange(_open, false)) {
            return;
        }
        wasm::closeCamera(reinterpret_cast<std::uintptr_t>(this));
    }

    // called on the browser main thread
    [[nodiscard]] std::uintptr_t acquire() noexcept {
        Slot expected = Slot::free;
        if (_staging.empty()) {
            return 0U;
        }
        return _slot.compare_exchange_strong(expected, Slot::writing, std::memory_order_acq_rel) ? reinterpret_cast<std::uintptr_t>(_staging.data()) : 0U; // 0: still consumed, frame dropped
    }

    void frameWritten(std::uint64_t presentedFrames, double mediaTimeNs) {
        {
            std::lock_guard lock(_mutex);
            _sequence = presentedFrames; // the browser's presented-frame count: gaps are frames it did not present
            _timeNs   = static_cast<std::int64_t>(mediaTimeNs);
            _slot.store(Slot::ready, std::memory_order_release);
        }
        _wakeUp.notify_all();
    }

    /// false: the browser's size is refused and reported as the camera's error; JS then delivers no frames
    [[nodiscard]] bool modeReported(std::uint32_t w, std::uint32_t h, double frameRate) {
        {
            std::lock_guard lock(_mutex);
            if (isSupportedFrameSize(w, h)) {
                _mode = {.width = w, .height = h, .frameRate = frameRate, .format = PixelFormat::rgba8};
                _staging.assign(std::size_t{w} * h, 0U); // before the first frame: JS reports the mode before it delivers
                return true;
            }
            _error = std::format("browser camera size {}x{} px outside {}..{} px", w, h, kMinImageSide, kMaxImageSide);
        }
        _wakeUp.notify_all();
        return false;
    }

    void controlReported(std::size_t index, bool boolean, double value, double min, double max, double step) {
        assert(index < kCommonControls.size());
        std::lock_guard   lock(_mutex);
        const std::string name(kCommonControls[index]);
        std::erase_if(_controls, [&](const CameraControl& c) { return c.name == name; });
        _controls.push_back({.name = name, .kind = boolean ? CameraControl::Kind::boolean : CameraControl::Kind::integer, .value = value, .defaultValue = value, .min = min, .max = max, .step = step});
        ++_controlsRevision;
    }

    [[nodiscard]] char* errorBuffer(std::size_t size) {
        _errorText.assign(size, '\0');
        return _errorText.data();
    }

    void failed() {
        {
            std::lock_guard lock(_mutex);
            _error = _errorText.c_str();
            if (_error.empty()) {
                _error = "camera failed";
            }
        }
        _wakeUp.notify_all();
    }
};

extern "C" EMSCRIPTEN_KEEPALIVE inline std::uintptr_t od_camera_acquire(std::uintptr_t camera) { return reinterpret_cast<WasmCamera*>(camera)->acquire(); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void           od_camera_frame(std::uintptr_t camera, double presentedFrames, double mediaTimeNs) { reinterpret_cast<WasmCamera*>(camera)->frameWritten(static_cast<std::uint64_t>(presentedFrames), mediaTimeNs); }
extern "C" EMSCRIPTEN_KEEPALIVE inline int            od_camera_mode(std::uintptr_t camera, int w, int h, double frameRate) { return reinterpret_cast<WasmCamera*>(camera)->modeReported(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), frameRate) ? 1 : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE inline void           od_camera_control(std::uintptr_t camera, int index, int boolean, double value, double min, double max, double step) { reinterpret_cast<WasmCamera*>(camera)->controlReported(static_cast<std::size_t>(index), boolean != 0, value, min, max, step); }
extern "C" EMSCRIPTEN_KEEPALIVE inline char*          od_camera_error_buffer(std::uintptr_t camera, std::size_t size) { return reinterpret_cast<WasmCamera*>(camera)->errorBuffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void           od_camera_failed(std::uintptr_t camera) { reinterpret_cast<WasmCamera*>(camera)->failed(); }
extern "C" EMSCRIPTEN_KEEPALIVE inline char*          od_camera_devices_buffer(std::size_t size) {
    browserCameras().text.assign(size, '\0');
    return browserCameras().text.data();
}
extern "C" EMSCRIPTEN_KEEPALIVE inline void od_camera_devices_reported() { browserCameras().reported(); }

} // namespace opendigitizer::video

#endif // __EMSCRIPTEN__
#endif // OPENDIGITIZER_VIDEO_WASMCAMERA_HPP
