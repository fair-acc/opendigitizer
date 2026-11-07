#ifndef OPENDIGITIZER_VIDEO_DEVICEWATCH_HPP
#define OPENDIGITIZER_VIDEO_DEVICEWATCH_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <algorithm>
#include <vector>

#include <emscripten.h>
#elif defined(__linux__)
#include <array>
#include <cerrno>
#include <string_view>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace opendigitizer::video {

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
struct OwnedFd {
    int fd = -1;

    OwnedFd() = default;
    explicit OwnedFd(int f) noexcept : fd(f) {}
    OwnedFd(OwnedFd&& other) noexcept : fd(std::exchange(other.fd, -1)) {}
    OwnedFd& operator=(OwnedFd&& other) noexcept {
        reset(std::exchange(other.fd, -1));
        return *this;
    }
    OwnedFd(const OwnedFd&)            = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    ~OwnedFd() { reset(); }

    void reset(int f = -1) noexcept {
        if (fd >= 0) {
            ::close(fd);
        }
        fd = f;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return fd >= 0; }
};
#endif

/// sleeps until a deadline unless interrupted or, when asked to, a camera device appears.
/// Linux: inotify on `directory` for created or re-permissioned `video*` nodes; browser: `notifyDeviceChange()` from the
/// media-devices `devicechange` event
struct DeviceWatch {
    enum class Wake { deadline, interrupted, deviceChanged };

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    OwnedFd _inotify;
    OwnedFd _wakeFd;

    explicit DeviceWatch(const std::string& directory = "/dev") : _inotify(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)), _wakeFd(::eventfd(0U, EFD_NONBLOCK | EFD_CLOEXEC)) {
        if (_inotify) {
            ::inotify_add_watch(_inotify.fd, directory.c_str(), IN_CREATE | IN_ATTRIB);
        }
    }

    [[nodiscard]] bool interruptible() const noexcept { return static_cast<bool>(_wakeFd); }

    void reset() noexcept {
        std::uint64_t count = 0U;
        std::ignore         = ::read(_wakeFd.fd, &count, sizeof(count));
        drainVideoEvents();
    }

    void interrupt() noexcept {
        const std::uint64_t one = 1U;
        std::ignore             = ::write(_wakeFd.fd, &one, sizeof(one));
    }

    [[nodiscard]] Wake waitUntil(std::chrono::steady_clock::time_point deadline, bool watchDevices) {
        while (true) {
            const auto            remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            std::array<pollfd, 2> fds{pollfd{.fd = _wakeFd.fd, .events = POLLIN, .revents = 0}, pollfd{.fd = watchDevices ? _inotify.fd : -1, .events = POLLIN, .revents = 0}};
            const int             n = ::poll(fds.data(), fds.size(), static_cast<int>(std::max<std::int64_t>(0, remaining)));
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if ((fds[0].revents & POLLIN) != 0) {
                std::uint64_t count = 0U;
                std::ignore         = ::read(_wakeFd.fd, &count, sizeof(count));
                return Wake::interrupted;
            }
            if ((fds[1].revents & POLLIN) != 0 && drainVideoEvents()) {
                return Wake::deviceChanged;
            }
            if (n <= 0 && remaining <= 0) {
                return Wake::deadline;
            }
        }
    }

private:
    bool drainVideoEvents() noexcept {
        alignas(inotify_event) std::array<char, 4096> buffer;
        bool                                          video = false;
        for (ssize_t len; (len = ::read(_inotify.fd, buffer.data(), buffer.size())) > 0;) {
            for (ssize_t offset = 0; offset < len;) {
                const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                video             = video || (event->len > 0U && std::string_view(event->name).starts_with("video"));
                offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
            }
        }
        return video;
    }

public:
#else
    std::mutex              _mutex;
    std::condition_variable _wakeUp;
    bool                    _interrupted = false;
    bool                    _changed     = false;

    explicit DeviceWatch(const std::string& = {}) {
#if defined(__EMSCRIPTEN__)
        {
            std::lock_guard lock(registryMutex());
            registry().push_back(this);
        }
        // clang-format off
        MAIN_THREAD_EM_ASM({ // outside the lock: the main thread may be waiting for it in od_devices_changed()
            if (!globalThis.__odDeviceWatch && navigator.mediaDevices) {
                globalThis.__odDeviceWatch = true;
                navigator.mediaDevices.addEventListener('devicechange', function() { _od_devices_changed(); });
            }
        });
        // clang-format on
#endif
    }
    DeviceWatch(const DeviceWatch&)            = delete;
    DeviceWatch& operator=(const DeviceWatch&) = delete;
    ~DeviceWatch() {
#if defined(__EMSCRIPTEN__)
        std::lock_guard lock(registryMutex());
        std::erase(registry(), this);
#endif
    }

#if defined(__EMSCRIPTEN__)
    static std::mutex& registryMutex() {
        static std::mutex mutex;
        return mutex;
    }
    static std::vector<DeviceWatch*>& registry() {
        static std::vector<DeviceWatch*> watches;
        return watches;
    }
#endif

    [[nodiscard]] static constexpr bool interruptible() noexcept { return true; }

    void reset() {
        std::lock_guard lock(_mutex);
        _interrupted = false;
        _changed     = false;
    }

    void interrupt() {
        {
            std::lock_guard lock(_mutex);
            _interrupted = true;
        }
        _wakeUp.notify_all();
    }

    void notifyDeviceChange() {
        {
            std::lock_guard lock(_mutex);
            _changed = true;
        }
        _wakeUp.notify_all();
    }

    [[nodiscard]] Wake waitUntil(std::chrono::steady_clock::time_point deadline, bool watchDevices) {
        std::unique_lock lock(_mutex);
        _wakeUp.wait_until(lock, deadline, [&] { return _interrupted || (watchDevices && _changed); });
        if (std::exchange(_interrupted, false)) {
            return Wake::interrupted;
        }
        if (watchDevices && std::exchange(_changed, false)) {
            return Wake::deviceChanged;
        }
        return Wake::deadline;
    }
#endif
};

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE inline void od_devices_changed() {
    std::lock_guard lock(DeviceWatch::registryMutex());
    for (DeviceWatch* watch : DeviceWatch::registry()) {
        watch->notifyDeviceChange();
    }
}
#endif

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_DEVICEWATCH_HPP
