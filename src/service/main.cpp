#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <fstream>
#include <thread>

#include "acquisition/acqWorker.hpp"
#include "dashboard/dashboardWorker.hpp"
#include "flowgraph/flowgraphWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

using namespace opencmw::majordomo;

int main() {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opencmw::majordomo;
    using namespace std::chrono_literals;

    // broker
    Broker broker("PrimaryBroker");
    // REST backend
    // auto fs           = cmrc::assets_opencmw::get_filesystem();
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<PLAIN_HTTP, decltype(fs)>;
    RestBackend rest(broker, fs, "./");

    const auto  brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }
    std::jthread brokerThread([&broker] {
        broker.run();
    });

    std::jthread restThread([&rest] { rest.run(); });

    // flowgraph worker (mock)
    using FgWorker = FlowgraphWorker<"flowgraph", description<"Provides R/W access to the flowgraph as a yaml serialized string">>;
    FgWorker     flowgraphWorker(broker);
    std::jthread flowgraphWorkerThread([&flowgraphWorker] { flowgraphWorker.run(); });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    // acquisition worker (mock)
    using AcqWorker = AcquisitionWorker<"/DeviceName/Acquisition", description<"Provides data acquisition updates">>;
    AcqWorker    acquisitionWorker(broker, 3000ms);
    std::jthread acquisitionWorkerThread([&acquisitionWorker] { acquisitionWorker.run(); });

    std::this_thread::sleep_for(100ms);

    // start some simple subscription client
    fmt::print("starting some client subscriptions\n");
    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    opencmw::client::ClientContext client{ std::move(clients) };

    std::this_thread::sleep_for(100ms);

    std::atomic<int> receivedA{ 0 };
    std::atomic<int> receivedAB{ 0 };
    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?channelNameFilter=saw"), [&receivedA](const opencmw::mdp::Message &update) {
        fmt::print("Client('saw') received message from service '{}' for endpoint '{}'\n{}\n", update.serviceName, update.endpoint.str(), update.data.asString());
        receivedA++;
    });
    client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition"), [&receivedAB](const opencmw::mdp::Message &update) {
        fmt::print("Client('all') received message from service '{}' for endpoint '{}'\n{}\n", update.serviceName, update.endpoint.str(), update.data.asString());
        receivedAB++;
    });

    while (receivedA < 2 || receivedAB < 4) {
        std::this_thread::sleep_for(200ms);
    }

    client.unsubscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?channelNameFilter=saw"));
    client.unsubscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition"));
    fmt::print("received client updates: {} for 'sine' and {} for 'sine,saw'\n", receivedA.load(), receivedAB.load());
    client.stop();

    // shutdown
    brokerThread.join();
    restThread.join();
    // workers terminate when broker shuts down
    flowgraphWorkerThread.join();
    acquisitionWorkerThread.join();
}
