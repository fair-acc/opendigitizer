#ifndef OPENDIGITIZER_VIDEO_V4L2CAMERA_HPP
#define OPENDIGITIZER_VIDEO_V4L2CAMERA_HPP

#if defined(__linux__) && !defined(__EMSCRIPTEN__)

#include "CameraTypes.hpp"
#include "DeviceWatch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace opendigitizer::video {

namespace v4l2 {

[[nodiscard]] inline int xioctl(int fd, unsigned long request, void* arg) noexcept {
    int r;
    do {
        r = ::ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

[[nodiscard]] inline CameraError errnoError(std::string_view what) {
    const int err = errno;
    return {.code = (err == ENODEV || err == ENXIO || err == EIO) ? CameraErrc::disconnected : CameraErrc::io, .message = std::format("{}: {}", what, std::strerror(err))};
}

[[nodiscard]] inline std::uint32_t captureCaps(const v4l2_capability& cap) noexcept { return (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0U ? cap.device_caps : cap.capabilities; }

[[nodiscard]] inline std::string cString(const std::uint8_t* chars, std::size_t size) { return {reinterpret_cast<const char*>(chars), ::strnlen(reinterpret_cast<const char*>(chars), size)}; }

[[nodiscard]] constexpr std::uint32_t fourcc(PixelFormat format) noexcept { return format == PixelFormat::mjpeg ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV; }

/// canonical names of the common controls (`kCommonControls`); others keep the driver's name in v4l2-ctl spelling
[[nodiscard]] inline std::string controlName(std::uint32_t id, std::string_view driverName) {
    static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 11> kCanonical{{{V4L2_CID_BRIGHTNESS, "brightness"}, {V4L2_CID_CONTRAST, "contrast"}, {V4L2_CID_SATURATION, "saturation"}, {V4L2_CID_SHARPNESS, "sharpness"}, {V4L2_CID_EXPOSURE_AUTO, "exposure_auto"}, {V4L2_CID_EXPOSURE_ABSOLUTE, "exposure_time"}, {V4L2_CID_AUTO_WHITE_BALANCE, "white_balance_auto"}, {V4L2_CID_WHITE_BALANCE_TEMPERATURE, "white_balance_temperature"}, {V4L2_CID_FOCUS_AUTO, "focus_auto"}, {V4L2_CID_FOCUS_ABSOLUTE, "focus"}, {V4L2_CID_ZOOM_ABSOLUTE, "zoom"}}};
    if (const auto it = std::ranges::find(kCanonical, id, &std::pair<std::uint32_t, std::string_view>::first); it != kCanonical.end()) {
        return std::string(it->second);
    }
    std::string name;
    for (char c : driverName) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!name.empty() && name.back() != '_') {
            name += '_';
        }
    }
    while (!name.empty() && name.back() == '_') {
        name.pop_back();
    }
    return name;
}

} // namespace v4l2

/// single-planar mmap streaming capture of YUYV or MJPEG frames
struct V4l2Camera {
    static constexpr std::size_t kBufferCount = 4UZ;

    struct Mapping {
        void*       start  = nullptr;
        std::size_t length = 0UZ;
    };

    OwnedFd                      _fd;
    OwnedFd                      _wakeFd;
    std::vector<Mapping>         _buffers;
    CameraMode                   _mode;
    std::size_t                  _stride    = 0UZ;
    bool                         _streaming = false;
    std::optional<std::uint32_t> _dequeued;

    V4l2Camera()                             = default;
    V4l2Camera(const V4l2Camera&)            = delete;
    V4l2Camera& operator=(const V4l2Camera&) = delete;
    ~V4l2Camera() { close(); }

    [[nodiscard]] static std::vector<DeviceInfo> listDevices() {
        static constexpr std::string_view kNodePrefix = "/dev/video";
        std::vector<DeviceInfo>           devices;
        std::error_code                   ec;
        for (auto entry = std::filesystem::directory_iterator("/sys/class/video4linux", ec); !ec && entry != std::filesystem::directory_iterator(); entry.increment(ec)) {
            const std::string node = entry->path().filename().string();
            if (!node.starts_with("video")) {
                continue;
            }
            const std::string path = "/dev/" + node;
            OwnedFd           fd(::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
            v4l2_capability   cap{};
            if (!fd || v4l2::xioctl(fd.fd, VIDIOC_QUERYCAP, &cap) != 0) {
                continue;
            }
            const std::uint32_t caps = v4l2::captureCaps(cap);
            if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0U || (caps & V4L2_CAP_STREAMING) == 0U) {
                continue; // e.g. the UVC metadata node
            }
            devices.push_back({.id = v4l2::cString(cap.bus_info, sizeof(cap.bus_info)), .name = v4l2::cString(cap.card, sizeof(cap.card)), .path = path});
        }
        const auto nodeNumber = [](const DeviceInfo& device) {
            const std::string_view digits = std::string_view(device.path).substr(kNodePrefix.size());
            int                    number = 0;
            std::from_chars(digits.data(), digits.data() + digits.size(), number);
            return number;
        };
        std::ranges::sort(devices, {}, nodeNumber);
        return devices;
    }

    /// discrete YUYV and MJPEG modes; stepwise sizes contribute their largest size, stepwise intervals their fastest rate
    [[nodiscard]] static std::vector<CameraMode> listModes(const DeviceInfo& device) {
        std::vector<CameraMode> modes;
        OwnedFd                 fd(::open(device.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        if (!fd) {
            return modes;
        }
        for (PixelFormat format : {PixelFormat::yuyv, PixelFormat::mjpeg}) {
            v4l2_frmsizeenum size{.index = 0U, .pixel_format = v4l2::fourcc(format), .type = 0U, .discrete = {}, .reserved = {}};
            for (; v4l2::xioctl(fd.fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
                const bool          discrete = size.type == V4L2_FRMSIZE_TYPE_DISCRETE;
                const std::uint32_t w = discrete ? size.discrete.width : size.stepwise.max_width, h = discrete ? size.discrete.height : size.stepwise.max_height;
                v4l2_frmivalenum    ival{.index = 0U, .pixel_format = size.pixel_format, .width = w, .height = h, .type = 0U, .discrete = {}, .reserved = {}};
                for (; v4l2::xioctl(fd.fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) == 0; ++ival.index) {
                    const v4l2_fract t = ival.type == V4L2_FRMIVAL_TYPE_DISCRETE ? ival.discrete : ival.stepwise.min;
                    if (t.numerator != 0U && isSupportedFrameSize(w, h)) {
                        modes.push_back({.width = w, .height = h, .frameRate = static_cast<double>(t.denominator) / t.numerator, .format = format});
                    }
                    if (ival.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                        break;
                    }
                }
                if (!discrete) {
                    break;
                }
            }
        }
        return modes;
    }

    /// opens the `listModes` entry `selectMode` picks for `priority`; the returned mode is what the driver set
    [[nodiscard]] std::expected<CameraMode, CameraError> open(const DeviceInfo& device, const CameraMode& request, ModePriority priority) {
        close();
        const std::vector<CameraMode> modes     = listModes(device);
        const CameraMode              requested = selectMode(modes, priority, request).value_or(request);
        _fd.reset(::open(device.path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        _wakeFd.reset(::eventfd(0U, EFD_NONBLOCK | EFD_CLOEXEC));
        if (!_fd || !_wakeFd) {
            return std::unexpected(v4l2::errnoError(std::format("open {}", device.path)));
        }

        v4l2_format fmt{};
        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = requested.width;
        fmt.fmt.pix.height      = requested.height;
        fmt.fmt.pix.pixelformat = v4l2::fourcc(requested.format);
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;
        if (v4l2::xioctl(_fd.fd, VIDIOC_S_FMT, &fmt) != 0) {
            return failed(v4l2::errnoError("VIDIOC_S_FMT"));
        }
        if (fmt.fmt.pix.pixelformat != v4l2::fourcc(requested.format)) {
            return failed({CameraErrc::unsupported, std::format("{} offers no {}", device.name, formatName(requested.format))});
        }
        if (!isSupportedFrameSize(fmt.fmt.pix.width, fmt.fmt.pix.height) || fmt.fmt.pix.bytesperline > yuyvRowBytes(kMaxImageSide)) {
            return failed({CameraErrc::unsupported, std::format("{} set {}x{} px, {} bytes per line, outside {}..{} px", device.name, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.bytesperline, kMinImageSide, kMaxImageSide)});
        }
        _mode   = {.width = fmt.fmt.pix.width, .height = fmt.fmt.pix.height, .frameRate = 0.0, .format = requested.format};
        _stride = requested.format == PixelFormat::mjpeg ? 0UZ : std::max<std::size_t>(fmt.fmt.pix.bytesperline, yuyvRowBytes(_mode.width));

        v4l2_streamparm parm{};
        parm.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator   = 1000U;
        parm.parm.capture.timeperframe.denominator = static_cast<std::uint32_t>(std::lround(requested.frameRate * 1000.0));
        if (v4l2::xioctl(_fd.fd, VIDIOC_S_PARM, &parm) == 0 && parm.parm.capture.timeperframe.numerator != 0U) {
            _mode.frameRate = static_cast<double>(parm.parm.capture.timeperframe.denominator) / static_cast<double>(parm.parm.capture.timeperframe.numerator);
        }

        v4l2_requestbuffers req{};
        req.count  = kBufferCount;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (v4l2::xioctl(_fd.fd, VIDIOC_REQBUFS, &req) != 0 || req.count == 0U) {
            return failed(v4l2::errnoError("VIDIOC_REQBUFS"));
        }
        for (std::uint32_t i = 0U; i < req.count; ++i) {
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = i;
            if (v4l2::xioctl(_fd.fd, VIDIOC_QUERYBUF, &buf) != 0) {
                return failed(v4l2::errnoError("VIDIOC_QUERYBUF"));
            }
            void* start = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, _fd.fd, buf.m.offset);
            if (start == MAP_FAILED) {
                return failed(v4l2::errnoError("mmap"));
            }
            _buffers.push_back({start, buf.length});
            if (v4l2::xioctl(_fd.fd, VIDIOC_QBUF, &buf) != 0) {
                return failed(v4l2::errnoError("VIDIOC_QBUF"));
            }
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (v4l2::xioctl(_fd.fd, VIDIOC_STREAMON, &type) != 0) {
            return failed(v4l2::errnoError("VIDIOC_STREAMON"));
        }
        _streaming = true;
        return _mode;
    }

    [[nodiscard]] std::expected<FrameView, CameraError> waitFrame() {
        release();
        const std::size_t frameBytes = _stride * _mode.height;
        while (true) {
            std::array<pollfd, 2> fds{pollfd{.fd = _fd.fd, .events = POLLIN, .revents = 0}, pollfd{.fd = _wakeFd.fd, .events = POLLIN, .revents = 0}};
            if (::poll(fds.data(), fds.size(), -1) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return std::unexpected(v4l2::errnoError("poll"));
            }
            if ((fds[1].revents & POLLIN) != 0) {
                std::uint64_t count = 0U;
                std::ignore         = ::read(_wakeFd.fd, &count, sizeof(count));
                return std::unexpected(CameraError{CameraErrc::interrupted, "interrupted"});
            }
            if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return std::unexpected(CameraError{CameraErrc::disconnected, "device reported an error or hang-up"});
            }
            v4l2_buffer buf{};
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            if (v4l2::xioctl(_fd.fd, VIDIOC_DQBUF, &buf) != 0) {
                if (errno == EAGAIN) {
                    continue;
                }
                return std::unexpected(v4l2::errnoError("VIDIOC_DQBUF"));
            }
            _dequeued = buf.index;
            if (buf.index >= _buffers.size() || (buf.flags & V4L2_BUF_FLAG_ERROR) != 0U || buf.bytesused < frameBytes || buf.bytesused == 0U || buf.bytesused > _buffers[buf.index].length) {
                release(); // corrupt or short frame: requeue, wait for the next one
                continue;
            }
            const bool         mono            = (buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
            const bool         startOfExposure = (buf.flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) == V4L2_BUF_FLAG_TSTAMP_SRC_SOE;
            const auto         driverSource    = startOfExposure ? TimestampSource::driverStartOfExposure : TimestampSource::driverEndOfFrame;
            const auto         source          = mono ? driverSource : TimestampSource::unknown;
            const std::int64_t timeNs          = mono ? std::int64_t{buf.timestamp.tv_sec} * 1'000'000'000 + std::int64_t{buf.timestamp.tv_usec} * 1'000 : 0;
            return FrameView{.data = {static_cast<const std::byte*>(_buffers[buf.index].start), _stride == 0UZ ? std::size_t{buf.bytesused} : frameBytes}, .stride = _stride, .info = {.sequence = buf.sequence, .driverTimeNs = timeNs, .timestampSource = source}};
        }
    }

    [[nodiscard]] std::vector<CameraControl> controls() const {
        std::vector<CameraControl> result;
        v4l2_query_ext_ctrl        query{};
        query.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        for (; v4l2::xioctl(_fd.fd, VIDIOC_QUERY_EXT_CTRL, &query) == 0; query.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND) {
            using enum CameraControl::Kind;
            const std::optional<CameraControl::Kind> kind = [&query]() -> std::optional<CameraControl::Kind> {
                switch (query.type) {
                case V4L2_CTRL_TYPE_INTEGER: return integer;
                case V4L2_CTRL_TYPE_BOOLEAN: return boolean;
                case V4L2_CTRL_TYPE_MENU: return menu;
                default: return std::nullopt;
                }
            }();
            if (!kind || (query.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY)) != 0U) {
                continue;
            }
            v4l2_control value{.id = query.id, .value = 0};
            if (v4l2::xioctl(_fd.fd, VIDIOC_G_CTRL, &value) != 0) {
                continue;
            }
            CameraControl control{.name = v4l2::controlName(query.id, query.name), .kind = *kind, .value = static_cast<double>(value.value), .defaultValue = static_cast<double>(query.default_value), .min = static_cast<double>(query.minimum), .max = static_cast<double>(query.maximum), .step = static_cast<double>(query.step)};
            if (query.id == V4L2_CID_EXPOSURE_AUTO) { // the menu becomes a switch: manual vs. the driver's automatic default
                control = {.name = control.name, .kind = boolean, .value = value.value == V4L2_EXPOSURE_MANUAL ? 0.0 : 1.0, .defaultValue = query.default_value == V4L2_EXPOSURE_MANUAL ? 0.0 : 1.0, .min = 0.0, .max = 1.0, .step = 1.0};
            }
            result.push_back(std::move(control));
        }
        return result;
    }

    [[nodiscard]] std::expected<void, CameraError> setControl(std::string_view name, double value) {
        v4l2_query_ext_ctrl query{};
        query.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        for (; v4l2::xioctl(_fd.fd, VIDIOC_QUERY_EXT_CTRL, &query) == 0; query.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND) {
            if (v4l2::controlName(query.id, query.name) != name) {
                continue;
            }
            v4l2_control control{.id = query.id, .value = static_cast<std::int32_t>(std::lround(value))};
            if (query.id == V4L2_CID_EXPOSURE_AUTO) {
                const std::int64_t automatic = query.default_value == V4L2_EXPOSURE_MANUAL ? std::int64_t{V4L2_EXPOSURE_APERTURE_PRIORITY} : query.default_value;
                control.value                = value != 0.0 ? static_cast<std::int32_t>(automatic) : std::int32_t{V4L2_EXPOSURE_MANUAL};
            }
            if (v4l2::xioctl(_fd.fd, VIDIOC_S_CTRL, &control) != 0) {
                return std::unexpected(v4l2::errnoError(std::format("set control '{}'", name)));
            }
            return {};
        }
        return std::unexpected(CameraError{CameraErrc::unsupported, std::format("no control '{}'", name)});
    }

    void release() noexcept {
        if (!_dequeued) {
            return;
        }
        v4l2_buffer buf{};
        buf.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory  = V4L2_MEMORY_MMAP;
        buf.index   = *std::exchange(_dequeued, std::nullopt);
        std::ignore = v4l2::xioctl(_fd.fd, VIDIOC_QBUF, &buf);
    }

    void interrupt() noexcept {
        const std::uint64_t one = 1U;
        std::ignore             = ::write(_wakeFd.fd, &one, sizeof(one));
    }

    void close() noexcept {
        if (_streaming) {
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            std::ignore        = v4l2::xioctl(_fd.fd, VIDIOC_STREAMOFF, &type);
            _streaming         = false;
        }
        _dequeued.reset();
        for (const Mapping& m : _buffers) {
            ::munmap(m.start, m.length);
        }
        _buffers.clear();
        if (_fd) {
            v4l2_requestbuffers req{};
            req.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req.memory  = V4L2_MEMORY_MMAP;
            std::ignore = v4l2::xioctl(_fd.fd, VIDIOC_REQBUFS, &req);
        }
        _fd.reset();
        _wakeFd.reset();
    }

private:
    [[nodiscard]] std::unexpected<CameraError> failed(CameraError error) {
        close();
        return std::unexpected(std::move(error));
    }
};

} // namespace opendigitizer::video

#endif // __linux__ && !__EMSCRIPTEN__
#endif // OPENDIGITIZER_VIDEO_V4L2CAMERA_HPP
