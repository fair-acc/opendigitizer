#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <fstream>
#include <thread>

#include "dashboard/dashboardWorker.hpp"
#include "gnuradio/GnuRadioWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

#include <gnuradio-4.0/basic/common_blocks.hpp>
#include <gnuradio-4.0/basic/function_generator.h>
#include <gnuradio-4.0/basic/selector.hpp>

// TODO instead of including and registering blocks manually here, rely on the plugin system
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
#ifndef __EMSCRIPTEN__
    GP_REGISTER_NODE_RUNTIME(registry, fair::picoscope::Picoscope4000a, double, float, int16_t);
#endif
#pragma GCC diagnostic pop
}
} // namespace

using namespace opencmw::majordomo;

int main(int argc, char **argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opencmw::majordomo;
    using namespace std::chrono_literals;

    std::string grc;
    if (argc > 1) {
        std::ifstream     in(argv[1]);
        std::stringstream grcBuffer;
        if (!(grcBuffer << in.rdbuf())) {
            fmt::println(std::cerr, "Could not read GRC file: {}", strerror(errno));
            return 1;
        }
        grc = grcBuffer.str();
    }

    // broker
    Broker broker("PrimaryBroker");
    // REST backend
    // auto fs           = cmrc::assets_opencmw::get_filesystem();
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<PLAIN_HTTP, decltype(fs)>;
    RestBackend rest(broker, fs, "./");

    const auto  requestedAddress    = URI<>("mds://127.0.0.1:12345");
    const auto  brokerRouterAddress = broker.bind(requestedAddress);
    if (!brokerRouterAddress) {
        fmt::println(std::cerr, "Could bind to broker address {}", requestedAddress.str());
        return 1;
    }
    std::jthread brokerThread([&broker] {
        broker.run();
    });

    std::jthread restThread([&rest] { rest.run(); });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    using GrAcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using GrFgWorker  = GnuRadioFlowGraphWorker<GrAcqWorker, "flowgraph", description<"Provides access to the GnuRadio flow graph">>;
    gr::BlockRegistry registry;
    registerTestBlocks(&registry);
    gr::plugin_loader pluginLoader(&registry, {});
    GrAcqWorker       grAcqWorker(broker, std::chrono::milliseconds(50));
    GrFgWorker        grFgWorker(broker, &pluginLoader, { grc, {} }, grAcqWorker);

    std::jthread      grAcqWorkerThread([&grAcqWorker] { grAcqWorker.run(); });
    std::jthread      grFgWorkerThread([&grFgWorker] { grFgWorker.run(); });

    std::this_thread::sleep_for(100ms);

    // start some simple subscription client
    fmt::print("starting some client subscriptions\n");
    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?channelNameFilter=saw"), [](const opencmw::mdp::Message &update) {
        fmt::print("Client('saw') received message from service '{}' for endpoint '{}'\n{}\n", update.serviceName, update.endpoint.str(), update.data.asString());
    });
    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?contentType=application%2Fjson"), [](const opencmw::mdp::Message &update) {
        fmt::print("Client('all') received message from service '{}' for endpoint '{}'\n{}\n", update.serviceName, update.endpoint.str(), update.data.asString());
    });
    // TODO this test makes only sense if the signal in the grc is called "test"
    client.subscribe(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?contentType=application%2Fjson&channelNameFilter=test"), [](const opencmw::mdp::Message &update) {
        fmt::print("Client('all') received message from service '{}' for endpoint '{}'\n{}\n", update.serviceName, update.endpoint.str(), update.data.asString());
    });

    brokerThread.join();
    restThread.join();

    client.stop();

    dashboardWorkerThread.join();
    grAcqWorkerThread.join();
    grFgWorkerThread.join();
}
