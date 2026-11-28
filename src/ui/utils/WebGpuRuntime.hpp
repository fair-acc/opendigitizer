#ifndef OPENDIGITIZER_UTILS_WEBGPURUNTIME_HPP
#define OPENDIGITIZER_UTILS_WEBGPURUNTIME_HPP

#include <atomic>
#include <cstdint>
#include <format>
#include <limits>
#include <string>
#include <string_view>

#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/device/DeviceContextWebGpu.hpp>
#include <gnuradio-4.0/thread/thread_pool.hpp>

#include "../common/FramePacer.hpp"
#include "Browser.hpp"

namespace gr::od {

namespace detail {
[[nodiscard]] inline std::atomic<bool>& webGpuRuntimeSettledFlag() noexcept {
    static std::atomic<bool> settled{!gr::device::kHasWebGpu};
    return settled;
}
} // namespace detail

/// The browser main thread owns the GPU device and must return to its event loop to settle the device's promises,
/// so registration runs on a pool thread while the UI keeps drawing; elsewhere it is settled at once.
/// Under the test hooks a bare 'gpu' is also answered by gpu:swift (CI's headless Chrome has only SwiftShader).
inline void startWebGpuRuntime() {
    if constexpr (gr::device::kHasWebGpu) {
        const bool testHooks = browserTestHooks(); // here: it reads the page's globalThis, which a pool thread does not see
        gr::thread_pool::Manager::defaultIoPool()->execute([testHooks] {
            const bool served   = gr::device::registerWebGpuRuntime();
            auto&      registry = gr::device::DeviceContextRegistry::instance();
            if (served && testHooks && !registry.isServedExactly("gpu:webgpu") && registry.isServedExactly("gpu:swift")) {
                registry.registerAlias("gpu:webgpu", "gpu:swift:0");
                gr::log::info("[WebGPU] test hooks: 'gpu' is answered by gpu:swift (SwiftShader)");
            }
            if (served) {
                gr::log::info("[WebGPU] runtime registered");
            } else {
                gr::log::warning("[WebGPU] not served: blocks on 'gpu' run on the CPU");
            }
            detail::webGpuRuntimeSettledFlag().store(true, std::memory_order_release);
            globalFramePacer().requestFrame();
        });
    }
}

/// true once registration has finished, either way: a graph started before would resolve 'gpu' to the CPU
[[nodiscard]] inline bool webGpuRuntimeSettled() noexcept { return detail::webGpuRuntimeSettledFlag().load(std::memory_order_acquire); }

/// for browser tests, when they asked for the hooks: `globalThis.odWebGpu.state` = {served, dispatches},
/// the WebGPU domain served (or "") and how many kernels ran on its adapter, not on the CPU; published when either changes
inline void publishWebGpuStateForBrowserTests() {
    if constexpr (gr::device::kHasWebGpu) {
        if (!browserTestHooks() || !webGpuRuntimeSettled()) {
            return;
        }
        static std::string   publishedDomain;
        static std::uint64_t publishedDispatches = std::numeric_limits<std::uint64_t>::max();
        auto&                registry            = gr::device::DeviceContextRegistry::instance();
        std::string_view     served;
        std::uint64_t        dispatches = 0U;
        // gpu:swift first: under the test hooks gpu:webgpu may be its alias
        for (const std::string_view domain : {std::string_view{"gpu:swift"}, std::string_view{"gpu:webgpu"}}) {
            if (registry.isServedExactly(domain)) {
                if (const auto* context = registry.tryResolve(domain)->as<gr::device::DeviceContextWebGpu>(); context != nullptr) {
                    served     = domain;
                    dispatches = context->gpuDispatches.load(std::memory_order_relaxed);
                }
                break;
            }
        }
        if (served == publishedDomain && dispatches == publishedDispatches) {
            return;
        }
        publishedDomain     = served;
        publishedDispatches = dispatches;
        publishForBrowserTests("odWebGpu", "state", std::format(R"({{"served":{},"dispatches":{}}})", jsonString(served), dispatches));
    }
}

} // namespace gr::od

#endif // OPENDIGITIZER_UTILS_WEBGPURUNTIME_HPP
