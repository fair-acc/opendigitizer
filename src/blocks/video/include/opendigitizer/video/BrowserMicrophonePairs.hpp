#ifndef OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP
#define OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
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
    const auto          q     = static_cast<std::ptrdiff_t>(quantum);
    const auto          steps = static_cast<std::ptrdiff_t>(maxSteps), acoustic = static_cast<std::ptrdiff_t>(maxAcoustic);
    std::vector<double> scores; // per multiple: the best |correlation| within the acoustic neighbourhood
    for (std::ptrdiff_t k = -steps; k <= steps; ++k) {
        double score = 0.0;
        for (std::ptrdiff_t d = -acoustic; d <= acoustic; ++d) {
            double sum = 0.0;
            for (std::ptrdiff_t i = reach; i < n - reach; ++i) {
                sum += static_cast<double>(a[static_cast<std::size_t>(i)]) * static_cast<double>(b[static_cast<std::size_t>(i + k * q + d)]);
            }
            score = std::max(score, std::abs(sum));
        }
        scores.push_back(score);
    }
    const auto best   = std::ranges::max_element(scores);
    const auto bestK  = std::ranges::distance(scores.begin(), best) - steps;
    double     second = 0.0;
    for (auto it = scores.begin(); it != scores.end(); ++it) {
        second = it == best ? second : std::max(second, *it);
    }
    if (*best <= 0.0 || *best < confidence * second) {
        return std::nullopt;
    }
    return bestK * q;
}

/// four time-aligned microphone streams (pair A left/right, pair B left/right) from two stereo sources; pushed blocks of
/// interleaved frames, pair B shifted by whole quanta until its first channel lines up with pair A's
struct PairAligner {
    static constexpr std::size_t kChannels = 4UZ;

    std::size_t        quantum     = 256UZ;
    std::size_t        maxSteps    = 4UZ;
    std::size_t        maxAcoustic = 24UZ;
    std::size_t        window      = 24000UZ; // frames per estimate
    std::size_t        maxBacklog  = 48000UZ; // frames held for output; older ones are dropped
    std::vector<float> a, b;                  // interleaved pairs from aHead/bHead on not yet output
    std::size_t        aHead = 0UZ, bHead = 0UZ;
    std::vector<float> historyA, historyB; // first channels of the aligned output, for the next estimate
    std::ptrdiff_t     lag     = 0;        // applied: pair B advanced by lag (> 0) or pair A by -lag
    std::size_t        dropped = 0UZ;      // frames dropped for the backlog bound, since last taken

    void push(std::span<const float> frames) {
        for (std::size_t i = 0UZ; i + kChannels <= frames.size(); i += kChannels) {
            a.push_back(frames[i]);
            a.push_back(frames[i + 1UZ]);
            b.push_back(frames[i + 2UZ]);
            b.push_back(frames[i + 3UZ]);
        }
        if (const std::size_t excess = available() > maxBacklog ? available() - maxBacklog : 0UZ; excess > 0UZ) {
            aHead += 2UZ * excess;
            bHead += 2UZ * excess;
            dropped += excess;
        }
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
        bool changed = false;
        if (historyA.size() >= window) {
            const std::ptrdiff_t residual = quantisedLag(historyA, historyB, quantum, maxSteps, maxAcoustic).value_or(0);
            historyA.clear();
            historyB.clear();
            if (residual != 0) {
                std::vector<float>& lagging = residual > 0 ? b : a;
                std::size_t&        head    = residual > 0 ? bHead : aHead;
                head                        = std::min(lagging.size(), head + 2UZ * static_cast<std::size_t>(std::abs(residual)));
                lag += residual;
                changed = true;
            }
        }
        for (auto [samples, head] : {std::pair{&a, &aHead}, std::pair{&b, &bHead}}) {
            if (*head > samples->size() / 2UZ) {
                samples->erase(samples->begin(), samples->begin() + static_cast<std::ptrdiff_t>(*head));
                *head = 0UZ;
            }
        }
        return changed;
    }
};

#if defined(__EMSCRIPTEN__)
namespace wasm {

EMSCRIPTEN_KEEPALIVE inline char __em_lib_deps_od_microphones[] __attribute__((section("em_lib_deps"), aligned(1))) = "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString";

// browser side: the two stereo sources (labels, e.g. PipeWire remap sources) via getUserMedia without processing, merged in
// one AudioContext into one AudioWorklet, so both arrive in the same render quanta; 1024-frame blocks of interleaved
// 4-channel samples are copied into the WASM heap
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
        const pairs  = labels.map(function(l) { return inputs.find(function(d) { return d.label.includes(l); }); });
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

    BrowserCapture() = default;
    BrowserCapture(BrowserCapture&& other) noexcept : block(std::exchange(other.block, 0U)) {}
    BrowserCapture& operator=(BrowserCapture&& other) noexcept {
        if (this != &other) {
            close();
            block = std::exchange(other.block, 0U);
        }
        return *this;
    }
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
    using Description = gr::Doc<R""(@brief Four time-aligned microphone streams from two browser stereo sources, e.g. PipeWire remap sources of one array (Chrome captures at most two channels per source).

Outputs pair A left/right and pair B left/right at `sample_rate`; pair B is shifted by whole render quanta until it lines up with
pair A (re-estimated every half second). `pair_a` and `pair_b` are label substrings of the sources. At most one second is held:
older frames are dropped and counted in `gr:n_dropped_samples`. Browser (WASM) only; elsewhere the outputs stay empty.)"">;
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    std::array<gr::PortOut<float>, PairAligner::kChannels> out;

    A<std::string, "pair a", gr::Visible, gr::Doc<"label (substring) of the first stereo source">>  pair_a      = "UMA16_PAIR_A";
    A<std::string, "pair b", gr::Visible, gr::Doc<"label (substring) of the second stereo source">> pair_b      = "UMA16_PAIR_B";
    A<float, "sample_rate", gr::Unit<"Hz">, gr::Doc<"of the browser's audio context">>              sample_rate = 48000.f;

    GR_MAKE_REFLECTABLE(BrowserMicrophonePairs, out, pair_a, pair_b, sample_rate);

    PairAligner        _aligner;
    std::vector<float> _staging;              // written by JS on the main thread
    std::vector<float> _pending;              // guarded by _mutex, at most _aligner.maxBacklog frames
    std::size_t        _droppedPending = 0UZ; // frames, guarded by _mutex
    std::vector<float> _taken;                // _pending swapped out, processed without the lock
    std::string        _errorText;            // written by JS on the main thread
    std::string        _error;                // guarded by _mutex
    std::mutex         _mutex;
    bool               _tagged = false;
    BrowserCapture     _capture;

    void start() {
        _tagged = false;
#if defined(__EMSCRIPTEN__)
        _capture.block = reinterpret_cast<std::uintptr_t>(this);
        wasm::openPairs(_capture.block, pair_a.value.c_str(), pair_b.value.c_str(), static_cast<double>(sample_rate.value));
#endif
    }

    void stop() { _capture.close(); }

    template<gr::OutputSpanLike TOut>
    [[nodiscard]] gr::work::Status processBulk(const std::span<TOut>& outputs) {
        std::string error;
        {
            std::lock_guard lock(_mutex);
            std::swap(_pending, _taken);
            _aligner.dropped += std::exchange(_droppedPending, 0UZ);
            error = std::exchange(_error, {});
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
            return gr::work::Status::INSUFFICIENT_INPUT_ITEMS; // nothing from the browser yet: written() wakes the scheduler
        }
        std::array<std::span<float>, PairAligner::kChannels> channels;
        for (std::size_t c = 0UZ; c < channels.size(); ++c) {
            channels[c] = std::span<float>(outputs[c].data(), n);
        }
        if (_aligner.pop(channels, n)) {
            gr::log::info("BrowserMicrophonePairs: pair B lags pair A by {} samples", _aligner.lag);
        }
        gr::property_map tag;
        if (!std::exchange(_tagged, true)) {
            gr::tag::put(tag, gr::tag::kSampleRate, sample_rate.value);
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
        this->progress->incrementAndGet();
        this->progress->notify_all();
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
        this->progress->incrementAndGet();
        this->progress->notify_all();
    }
};

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE inline float* od_microphones_buffer(std::uintptr_t block, std::size_t size) { return reinterpret_cast<BrowserMicrophonePairs*>(block)->buffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void   od_microphones_written(std::uintptr_t block, std::size_t size) { reinterpret_cast<BrowserMicrophonePairs*>(block)->written(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline char*  od_microphones_error_buffer(std::uintptr_t block, std::size_t size) { return reinterpret_cast<BrowserMicrophonePairs*>(block)->errorBuffer(size); }
extern "C" EMSCRIPTEN_KEEPALIVE inline void   od_microphones_failed(std::uintptr_t block) { reinterpret_cast<BrowserMicrophonePairs*>(block)->failed(); }
#endif

} // namespace opendigitizer::video

#endif // OPENDIGITIZER_VIDEO_BROWSERMICROPHONEPAIRS_HPP
