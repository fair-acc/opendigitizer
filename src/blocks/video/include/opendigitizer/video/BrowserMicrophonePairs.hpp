#ifndef OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP
#define OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace opendigitizer::video {

/// lag of stream `b` behind stream `a` in samples: the multiple of `quantum` within ±`maxSteps` whose neighbourhood of
/// ±`maxAcoustic` samples correlates best, if it beats the next-best multiple by `confidence`; the acoustic delay between the
/// two microphones stays below half a quantum, so the browser's buffering offset and the sound's own delay separate. Raw
/// samples, not differences: low-frequency room sound is coherent across the array, the microphones' self-noise is not.
[[nodiscard]] inline std::optional<std::ptrdiff_t> quantisedLag(std::span<const float> a, std::span<const float> b, std::size_t quantum, std::size_t maxSteps, std::size_t maxAcoustic, double confidence = 1.5) {
    const auto reach = static_cast<std::ptrdiff_t>(maxSteps * quantum + maxAcoustic);
    const auto n     = static_cast<std::ptrdiff_t>(std::min(a.size(), b.size()));
    if (n <= 2 * reach) {
        return std::nullopt;
    }
    const auto            q           = static_cast<std::ptrdiff_t>(quantum);
    const auto            steps       = static_cast<std::ptrdiff_t>(maxSteps);
    const auto            acoustic    = static_cast<std::ptrdiff_t>(maxAcoustic);
    const auto            correlation = [&](std::ptrdiff_t shift) { return std::transform_reduce(a.begin() + reach, a.begin() + (n - reach), b.begin() + (reach + shift), 0.0, std::plus{}, [](float x, float y) { return static_cast<double>(x) * static_cast<double>(y); }); };
    const auto            score       = [&](std::ptrdiff_t k) { return std::ranges::max(std::views::iota(-acoustic, acoustic + 1) | std::views::transform([&](std::ptrdiff_t d) { return std::abs(correlation(k * q + d)); })); };
    const auto            scores      = std::views::iota(-steps, steps + 1) | std::views::transform(score) | std::ranges::to<std::vector>();
    std::array<double, 2> topTwo{};
    std::ranges::partial_sort_copy(scores, topTwo, std::ranges::greater{});
    const auto [best, second] = topTwo;
    if (best <= 0.0 || best < confidence * second) {
        return std::nullopt;
    }
    return (std::ranges::distance(scores.begin(), std::ranges::max_element(scores)) - steps) * q;
}

enum class PairAlignment : std::uint8_t { continuous, once, off };

/// four time-aligned microphone streams (pair A left/right, pair B left/right) from two stereo sources; pushed blocks of
/// interleaved frames, pair B shifted by whole quanta until its first channel lines up with pair A's
struct PairAligner {
    static constexpr std::size_t kChannels           = 4UZ;
    static constexpr std::size_t kQuantum            = 256UZ; // frames
    static constexpr std::size_t kMaxSteps           = 4UZ;
    static constexpr double      kMaxAcousticSeconds = 0.5e-3;
    static constexpr double      kWindowSeconds      = 0.5;
    static constexpr double      kMaxBacklogSeconds  = 1.0;

    std::size_t                   maxAcoustic = 24UZ;    // frames; these three follow the sample rate (48 kHz until set)
    std::size_t                   window      = 24000UZ; // frames per estimate
    std::size_t                   maxBacklog  = 48000UZ; // frames held for output; older ones are dropped
    std::vector<float>            a, b;                  // interleaved pairs from aHead/bHead on not yet output
    std::size_t                   aHead = 0UZ, bHead = 0UZ;
    std::vector<float>            historyA, historyB; // first channels of the aligned output, for the next estimate
    std::ptrdiff_t                lag     = 0;        // applied: pair B advanced by lag (> 0) or pair A by -lag
    std::ptrdiff_t                owed    = 0;        // part of lag not yet skipped, for want of buffered frames
    std::size_t                   dropped = 0UZ;      // frames dropped for the backlog bound, since last taken
    PairAlignment                 mode    = PairAlignment::continuous;
    bool                          locked  = false;
    std::optional<std::ptrdiff_t> candidate; // once: the previous window's estimate, applied when the next one agrees

    void unlock() noexcept {
        locked = false;
        candidate.reset();
    }

    void push(std::span<const float> frames) {
        for (std::size_t i = 0UZ; i + kChannels <= frames.size(); i += kChannels) {
            a.push_back(frames[i]);
            a.push_back(frames[i + 1UZ]);
            b.push_back(frames[i + 2UZ]);
            b.push_back(frames[i + 3UZ]);
        }
        skipOwed();
        if (const std::size_t excess = available() > maxBacklog ? available() - maxBacklog : 0UZ; excess > 0UZ) {
            aHead += 2UZ * excess;
            bHead += 2UZ * excess;
            dropped += excess;
        }
        for (auto [samples, head] : {std::pair{&a, &aHead}, std::pair{&b, &bHead}}) {
            if (*head > samples->size() / 2UZ) {
                samples->erase(samples->begin(), samples->begin() + static_cast<std::ptrdiff_t>(*head));
                *head = 0UZ;
            }
        }
    }

    void skipOwed() noexcept {
        std::vector<float>& lagging = owed > 0 ? b : a;
        std::size_t&        head    = owed > 0 ? bHead : aHead;
        const std::size_t   n       = std::min((lagging.size() - head) / 2UZ, static_cast<std::size_t>(std::abs(owed)));
        head += 2UZ * n;
        owed -= owed > 0 ? static_cast<std::ptrdiff_t>(n) : -static_cast<std::ptrdiff_t>(n);
    }

    [[nodiscard]] std::size_t available() const noexcept { return std::min(a.size() - aHead, b.size() - bHead) / 2UZ; }

    /// writes `n` <= available() aligned frames per channel; returns true when the estimate changed the applied lag
    bool pop(std::span<std::span<float>, kChannels> channels, std::size_t n) {
        for (std::size_t f = 0UZ; f < n; ++f) {
            channels[0][f] = a[aHead + 2UZ * f];
            channels[1][f] = a[aHead + 2UZ * f + 1UZ];
            channels[2][f] = b[bHead + 2UZ * f];
            channels[3][f] = b[bHead + 2UZ * f + 1UZ];
        }
        aHead += 2UZ * n;
        bHead += 2UZ * n;
        historyA.insert(historyA.end(), channels[0].begin(), channels[0].begin() + static_cast<std::ptrdiff_t>(n));
        historyB.insert(historyB.end(), channels[2].begin(), channels[2].begin() + static_cast<std::ptrdiff_t>(n));
        if (dropped > 0UZ) {
            unlock();
        }
        bool changed = false;
        if (historyA.size() >= window) {
            const bool     estimating = mode == PairAlignment::continuous || (mode == PairAlignment::once && !locked);
            const auto     estimate   = estimating ? quantisedLag(historyA, historyB, kQuantum, kMaxSteps, maxAcoustic) : std::nullopt;
            std::ptrdiff_t residual   = 0;
            if (mode == PairAlignment::continuous) {
                residual = estimate.value_or(0);
            } else if (estimating) {
                locked    = estimate.has_value() && candidate == estimate;
                residual  = locked ? *estimate : 0;
                candidate = locked ? std::nullopt : estimate;
            }
            historyA.clear();
            historyB.clear();
            if (residual != 0) {
                owed += residual;
                skipOwed();
                lag += residual;
                changed = true;
            }
        }
        return changed;
    }
};

#if defined(__EMSCRIPTEN__)
namespace wasm {

EMSCRIPTEN_KEEPALIVE inline char __em_lib_deps_od_microphones[] __attribute__((section("em_lib_deps"), aligned(1))) = "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString";

// both stereo sources go through one AudioContext into one AudioWorklet, so their samples arrive in the same render quanta
// clang-format off
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
inline void openPairs(std::uintptr_t block, const char* labelA, const char* labelB, double sampleRate) {
    MAIN_THREAD_EM_ASM({ ((block, labelA, labelB, sampleRate) => {
    const labels = [UTF8ToString(labelA), UTF8ToString(labelB)];
    const state  = { stopped: false };
    (globalThis.__odMicrophonePairs = globalThis.__odMicrophonePairs || new Map()).set(block, state);
    const fail = (message) => {
        if (state.stopped) { return; }
        const text = String(message);
        const size = lengthBytesUTF8(text) + 1;
        stringToUTF8(text, _od_microphones_error_buffer(block, size), size);
        _od_microphones_failed(block);
    };
    (async () => {
        const live = function() { if (state.stopped) { throw new Error('stopped'); } }; // after every await: a stop may have come
        (await navigator.mediaDevices.getUserMedia({ audio: true })).getTracks().forEach(function(t) { t.stop(); });
        live();
        const inputs = (await navigator.mediaDevices.enumerateDevices()).filter(function(d) { return d.kind === 'audioinput'; });
        live();
        const list   = inputs.map(function(d) { return (d.label || 'microphone').replaceAll('\n', ' ') + ' [' + d.deviceId + ']'; }).join('\n');
        const size   = lengthBytesUTF8(list) + 1;
        stringToUTF8(list, _od_microphones_devices_buffer(block, size), size);
        _od_microphones_devices_reported(block);
        const pairs  = labels.map(function(l) { return inputs.find(function(d) { return l.startsWith('@id:') ? d.deviceId === l.slice(4) : d.label.includes(l); }); });
        if (pairs.some(function(d) { return !d; })) { throw new Error('microphone pairs ' + labels.join(', ') + ' not found (uma16-browser-pairs.sh setup)'); }
        state.streams = await Promise.all(pairs.map(function(d) { return navigator.mediaDevices.getUserMedia({ audio: { deviceId: { exact: d.deviceId }, channelCount: { exact: 2 }, echoCancellation: false, noiseSuppression: false, autoGainControl: false } }); }));
        if (state.stopped) { state.streams.forEach(function(s) { s.getTracks().forEach(function(t) { t.stop(); }); }); return; }
        state.context = new AudioContext({ sampleRate: sampleRate });
        await state.context.resume();
        live();
        const merger = state.context.createChannelMerger(4);
        state.streams.forEach(function(s, k) {
            const split = state.context.createChannelSplitter(2);
            state.context.createMediaStreamSource(s).connect(split);
            split.connect(merger, 0, 2 * k);
            split.connect(merger, 1, 2 * k + 1);
        });
        const code = 'class OdPairs extends AudioWorkletProcessor { constructor() { super(); this.buf = new Float32Array(4096); this.n = 0; }'
            + ' process(inputs) { const i = inputs[0]; if (i.length === 4) { for (let f = 0; f < i[0].length; ++f, ++this.n) { for (let c = 0; c < 4; ++c) { this.buf[4 * this.n + c] = i[c][f]; } }'
            + ' if (this.n === 1024) { this.port.postMessage(this.buf); this.buf = new Float32Array(4096); this.n = 0; } } return true; } }'
            + ' registerProcessor("od-pairs", OdPairs);';
        await state.context.audioWorklet.addModule(URL.createObjectURL(new Blob([code], { type: 'text/javascript' })));
        live();
        const node = new AudioWorkletNode(state.context, 'od-pairs', { numberOfInputs: 1, numberOfOutputs: 0, channelCount: 4, channelCountMode: 'explicit', channelInterpretation: 'discrete' });
        merger.connect(node);
        node.port.onmessage = function(e) {
            if (state.stopped) { return; }
            const ptr = _od_microphones_buffer(block, e.data.length);
            new Float32Array(wasmMemory.buffer, ptr, e.data.length).set(e.data);
            _od_microphones_written(block, e.data.length);
        };
    })().catch(function(error) {
        if (state.stopped) { // closePairs ran meanwhile: release what was opened after it
            if (state.streams) { state.streams.forEach(function(s) { s.getTracks().forEach(function(t) { t.stop(); }); }); }
            if (state.context) { state.context.close(); }
            return;
        }
        fail(error);
    });
    })($0, $1, $2, $3); }, block, labelA, labelB, sampleRate);
}

inline void closePairs(std::uintptr_t block) {
    MAIN_THREAD_EM_ASM({ ((block) => {
    const all   = globalThis.__odMicrophonePairs;
    const state = all && all.get(block);
    if (!state) { return; }
    state.stopped = true;
    if (state.streams) { state.streams.forEach(function(s) { s.getTracks().forEach(function(t) { t.stop(); }); }); }
    if (state.context) { state.context.close(); }
    all.delete(block);
    })($0); }, block);
}
#pragma clang diagnostic pop
// clang-format on

} // namespace wasm
#endif

/// the browser side of one block: opened by start(), closed by stop() or, at the latest, when the block is destroyed
struct BrowserCapture {
    std::uintptr_t block = 0U;

    BrowserCapture()                                 = default;
    BrowserCapture(const BrowserCapture&)            = delete;
    BrowserCapture& operator=(const BrowserCapture&) = delete;
    ~BrowserCapture() { close(); }

    void close() noexcept {
#if defined(__EMSCRIPTEN__)
        if (block != 0U) {
            wasm::closePairs(std::exchange(block, 0U));
        }
#endif
    }
};

struct BrowserMicrophonePairs : gr::Block<BrowserMicrophonePairs> {
    using Description = gr::Doc<R""(@brief Four time-aligned microphone streams (pair A left/right, pair B left/right) from two browser stereo sources, e.g. PipeWire remap sources of one array.

Chrome captures at most two channels per source; pair B is shifted by whole render quanta until it lines up with pair A, estimated every half second.
`align: once` holds the first estimate two consecutive windows agree on, until frames are dropped or the sources reopen: for first microphones of the pairs more than about 1.6 m (48 kHz) or 0.7 m (96 kHz) apart.
At most one second is held: older frames are dropped and counted in `gr:n_dropped_samples`.
Browser (WASM) only; elsewhere the outputs stay empty.)"">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    std::array<gr::PortOut<float>, PairAligner::kChannels> out;

    A<std::string, "pair a", gr::Visible, gr::Doc<"label substring or '@id:<deviceId>' of the first stereo source; a change reopens both">>     pair_a      = "UMA16_PAIR_A";
    A<std::string, "pair b", gr::Visible, gr::Doc<"label substring or '@id:<deviceId>' of the second stereo source; a change reopens both">>    pair_b      = "UMA16_PAIR_B";
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"of the browser's audio context">>                                                          sample_rate = 48000.f;
    A<std::vector<std::string>, "available_devices", gr::Doc<"the browser's audio inputs as 'label [deviceId]'">>                               available_devices;
    A<PairAlignment, "align", gr::Visible, gr::Doc<"continuous: re-estimated every half second; once: held after two agreeing estimates; off">> align = PairAlignment::continuous;
    A<std::int64_t, "lag", gr::Unit<"samples">, gr::Doc<"applied, read-only: pair B advanced by lag (> 0) or pair A by -lag">>                  lag   = 0;

    GR_MAKE_REFLECTABLE(BrowserMicrophonePairs, out, pair_a, pair_b, sample_rate, available_devices, align, lag);

    PairAligner                             _aligner;
    std::vector<float>                      _staging;              // written by JS on the main thread
    std::vector<float>                      _pending;              // guarded by _mutex, at most _aligner.maxBacklog frames
    std::size_t                             _droppedPending = 0UZ; // frames, guarded by _mutex
    std::vector<float>                      _taken;                // _pending swapped out, processed without the lock
    std::string                             _errorText;            // written by JS on the main thread
    std::string                             _devicesText;          // written by JS on the main thread
    std::optional<std::vector<std::string>> _devices;              // reported and not yet staged, guarded by _mutex
    std::string                             _error;                // guarded by _mutex
    std::mutex                              _mutex;
    bool                                    _tagged = false;
    BrowserCapture                          _capture;

    void start() {
        _tagged              = false;
        const auto framesOf  = [rate = static_cast<double>(sample_rate.value)](double seconds) { return static_cast<std::size_t>(std::lround(seconds * rate)); };
        _aligner.maxAcoustic = framesOf(PairAligner::kMaxAcousticSeconds);
        _aligner.window      = framesOf(PairAligner::kWindowSeconds);
        _aligner.maxBacklog  = framesOf(PairAligner::kMaxBacklogSeconds);
        _aligner.mode        = align.value;
        _aligner.unlock();
#if defined(__EMSCRIPTEN__)
        _capture.block = reinterpret_cast<std::uintptr_t>(this);
        wasm::openPairs(_capture.block, pair_a.value.c_str(), pair_b.value.c_str(), static_cast<double>(sample_rate.value));
#endif
    }

    void stop() { _capture.close(); }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        const auto changed = [&](const char* key) {
            const auto now = newSettings.find(std::pmr::string(key));
            const auto was = oldSettings.find(std::pmr::string(key));
            return now != newSettings.end() && (was == oldSettings.end() || was->second != now->second);
        };
        if (_capture.block != 0U && (changed("pair_a") || changed("pair_b"))) {
            _capture.close();
            start();
        } else if (changed("align")) {
            _aligner.mode = align.value;
            _aligner.unlock();
        }
    }

    template<gr::OutputSpanLike TOut>
    [[nodiscard]] gr::work::Status processBulk(const std::span<TOut>& outputs) {
        std::string                             error;
        std::optional<std::vector<std::string>> devices;
        {
            std::lock_guard lock(_mutex);
            std::swap(_pending, _taken);
            _aligner.dropped += std::exchange(_droppedPending, 0UZ);
            error   = std::exchange(_error, {});
            devices = std::exchange(_devices, std::nullopt);
        }
        if (devices) { // staged, not assigned: settings subscribers such as the UI are notified
            std::ignore = this->settings().setStaged({{"available_devices", gr::Tensor<gr::pmt::Value>(gr::data_from, *devices | std::views::transform([](const std::string& d) { return gr::pmt::Value(d); }) | std::ranges::to<std::vector>())}});
        }
        if (!error.empty()) {
            this->emitErrorMessage("BrowserMicrophonePairs", gr::Error(std::move(error)));
        }
        _aligner.push(_taken);
        _taken.clear();
        std::size_t n = _aligner.available();
        for (const auto& output : outputs) {
            n = std::min(n, output.size());
        }
        if (n == 0UZ) {
            for (auto& output : outputs) {
                output.publish(0UZ);
            }
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS; // written() wakes the scheduler
        }
        std::array<std::span<float>, PairAligner::kChannels> channels;
        for (std::size_t c = 0UZ; c < channels.size(); ++c) {
            channels[c] = std::span<float>(outputs[c].data(), n);
        }
        if (_aligner.pop(channels, n)) {
            gr::log::info("BrowserMicrophonePairs: pair B lags pair A by {} samples", _aligner.lag);
            std::ignore = this->settings().setStaged({{"lag", std::int64_t{_aligner.lag}}});
        }
        gr::property_map tag;
        if (!std::exchange(_tagged, true)) {
            gr::tag::put(tag, gr::tag::kSampleRate, sample_rate.value);
            gr::tag::put(tag, gr::tag::kTriggerTime, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count())); // UTC anchor for downstream triggers' event times
        }
        if (const std::size_t dropped = std::exchange(_aligner.dropped, 0UZ); dropped > 0UZ) {
            gr::tag::put(tag, gr::tag::kNDroppedSamples, static_cast<gr::Size_t>(dropped));
        }
        for (auto& output : outputs) {
            if (!tag.empty()) {
                output.publishTag(tag, 0UZ);
            }
            output.publish(n);
        }
        return gr::work::Status::OK;
    }

    [[nodiscard]] float* buffer(std::size_t size) {
        _staging.resize(size);
        return _staging.data();
    }

    void written(std::size_t size) {
        {
            std::lock_guard   lock(_mutex);
            const std::size_t frames        = std::min(size, _staging.size()) / PairAligner::kChannels;
            const std::size_t capacity      = _aligner.maxBacklog;
            const std::size_t kept          = std::min(frames, capacity);
            const std::size_t held          = _pending.size() / PairAligner::kChannels;
            const std::size_t oldestDropped = held + kept > capacity ? held + kept - capacity : 0UZ;
            _pending.erase(_pending.begin(), _pending.begin() + static_cast<std::ptrdiff_t>(oldestDropped * PairAligner::kChannels));
            _droppedPending += frames - kept + oldestDropped;
            const auto from = _staging.begin() + static_cast<std::ptrdiff_t>((frames - kept) * PairAligner::kChannels);
            _pending.insert(_pending.end(), from, from + static_cast<std::ptrdiff_t>(kept * PairAligner::kChannels));
        }
        this->notifyProgress();
    }

    [[nodiscard]] char* errorBuffer(std::size_t size) {
        _errorText.assign(size, '\0');
        return _errorText.data();
    }

    void failed() {
        {
            std::lock_guard lock(_mutex);
            _error = _errorText.c_str();
        }
        this->notifyProgress();
    }

    [[nodiscard]] char* devicesBuffer(std::size_t size) {
        _devicesText.assign(size, '\0');
        return _devicesText.data();
    }

    void devicesReported() {
        {
            std::lock_guard lock(_mutex);
            _devices = std::string_view(_devicesText.c_str()) | std::views::split('\n') | std::views::transform([](auto line) { return std::string(line.begin(), line.end()); }) | std::views::filter([](const std::string& line) { return !line.empty(); }) | std::ranges::to<std::vector>();
        }
        this->notifyProgress();
    }
};

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE inline float* od_microphones_buffer(std::uintptr_t block, std::size_t size) { return reinterpret_cast<BrowserMicrophonePairs*>(block)->buffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void   od_microphones_written(std::uintptr_t block, std::size_t size) { reinterpret_cast<BrowserMicrophonePairs*>(block)->written(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline char*  od_microphones_error_buffer(std::uintptr_t block, std::size_t size) { return reinterpret_cast<BrowserMicrophonePairs*>(block)->errorBuffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void   od_microphones_failed(std::uintptr_t block) { reinterpret_cast<BrowserMicrophonePairs*>(block)->failed(); }
extern "C" EMSCRIPTEN_KEEPALIVE inline char*  od_microphones_devices_buffer(std::uintptr_t block, std::size_t size) { return reinterpret_cast<BrowserMicrophonePairs*>(block)->devicesBuffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void   od_microphones_devices_reported(std::uintptr_t block) { reinterpret_cast<BrowserMicrophonePairs*>(block)->devicesReported(); }
#endif

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP
