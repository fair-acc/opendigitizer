#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <boost/ut.hpp>
#include <fmt/format.h>

#include <GnuRadioWorker.hpp>

#include "CountSource.hpp"

template<typename T>
struct ForeverSource : public gr::Block<ForeverSource<T>> {
    gr::PortOut<T> out;

    gr::work::Status
    processBulk(gr::PublishableSpan auto &output) noexcept {
        output.publish(output.size());
        return gr::work::Status::OK;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE(ForeverSource, out)

template<typename Registry>
void                   registerTestBlocks(Registry *registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    GP_REGISTER_NODE_RUNTIME(registry, CountSource, double);
    GP_REGISTER_NODE_RUNTIME(registry, ForeverSource, double);
    GP_REGISTER_NODE_RUNTIME(registry, gr::basic::DataSink, double);
#pragma GCC diagnostic pop
}

using namespace opencmw;
using namespace opendigitizer::acq;
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace boost::ut;
using namespace boost::ut::literals;
using namespace boost::ut::operators::terse;

namespace {
std::vector<float> getIota(std::size_t n, float first = 0.f) {
    std::vector<float> v(n);
    std::iota(v.begin(), v.end(), first);
    return v;
}

client::ClientContext makeClient(zmq::Context &ctx) {
    std::vector<std::unique_ptr<client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<client::MDClientCtx>(ctx, 20ms, ""));
    return client::ClientContext{ std::move(clients) };
}

void waitWhile(auto condition) {
    int tries = 10;
    while (tries > 0) {
        if (!condition()) return;
        tries--;
        std::this_thread::sleep_for(100ms);
    }
    expect(false);
}

} // namespace

struct TestSetup {
    using AcqWorker                    = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data acquisition updates">>;
    using FgWorker                     = GnuRadioFlowGraphWorker<AcqWorker, "/GnuRadio/FlowGraph", description<"Provides access to flow graph">>;
    gr::BlockRegistry     registry     = [] { gr::BlockRegistry r; registerTestBlocks(&r); return r; }();
    gr::plugin_loader     pluginLoader = gr::plugin_loader(&registry, {});
    majordomo::Broker<>   broker       = majordomo::Broker<>("/PrimaryBroker");
    AcqWorker             acqWorker    = AcqWorker(broker, 50ms);
    FgWorker              fgWorker     = FgWorker(broker, &pluginLoader, {}, acqWorker);
    std::jthread          brokerThread;
    std::jthread          acqWorkerThread;
    std::jthread          fgWorkerThread;
    zmq::Context          ctx;
    client::ClientContext client = makeClient(ctx);

    TestSetup() {
        const auto brokerPubAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
        expect((brokerPubAddress.has_value() == "bound successful"_b));
        const auto brokerRouterAddress = broker.bind(URI<>("mdp://127.0.0.1:12346"));
        expect((brokerRouterAddress.has_value() == "bound successful"_b));
        brokerThread    = std::jthread([this] { broker.run(); });
        acqWorkerThread = std::jthread([this] { acqWorker.run(); });
        fgWorkerThread  = std::jthread([this] { fgWorker.run(); });
        // let's give everyone some time to spin up and sort themselves
        std::this_thread::sleep_for(100ms);
    }

    void subscribeClient(const URI<> &uri, std::function<void(const Acquisition &)> &&handlerFnc) {
        client.subscribe(uri, [handler = std::move(handlerFnc)](const mdp::Message &update) {
            fmt::println("Client 'received message from service '{}' for topic '{}'", update.serviceName, update.topic.str());
            Acquisition acq;
            IoBuffer    buffer(update.data);
            try {
                const auto result = deserialise<YaS, ProtocolCheck::ALWAYS>(buffer, acq);
                if (!result.exceptions.empty()) {
                    throw result.exceptions.front();
                }
                handler(acq);
            } catch (const ProtocolException &e) {
                fmt::println(std::cerr, "Parsing failed: {}", e.what());
                throw;
            }
        });
    }

    void setGrc(std::string_view grc, auto callback) {
        opendigitizer::flowgraph::Flowgraph fg{ std::string(grc), {} };
        IoBuffer                            buffer;
        serialise<Json>(buffer, fg);
        client.set(URI("mdp://127.0.0.1:12346/GnuRadio/FlowGraph"), std::move(callback), std::move(buffer));
    }

    void setGrc(std::string_view grc) {
        std::atomic<bool> receivedReply = false;
        setGrc(grc, [&](const auto &reply) { expect(eq(reply.error, std::string{})); expect(!reply.data.empty()); receivedReply = true; });
        waitWhile([&receivedReply] { return !receivedReply.load(); });
    }

    ~TestSetup() {
        client.stop();
        broker.shutdown();
        brokerThread.join();
        acqWorkerThread.join();
        fgWorkerThread.join();
    }
};

const boost::ut::suite GnuRadioWorker_tests = [] {
    "Streaming"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count_up
    id: CountSource
    parameters:
      n_samples: 100
  - name: count_down
    id: CountSource
    parameters:
      n_samples: 100
      direction: down
  - name: test_sink_up
    id: gr::basic::DataSink
    parameters:
      signal_name: count_up
  - name: test_sink_down
    id: gr::basic::DataSink
    parameters:
      signal_name: count_down
connections:
  - [count_up, 0, test_sink_up, 0]
  - [count_down, 0, test_sink_down, 0]
)";
        TestSetup                  test;

        constexpr std::size_t      kExpectedSamples = 100;
        const std::vector<float>   expectedUpData   = getIota(kExpectedSamples);
        auto                       expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_up"), [&receivedUpData, &receivedUpCount](const auto &acq) {
            receivedUpData.insert(receivedUpData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_down"), [&receivedDownData, &receivedDownCount](const auto &acq) {
            receivedDownData.insert(receivedDownData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedDownCount = receivedDownData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedUpCount < kExpectedSamples || receivedDownCount < kExpectedSamples; });

        expect(eq(receivedUpData.size(), kExpectedSamples));
        expect(eq(receivedUpData, expectedUpData));
        expect(eq(receivedDownData.size(), kExpectedSamples));
        expect(eq(receivedDownData, expectedDownData));
    };

    "Flow graph management"_test = [] {
        constexpr std::string_view grc1 = R"(
blocks:
  - name: count_up
    id: CountSource
    parameters:
      n_samples: 100
  - name: test_sink_up
    id: gr::basic::DataSink
    parameters:
      signal_name: count_up
connections:
  - [count_up, 0, test_sink_up, 0]
)";
        constexpr std::string_view grc2 = R"(
blocks:
  - name: count_down
    id: CountSource
    parameters:
      n_samples: 100
      direction: down
  - name: test_sink_down
    id: gr::basic::DataSink
    parameters:
      signal_name: count_down
connections:
  - [count_down, 0, test_sink_down, 0]
)";

        TestSetup                  test;

        constexpr std::size_t      kExpectedSamples = 100;
        const std::vector<float>   expectedUpData   = getIota(kExpectedSamples);
        auto                       expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_up"), [&receivedUpData, &receivedUpCount](const auto &acq) {
            receivedUpData.insert(receivedUpData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_down"), [&receivedDownData, &receivedDownCount](const auto &acq) {
            receivedDownData.insert(receivedDownData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedDownCount = receivedDownData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);
        std::this_thread::sleep_for(500ms);
        test.setGrc(grc2);

        waitWhile([&] { return receivedUpCount < kExpectedSamples || receivedDownCount < kExpectedSamples; });

        expect(eq(receivedUpData.size(), kExpectedSamples));
        expect(eq(receivedUpData, expectedUpData));
        expect(eq(receivedDownData.size(), kExpectedSamples));
        expect(eq(receivedDownData, expectedDownData));
    };

#if 0  // TODO this only works with a multithreaded scheduler (see comment in GnuRadioWorker.hpp)
    "Flow graph management non-terminating graphs"_test = [] {
        constexpr std::string_view grc1 = R"(
blocks:
  - name: source
    id: ForeverSource
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: test1
connections:
  - [source, 0, test_sink, 0]
)";
        constexpr std::string_view grc2 = R"(
blocks:
  - name: source
    id: ForeverSource
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: test2
connections:
  - [source, 0, test_sink, 0]
)";

        TestSetup                  test;

        std::atomic<std::size_t>   receivedCount1 = 0;
        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=test1"), [&receivedCount1](const auto &acq) {
            receivedCount1 += acq.channelValue.size();
        });

        std::atomic<std::size_t> receivedCount2 = 0;
        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=test2"), [&receivedCount2](const auto &acq) {
            receivedCount2 += acq.channelValue.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);

        std::this_thread::sleep_for(900ms);
        test.setGrc(grc2);

        constexpr auto kExpectedSamples = 100000UZ;
        waitWhile([&] { return receivedCount1 < kExpectedSamples || receivedCount2 < kExpectedSamples; });
    };
#endif // disabled test

    "Trigger - tightly packed tags"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 100
      timing_tags:
        - 40,notatrigger
        - 50,hello
        - 60,ignoreme
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>         receivedData;
        std::atomic<std::size_t>   receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15"), [&receivedData, &receivedCount](const auto &acq) {
            expect(acq.acqTriggerName.value() == "hello");
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount < 20; });

        expect(eq(receivedData, getIota(20, 45)));
    };

    "Trigger - sparse tags"_test = [] {
        // Tests that tags detection and offsets work when the tag data is spread among multiple threads
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 10000000
      timing_tags:
        - 1000,notatrigger
        - 800000,hello
        - 900000,ignoreme
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>         receivedData;
        std::atomic<std::size_t>   receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15"), [&receivedData, &receivedCount](const auto &acq) {
            expect(acq.acqTriggerName.value() == "hello");
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount < 20; });

        expect(eq(receivedData, getIota(20, 799995)));
    };

    "Multiplexed"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 100
      sample_rate: 10
      timing_tags:
        - 30,hello
        - 50,start
        - 70,hello
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>         receivedData;
        std::atomic<std::size_t>   receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=multiplexed&triggerNameFilter=start"), [&receivedData, &receivedCount](const auto &acq) {
            expect(acq.acqTriggerName.value() == "start");
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount < 20; });

        expect(eq(receivedData, getIota(20, 50)));
    };

    "Snapshot"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 100
      sample_rate: 10
      timing_tags:
        - 40,hello
        - 50,shoot
        - 60,world
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>         receivedData;
        std::atomic<std::size_t>   receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=snapshot&triggerNameFilter=shoot&snapshotDelay=3000000000"), [&receivedData, &receivedCount](const auto &acq) {
            expect(acq.acqTriggerName.value() == "shoot");
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount == 0; });

        // trigger + delay * sample_rate = 50 + 3 * 10 = 80
        expect(eq(receivedData, std::vector{ 80.f }));
    };

    "Flow graph handling - Unknown block"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: unknown
    id: UnknownBlock
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [unknown, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::atomic<bool>          receivedReply = false;
        test.setGrc(grc, [&receivedReply](const auto &reply) {
            expect(eq(reply.data.asString(), std::string{}));
            expect(neq(reply.error, std::string{}));
            receivedReply = true;
        });

        waitWhile([&] { return !receivedReply; });
        expect(receivedReply.load());
    };
};

int main() { /* not needed for ut */
}
