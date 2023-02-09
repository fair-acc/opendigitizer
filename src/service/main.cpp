#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>

#include <fstream>
#include <iomanip>
#include <thread>

#include "acquisition/acqWorker.hpp"
#include "acquisition/mock_source.hpp"
#include "flowgraph/flowgraphWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

using namespace opencmw::majordomo;

int main() {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opencmw::majordomo;
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
    using FgWorker = FlowgraphWorker<"flowgraph", description<"Provides R/W access to the flowgraph as a yaml serialized string">>;
    FgWorker     flowgraphWorker(broker);
    std::jthread flowgraphWorkerThread([&flowgraphWorker] { flowgraphWorker.run(); });

    // acquisition worker (mock) todo: implement
    using AcqWorker = AcquisitionWorker<"acquisition", description<"Provides data acquisition updates">>;
    std::vector<AcqWorker::sink_buffer> sinks = {{"sample-sine", "V", AcqWorker::streambuffer{AcqWorker::RING_BUFFER_SIZE}, AcqWorker::tagbuffer{AcqWorker::RING_BUFFER_SIZE}}};
    AcqWorker    acquisitionWorker(broker, sinks); // todo: change to 25Hz, just slow for debugging
    std::jthread acquisitionWorkerThread([&acquisitionWorker] { acquisitionWorker.run(); });

    // mock publisher, which publishes sine waves to the available sinks // todo: put into separate class
    auto src = opendigitizer::acq::mock_source<AcqWorker>{sinks};
    std::jthread source{[&src](std::stop_token stoken) {return src(stoken);}};

    // shutdown
    brokerThread.join();
    source.join();
    // workers terminate when broker shuts down
    flowgraphWorkerThread.join();
    acquisitionWorkerThread.join();
}