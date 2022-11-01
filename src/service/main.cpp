#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>

#include <fstream>
#include <iomanip>
#include <thread>

#include "acquisition/acqWorker.hpp"
#include "flowgraph/flowgraphWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

using namespace opencmw::majordomo;

int main() {
    using opencmw::URI;
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

    // flowgraph worker (mock)
    FlowgraphWorker<"flowgraph", description<"Provides R/W access to the flowgraph as a yaml serialized string">> flowgraphWorker(broker);
    std::jthread                                                                                                  flowgraphWorkerThread([&flowgraphWorker] {
        flowgraphWorker.run();
                                                                                                     });

    // acquisition worker (mock) todo: implement
    AcquisitionWorker<"acquisition", description<"Provides data acquisition updates">> acquisitionWorker(broker, 2000ms); // todo: change to 25Hz, just slow for debugging
    std::jthread                                                                       acquisitionWorkerThread([&acquisitionWorker] {
        acquisitionWorker.run();
                                                                          });

    // shutdown
    brokerThread.join();
    // workers terminate when broker shuts down
    flowgraphWorkerThread.join();
    acquisitionWorkerThread.join();
}