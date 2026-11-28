#include "../components/ComputeDomainTag.hpp" // first: the header must compile standalone

#include <boost/ut.hpp>

#include <gnuradio-4.0/device/DeviceContext.hpp>

#include <memory>

using namespace boost::ut;
using gr::od::components::computeDomainTag;

const suite<"compute_domain tag in the flowgraph editor"> computeDomainTagTests = [] {
    "a host domain, or none, has no tag"_test = [] {
        expect(!computeDomainTag("").has_value());
        expect(!computeDomainTag("host").has_value());
        expect(!computeDomainTag("default_cpu").has_value());
    };

    "a device domain nobody serves falls back to the CPU, with GR4's reason"_test = [] {
        const auto tag = computeDomainTag("gpu:qa_unserved");
        expect(tag.has_value() >> fatal);
        expect(tag->fellBack);
        expect(eq(tag->label, std::string{"gpu:qa_unserved -> CPU"}));
        expect(!tag->reason.empty()) << "the reason GR4 gives when the graph starts";
    };

    "a required device domain nobody serves stops instead"_test = [] {
        const auto tag = computeDomainTag("gpu:qa_unserved!");
        expect(tag.has_value() >> fatal);
        expect(tag->fellBack);
        expect(tag->label.ends_with("-> stop")) << tag->label;
    };

    "outside a WebGPU build a bare 'gpu' names SYCL, which the UI never registers"_test = [] {
        if constexpr (!gr::device::kHasWebGpu) {
            const auto tag = computeDomainTag("gpu");
            expect(tag.has_value() >> fatal);
            expect(tag->fellBack);
            expect(eq(tag->label, std::string{"gpu:sycl -> CPU"}));
        }
    };

    "a served device domain shows its name, no fallback"_test = [] {
        expect(gr::device::DeviceContextRegistry::instance().registerContext("gpu:qa_served", std::make_unique<gr::device::DeviceContextCpu>()) >> fatal);
        const auto tag = computeDomainTag("gpu:qa_served");
        expect(tag.has_value() >> fatal);
        expect(!tag->fellBack);
        expect(tag->reason.empty());
        expect(tag->label.starts_with("gpu:qa_served")) << tag->label;
    };
};

const suite<"a running block's reported fallback"> runtimeFallbackTests = [] {
    using gr::od::components::ComputeDomainTag;
    using gr::od::components::withRuntimeFallback;

    "a served domain whose kernel was refused at dispatch shows -> CPU with the reported reason"_test = [] {
        const auto tag = withRuntimeFallback(ComputeDomainTag{.label = "gpu:webgpu", .reason = {}, .fellBack = false}, "kernel not translated: f64");
        expect(tag.fellBack);
        expect(eq(tag.label, std::string{"gpu:webgpu -> CPU"}));
        expect(eq(tag.reason, std::string{"kernel not translated: f64"}));
    };

    "an already predicted fallback keeps its label and takes the reported reason"_test = [] {
        const auto tag = withRuntimeFallback(ComputeDomainTag{.label = "gpu:sycl -> CPU", .reason = "not served", .fellBack = true}, "no device path");
        expect(eq(tag.label, std::string{"gpu:sycl -> CPU"}));
        expect(eq(tag.reason, std::string{"no device path"}));
    };

    "no report leaves the tag as predicted"_test = [] {
        const auto tag = withRuntimeFallback(ComputeDomainTag{.label = "gpu:webgpu", .reason = {}, .fellBack = false}, "");
        expect(!tag.fellBack);
        expect(eq(tag.label, std::string{"gpu:webgpu"}));
    };
};

const suite<"a block's domain as the flowgraph editor shows it"> blockDomainTests = [] {
    using gr::od::components::blockComputeDomain;

    "a block on the host has none"_test = [] {
        expect(!blockComputeDomain("", "").has_value());
        expect(!blockComputeDomain("host", "").has_value());
    };

    "a block on a served domain runs there until it reports a fallback"_test = [] {
        expect(gr::device::DeviceContextRegistry::instance().registerContext("gpu:qa_block", std::make_unique<gr::device::DeviceContextCpu>()) >> fatal);
        const auto served = blockComputeDomain("gpu:qa_block", "");
        expect(served.has_value() >> fatal);
        expect(!served->fellBack);
        const auto refused = blockComputeDomain("gpu:qa_block", "kernel refused at dispatch: f64");
        expect(refused.has_value() >> fatal);
        expect(refused->fellBack);
        expect(eq(refused->reason, std::string{"kernel refused at dispatch: f64"}));
    };

    "a block on an unserved domain falls back"_test = [] {
        const auto tag = blockComputeDomain("gpu:qa_unserved", "");
        expect(tag.has_value() >> fatal);
        expect(tag->fellBack);
    };
};

const suite<"the flowgraph glyph's tooltip"> tooltipTests = [] {
    using gr::od::components::ComputeDomainTag;
    using gr::od::components::computeDomainTooltip;

    "a served domain says where the block runs"_test = [] { expect(eq(computeDomainTooltip("GPU", ComputeDomainTag{.label = "gpu:webgpu", .reason = {}, .fellBack = false}), std::string{"runs on the GPU (gpu:webgpu)"})); };

    "a fallback names the unserved device, where the block runs instead, and why"_test = [] {
        const ComputeDomainTag tag{.label = "gpu:qa_unserved -> CPU", .reason = "no context registered", .fellBack = true};
        expect(eq(computeDomainTooltip("GPU", tag), std::string{"GPU not served: gpu:qa_unserved -> CPU\nno context registered"}));
    };

    "a required domain that stops the block says so"_test = [] {
        const ComputeDomainTag tag{.label = "tpu:x! -> stop", .reason = {}, .fellBack = true};
        expect(eq(computeDomainTooltip("TPU", tag), std::string{"TPU not served: tpu:x! -> stop"}));
    };
};

// suites run here, not at exit, where the static state they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
