#ifndef OPENDIGITIZER_UI_COMPONENTS_COMPUTEDOMAINTAG_HPP
#define OPENDIGITIZER_UI_COMPONENTS_COMPUTEDOMAINTAG_HPP

#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <gnuradio-4.0/ComputeDomain.hpp>
#include <gnuradio-4.0/device/DeviceContextRegistry.hpp>

namespace gr::od::components {

/// what a block's compute_domain setting resolves to: the label the flowgraph editor's tooltip shows, and whether the block runs on the host instead of the device it asked for.
/// GR4's scheduler makes the same decision when the graph starts (resolveDeclaredDomain);
/// the UI loads a dashboard only after the WebGPU runtime has settled, so both agree.
struct ComputeDomainTag {
    std::string label;            // the served domain, or "<domain> -> CPU" / "<domain> -> stop"
    std::string reason;           // why it is not served, empty when it is
    bool        fellBack = false; // not served: a block runs on the CPU, or stops when its domain is required ('!')
};

/// nullopt for a host domain (or none), which needs no tag
[[nodiscard]] inline std::optional<ComputeDomainTag> computeDomainTag(std::string_view declared) {
    if (!gr::ComputeDomain::parse(declared).isDevice()) {
        return std::nullopt;
    }
    const gr::device::DomainOutcome outcome = gr::device::resolveDeclaredDomain(declared);
    if (outcome.context != nullptr) {
        return ComputeDomainTag{.label = outcome.declared, .reason = {}, .fellBack = false};
    }
    return ComputeDomainTag{.label = std::format("{} -> {}", outcome.declared, outcome.refused ? "stop" : "CPU"), .reason = outcome.reason, .fellBack = true};
}

/// `tag` with what the running block reported (GR4's ComputeDomainFallback):
/// a kernel refused at dispatch (not translated, f64, too large) runs on the host although its domain is served;
/// the report's reason replaces the prediction's
[[nodiscard]] inline ComputeDomainTag withRuntimeFallback(ComputeDomainTag tag, std::string_view reportedReason) {
    if (reportedReason.empty()) {
        return tag;
    }
    if (!tag.fellBack) {
        tag.label    = std::format("{} -> CPU", tag.label);
        tag.fellBack = true;
    }
    tag.reason = std::string(reportedReason);
    return tag;
}

/// what `declared` resolves to, with what the running block reported; nullopt on the host
[[nodiscard]] inline std::optional<ComputeDomainTag> blockComputeDomain(std::string_view declared, std::string_view reportedReason) {
    auto tag = computeDomainTag(declared);
    return tag ? std::optional(withRuntimeFallback(std::move(*tag), reportedReason)) : std::nullopt;
}

/// the flowgraph glyph's tooltip: where the block runs, or that its `device` is not served and why
[[nodiscard]] inline std::string computeDomainTooltip(std::string_view device, const ComputeDomainTag& tag) {
    if (!tag.fellBack) {
        return std::format("runs on the {} ({})", device, tag.label);
    }
    return tag.reason.empty() ? std::format("{} not served: {}", device, tag.label) : std::format("{} not served: {}\n{}", device, tag.label, tag.reason);
}

} // namespace gr::od::components

#endif // OPENDIGITIZER_UI_COMPONENTS_COMPUTEDOMAINTAG_HPP
