#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <fmt/format.h>

#include <GnuRadioWorker.hpp>

#include <gnuradio-4.0/basic/common_blocks.hpp>
#include <gnuradio-4.0/basic/function_generator.h>
#include <gnuradio-4.0/basic/selector.hpp>

#include "CountSource.hpp"

#ifndef __EMSCRIPTEN__
#include <Picoscope4000a.hpp>
#endif

namespace {
template<typename Registry>
void registerTestBlocks(Registry *registry) {
    registerBuiltinBlocks(registry);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    GP_REGISTER_NODE_RUNTIME(registry, gr::basic::DataSink, double, float, int16_t);
    GP_REGISTER_NODE_RUNTIME(registry, CountSource, double, float, int16_t);
#ifndef __EMSCRIPTEN__
    GP_REGISTER_NODE_RUNTIME(registry, fair::picoscope::Picoscope4000a, double, float, int16_t);
#endif
#pragma GCC diagnostic pop
}
} // namespace

static void printUsage() {
    std::cerr << "Usage: GnuRadioWorkerDemo [--extern-broker] <brokerRouterAddress> <grcFile>" << std::endl;
}

int main(int argc, char **argv) {
    using namespace opencmw;
    using namespace opendigitizer::acq;
    using namespace std::chrono_literals;
    if (argc < 3) {
        printUsage();
        return 1;
    }

    auto nextArg      = 1UZ;
    auto externBroker = false;
    if (std::string_view(argv[nextArg]) == "--extern-broker") {
        externBroker = true;
        nextArg++;
    }

    if (argc - nextArg != 2) {
        printUsage();
        return 1;
    }

    const auto brokerAddress = URI<>(std::string(argv[nextArg]));
    nextArg++;

    std::ifstream     in(argv[nextArg]);

    std::stringstream grcBuffer;
    if (!(grcBuffer << in.rdbuf())) {
        fmt::println(std::cerr, "Could not read GRC file: {}", strerror(errno));
        return 1;
    }

    gr::BlockRegistry registry;
    registerTestBlocks(&registry);
    gr::plugin_loader pluginLoader(&registry, {});

    using AcqWorker     = GnuRadioAcquisitionWorker<"/Hello/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using FgWorker      = GnuRadioFlowGraphWorker<AcqWorker, "/Hello/GnuRadio/FlowGraph", description<"Provides access to the GnuRadio flow graph">>;

    constexpr auto rate = 10ms; // could be made configurable/(0 -> disable sleep completely)

    if (externBroker) {
        zmq::Context ctx;
        AcqWorker    acqWorker(brokerAddress, ctx, rate);
        FgWorker     fgWorker(brokerAddress, ctx, &pluginLoader, { grcBuffer.str(), {} }, acqWorker);
        std::jthread acqWorkerThread([&acqWorker] { acqWorker.run(); });
        std::jthread fgWorkerThread([&fgWorker] { fgWorker.run(); });
        acqWorkerThread.join();
        fgWorkerThread.join();
        return 0;
    }

    majordomo::Broker broker("PrimaryBroker");
    const auto        boundAddress = broker.bind(brokerAddress);
    if (!boundAddress) {
        fmt::println(std::cerr, "Could not bind broker to address '{}'", argv[1]);
        return 1;
    }
    fmt::println("Broker listens to {}", boundAddress->str());

    AcqWorker    acqWorker(broker, rate);
    FgWorker     fgWorker(broker, &pluginLoader, { grcBuffer.str(), {} }, acqWorker);
    std::jthread brokerThread([&broker] { broker.run(); });
    std::jthread acqWorkerThread([&acqWorker] { acqWorker.run(); });
    std::jthread fgWorkerThread([&fgWorker] { fgWorker.run(); });
    brokerThread.join();
    acqWorkerThread.join();
    fgWorkerThread.join();
}
