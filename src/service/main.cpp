#include <fstream>
#include <thread>

#include <majordomo/Broker.hpp>

#include "flowgraph/flowgraphWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

int main() {
    using namespace opencmw::majordomo;
    using opencmw::URI;
    auto           fs = cmrc::assets_opencmw::get_filesystem();
    using RestBackend = FileServerRestBackend<PLAIN_HTTP, decltype(fs)>;

    // broker
    Broker      broker("PrimaryBroker");
    const auto  brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }
    std::jthread brokerThread([&broker] {
        broker.run();
    });

    // REST backend
    RestBackend rest(broker, fs, "./");

    // flowgraph worker (mock)
    //FlowgraphWorker<"flowgraph", description<"Provides R/W access to the flowgraph as a yaml serialized string">> flowgraphWorker(broker);
    //std::jthread flowgraphWorkerThread([&flowgraphWorker] {
    //    flowgraphWorker.run();
    //});

    // acquisition worker (mock) todo: implement
    // AcquisitionWorker<"acquisition", description<"Provides data acquisition updates">> acquisitionWorker(broker);
    // std::jthread acquisitionWorkerThread([&acquisitionWorker] {
    //     acquisitionWorker.run();
    // });

    // shutdown
    brokerThread.join();
    // workers terminate when broker shuts down
    //flowgraphWorkerThread.join();
    //acquisitionWorkerThread.join();
}