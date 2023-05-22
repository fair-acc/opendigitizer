#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <fmt/format.h>
#define __cpp_lib_source_location
#include <boost/ut.hpp>

#include <acqWorker.hpp>

using opencmw::majordomo::Broker;
using opencmw::majordomo::BrokerMessage;
using opencmw::mdp::Command;
using opencmw::mdp::Message;
using opencmw::majordomo::Settings;
using opencmw::majordomo::Worker;
using opencmw::zmq::Context;

const boost::ut::suite basic_acq_worker_tests = [] {
    using namespace boost::ut;

    "GetDataTest"_test = [] {
        using opencmw::URI;
        using namespace opendigitizer::acq;
        using namespace opencmw::majordomo;
        using namespace std::chrono_literals;
        using namespace boost::ut;
        using namespace boost::ut::literals;
        using namespace boost::ut::operators::terse;

        // broker
        Broker     broker("PrimaryBroker");

        const auto brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
        expect((brokerRouterAddress.has_value() == "bound successful"_b));
        std::jthread brokerThread([&broker] {
            broker.run();
        });

        // acquisition worker (mock)
        using AcqWorker = AcquisitionWorker<"/DeviceName/Acquisition", description<"Provides data acquisition updates">>;
        AcqWorker    acquisitionWorker(broker, 1000ms);
        std::jthread acquisitionWorkerThread([&acquisitionWorker] { acquisitionWorker.run(); });

        std::this_thread::sleep_for(100ms);

        // start some simple subscription client
        fmt::print("starting some client subscriptions\n");
        const Context                                             zctx{};
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
        opencmw::client::ClientContext client{ std::move(clients) };

        std::this_thread::sleep_for(100ms);

        std::atomic<int> receivedA{ 0 };
        std::atomic<int> receivedAB{ 0 };
        client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?channelNameFilter=saw"), [&receivedA](const opencmw::mdp::Message &update) {
            fmt::print("Client('A') received message from service '{}' for endpoint '{}'\n", update.serviceName, update.endpoint.str());
            receivedA++;
        });
        client.subscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition"), [&receivedAB](const opencmw::mdp::Message &update) {
            fmt::print("Client('A,B') received message from service '{}' for endpoint '{}'\n", update.serviceName, update.endpoint.str());
            receivedAB++;
        });

        while (receivedA < 2 && receivedAB < 2) {
            std::this_thread::sleep_for(200ms);
        }

        client.unsubscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition?channelNameFilter=saw"));
        client.unsubscribe(URI("mds://127.0.0.1:12345/DeviceName/Acquisition"));
        fmt::print("received client updates: {} for 'A' and {} for 'A,B'\n", receivedA, receivedAB);
        expect(int(receivedA) >= 2_i);
        expect(int(receivedAB) >= 2_i);
        client.stop();

        broker.shutdown();

        brokerThread.join();
        acquisitionWorkerThread.join();
    };
};

int main() { /* not needed for ut */
}
