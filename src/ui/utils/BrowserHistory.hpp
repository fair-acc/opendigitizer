#ifndef OPENDIGITIZER_UTILS_BROWSER_HISTORY_HPP
#define OPENDIGITIZER_UTILS_BROWSER_HISTORY_HPP

#include "AppLocation.hpp"

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#endif

namespace DigitizerUi {

/// Keeps the browser URL fragment and the application location in sync (WASM) and behaves as an
/// in-memory history otherwise. Browser-initiated changes (hand-edited URL, back, forward) are queued
/// and picked up on the main loop with takeBrowserChange(). Emscripten has no hashchange/popstate
/// callback, so on WASM a timer compares location.hash with the last seen value.
struct BrowserHistory {
    BrowserHistory() = default;
    ~BrowserHistory() { uninstall(); }

    BrowserHistory(const BrowserHistory&)            = delete;
    BrowserHistory& operator=(const BrowserHistory&) = delete;

    /// called after a browser change was queued, e.g. to wake an idle render loop
    std::function<void()> onBrowserChange;

    void install() {
#ifdef __EMSCRIPTEN__
        _lastHash = readHash();
        _current  = Digitizer::AppLocation::parse(_lastHash);
        _timer    = emscripten_set_interval(&BrowserHistory::poll, kPollIntervalMs, this);
#endif
    }

    void uninstall() {
#ifdef __EMSCRIPTEN__
        if (_timer != 0) {
            emscripten_clear_interval(_timer);
            _timer = 0;
        }
#endif
    }

    [[nodiscard]] const Digitizer::AppLocation& current() const noexcept { return _current; }

    /// adds a history entry; does nothing when the location is already current
    void push(const Digitizer::AppLocation& location) { write(location, /*replace=*/false); }

    /// rewrites the current history entry
    void replace(const Digitizer::AppLocation& location) { write(location, /*replace=*/true); }

    /// location the browser moved to since the last call, if it differs from what was last applied
    [[nodiscard]] std::optional<Digitizer::AppLocation> takeBrowserChange() { return std::exchange(_pendingBrowserChange, std::nullopt); }

    /// absolute link that opens `location`; relative ("#/...") when there is no page, e.g. natively
    [[nodiscard]] std::string linkTo(const Digitizer::AppLocation& location) const { return pageBase() + "#" + location.toHash(); }

    /// browser-reported change, public so the poll and tests share one entry point
    void onBrowserLocation(const Digitizer::AppLocation& location) {
        if (location == _current) { // hashchange and popstate both fire for back/forward
            return;
        }
        _current              = location;
        _pendingBrowserChange = location;
    }

private:
    Digitizer::AppLocation                _current;
    std::optional<Digitizer::AppLocation> _pendingBrowserChange;
#ifdef __EMSCRIPTEN__
    static constexpr double kPollIntervalMs = 200.;
    std::string             _lastHash; // raw location.hash seen last, avoids parsing on every tick
    long                    _timer = 0;
#endif

    void write(const Digitizer::AppLocation& location, [[maybe_unused]] bool replace) {
        if (location == _current) {
            return;
        }
        _current = location;
        _pendingBrowserChange.reset(); // a newer local state supersedes an unprocessed browser change
        [[maybe_unused]] const std::string hash = location.toHash();
#ifdef __EMSCRIPTEN__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
        // clang-format off
        // MAIN_THREAD: window/history are only valid on the browser main thread (pthreads build).
        MAIN_THREAD_EM_ASM(
            {
                const url = window.location.pathname + window.location.search + "#" + UTF8ToString($0);
                if ($1) {
                    history.replaceState(null, "", url);
                } else {
                    history.pushState(null, "", url);
                }
            },
            hash.c_str(), replace ? 1 : 0);
        // clang-format on
#pragma clang diagnostic pop
        _lastHash = readHash();
#endif
    }

#ifndef __EMSCRIPTEN__
    [[nodiscard]] static std::string pageBase() { return {}; }
#else
    [[nodiscard]] static std::string pageBase() { return browserText(/*pageBase=*/true); }

    [[nodiscard]] static std::string readHash() { return browserText(/*pageBase=*/false); }

    [[nodiscard]] static std::string browserText(bool pageBase) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
        // clang-format off
        char* raw = static_cast<char*>(MAIN_THREAD_EM_ASM_PTR(
            {
                const text = $0 ? window.location.origin + window.location.pathname + window.location.search : window.location.hash || "";
                const size = lengthBytesUTF8(text) + 1;
                const ptr  = _malloc(size);
                stringToUTF8(text, ptr, size);
                return ptr;
            },
            pageBase ? 1 : 0));
        // clang-format on
#pragma clang diagnostic pop
        std::string text{raw != nullptr ? raw : ""};
        std::free(raw);
        return text;
    }

    static void poll(void* self) {
        auto& history = *static_cast<BrowserHistory*>(self);
        auto  hash    = readHash();
        if (hash == history._lastHash) {
            return;
        }
        history._lastHash = std::move(hash);
        history.onBrowserLocation(Digitizer::AppLocation::parse(history._lastHash));
        if (history._pendingBrowserChange && history.onBrowserChange) {
            history.onBrowserChange();
        }
    }
#endif
};

} // namespace DigitizerUi

#endif // OPENDIGITIZER_UTILS_BROWSER_HISTORY_HPP
