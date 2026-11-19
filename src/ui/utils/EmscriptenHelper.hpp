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
#include <ranges>
#include <string>
#include <string_view>

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE inline char __em_lib_deps_od_ui[] __attribute__((section("em_lib_deps"), aligned(1))) = "$UTF8ToString"; // inline: one per program, unlike EM_JS_DEPS
#endif

/// the page asked for the UI's browser-test hooks (globalThis.odTestHooks, set before the module starts); read once
inline bool browserTestHooks() {
#ifdef __EMSCRIPTEN__
    static const bool enabled = EM_ASM_INT({ return globalThis.odTestHooks ? 1 : 0; }) != 0;
    return enabled;
#else
    return false;
#endif
}

[[nodiscard]] inline std::string jsonString(std::string_view text) {
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
            quoted += c;
        } else if (static_cast<unsigned char>(c) < 0x20U) {
            quoted += std::format("\\u{:04x}", static_cast<unsigned int>(c));
        } else {
            quoted += c;
        }
    }
    return quoted + '"';
}

[[nodiscard]] inline std::string jsonArray(std::ranges::input_range auto&& strings) {
    std::string json = "[";
    for (const std::string_view entry : strings) {
        json += std::format("{}{}", json.size() > 1UZ ? "," : "", jsonString(entry));
    }
    return json + ']';
}

/// browser tests read `json` (finite numbers only) as globalThis[table][key]; callers check browserTestHooks() before building it
inline void publishForBrowserTests([[maybe_unused]] std::string_view table, [[maybe_unused]] std::string_view key, [[maybe_unused]] std::string_view json) {
#ifdef __EMSCRIPTEN__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
    // clang-format off
    EM_ASM({ ((table, key, json) => { (globalThis[table] = globalThis[table] || {})[key] = JSON.parse(json); })(UTF8ToString($0, $1), UTF8ToString($2, $3), UTF8ToString($4, $5)); },
        table.data(), static_cast<int>(table.size()), key.data(), static_cast<int>(key.size()), json.data(), static_cast<int>(json.size()));
    // clang-format on
#pragma clang diagnostic pop
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

/// Update or clear `#dashboard=` in the browser URL without reloading (WASM only).
inline void setBrowserDashboardFragment([[maybe_unused]] std::string_view dashboardName) {
#ifdef __EMSCRIPTEN__
    const std::string name{dashboardName};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdollar-in-identifier-extension"
    // clang-format off
    // MAIN_THREAD: window/history are only valid on the browser main thread (pthreads build).
    // Use "" (not '') in JS — the C preprocessor rejects empty character literals.
    MAIN_THREAD_EM_ASM(
        {
            const name = UTF8ToString($0);
            const hash = (window.location.hash || "").replace(/^#/, "");
            const params = hash.length
                ? hash.split("&").filter(function (p) { return p.length && p.indexOf("dashboard=") !== 0; })
                : [];
            if (name.length) {
                params.unshift("dashboard=" + name);
            }
            const fragment = params.join("&");
            const url = window.location.pathname + window.location.search + (fragment ? "#" + fragment : "");
            history.replaceState(null, "", url);
        },
        name.c_str());
    // clang-format on
#pragma clang diagnostic pop
#endif
}

#endif // EMSCRIPTENHELPER_HPP
