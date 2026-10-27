#ifndef EMSCRIPTENHELPER_HPP
#define EMSCRIPTENHELPER_HPP

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#endif
#include <cstdint>
#include <format>
#include <print>
#include <string>
#include <string_view>

enum class ExecutionMode : std::uint8_t { Async = 0, Sync };

constexpr bool isWebAssembly() noexcept {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

inline bool isMainThread() {
#ifdef __EMSCRIPTEN__
    return emscripten_is_main_runtime_thread();
#else
    return true; // Native: assume single-threaded or main thread
#endif
}

inline bool isTabVisible() {
#ifdef __EMSCRIPTEN__
    EmscriptenVisibilityChangeEvent status;
    if (emscripten_get_visibility_status(&status) == EMSCRIPTEN_RESULT_SUCCESS) {
        return !status.hidden;
    }
#endif
    return true;
}

#ifdef __EMSCRIPTEN__
inline bool em_visibilitychange_callback(int, const EmscriptenVisibilityChangeEvent* evt, void*) {
    constexpr int visibleFPS = 0; // 0 = requestAnimationFrame
    constexpr int hiddenFPS  = 5; // ~200ms refresh when hidden
    if (evt->hidden) {
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 1000 / hiddenFPS);
        std::println("[MainLoop] Switched to setTimeout {}ms (hidden)", 1000 / hiddenFPS);
    } else {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, visibleFPS);
        std::println("[MainLoop] Switched to requestAnimationFrame (visible)");
    }
    return true;
}
#endif

/// Writes `text` to the system clipboard (WASM only). SDL3's Emscripten backend has no clipboard support, so
/// ImGui::SetClipboardText never leaves the page; this must run shortly after a user click for the browser to allow it.
inline void copyToBrowserClipboard([[maybe_unused]] const std::string& text) {
#ifdef __EMSCRIPTEN__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
    // clang-format off
    // MAIN_THREAD: navigator/document are only valid on the browser main thread (pthreads build).
    MAIN_THREAD_EM_ASM(
        {
            const text = UTF8ToString($0);
            const fallbackCopy = function () {
                const previouslyFocused = document.activeElement;
                const area = document.createElement("textarea");
                area.value = text;
                area.style.position = "fixed";
                area.style.opacity = "0";
                document.body.appendChild(area);
                area.select();
                try {
                    document.execCommand("copy");
                } catch (error) {
                    console.error("copy to clipboard failed", error);
                }
                area.remove();
                if (previouslyFocused && previouslyFocused.focus) {
                    previouslyFocused.focus();
                }
            };
            if (navigator.clipboard && window.isSecureContext) {
                navigator.clipboard.writeText(text).catch(fallbackCopy);
            } else {
                fallbackCopy();
            }
        },
        text.c_str());
    // clang-format on
#pragma clang diagnostic pop
#endif
}

// clang-format off
inline void listPersistentFiles([[maybe_unused]] bool recursive = true) {
#ifdef __EMSCRIPTEN__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
    EM_ASM_(
        {
            function listDir(path, recursive, indent = "") {
                try {
                    const entries = FS.readdir(path);
                    for (let entry of entries) {
                        if (entry === '.' || entry === '..') {
                            continue;
                        }
                        const fullPath = path + (path.endsWith('/') ? "" : "/") + entry;
                        const stat     = FS.stat(fullPath);
                        if (FS.isDir(stat.mode)) {
                            console.log(indent + '[Dir] ' + fullPath);
                            if (recursive) {
                                listDir(fullPath, recursive, indent + '  ');
                            }
                        } else {
                            console.log(indent + '[File] ' + fullPath);
                        }
                    }
                } catch (e) {
                    console.error('Error listing directory:', path, e);
                }
            }
            listDir('/', $0 !== 0);
        },
        recursive ? 1 : 0);
#pragma clang diagnostic pop
#else

#endif
}
// clang-format on

#endif // EMSCRIPTENHELPER_HPP
