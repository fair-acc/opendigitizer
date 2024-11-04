#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/testing/Delay.hpp>

#include <boost/ut.hpp>
#include <fmt/format.h>
#include <magic_enum_format.hpp>

#include "GnuRadioAcquisitionWorker.hpp"
#include "GnuRadioFlowgraphWorker.hpp"

#include "CountSource.hpp"

template<typename T>
struct ForeverSource : public gr::Block<ForeverSource<T>> {
    gr::PortOut<T> out;

    GR_MAKE_REFLECTABLE(ForeverSource, out);

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        output.publish(output.size());
        return gr::work::Status::OK;
    }
};

template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<CountSource, double>(registry);
    gr::registerBlock<ForeverSource, double>(registry);
    gr::registerBlock<gr::basic::DataSetSink, double>(registry);
    gr::registerBlock<gr::basic::DataSink, double>(registry);
    gr::registerBlock<gr::blocks::fft::DefaultFFT, double>(registry);
    gr::registerBlock<gr::testing::Delay, double>(registry);
#pragma GCC diagnostic pop
}

namespace magic_enum::customize {
template<typename opendigitizer::gnuradio::SignalType>
constexpr bool enum_format_enabled() noexcept {
    return true;
}
} // namespace magic_enum::customize

namespace opendigitizer::gnuradio {
constexpr std::ostream& operator<<(std::ostream& os, const opendigitizer::gnuradio::SignalType& t) { return os << std::format("{}", t); }
} // namespace opendigitizer::gnuradio

using namespace opencmw;
using namespace opendigitizer::gnuradio;
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

client::ClientContext makeClient(zmq::Context& ctx) {
    std::vector<std::unique_ptr<client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<client::MDClientCtx>(ctx, 20ms, ""));
    return client::ClientContext{std::move(clients)};
}

void waitWhile(auto condition) {
    // Use very generous timeout to avoid flakiness when run under gcov
    // (on my system, creating 6 blocks from GRC already takes about 6 seconds)
    constexpr auto kTimeout       = 20s;
    constexpr auto kSleepInterval = 100ms;
    auto           elapsed        = 0ms;
    while (elapsed < kTimeout) {
        if (!condition()) {
            return;
        }
        std::this_thread::sleep_for(kSleepInterval);
        elapsed += kSleepInterval;
    }
    expect(false);
}

} // namespace

struct TestSetup {
    using AcqWorker            = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data acquisition updates">>;
    using FgWorker             = GnuRadioFlowGraphWorker<AcqWorker, "/GnuRadio/FlowGraph", description<"Provides access to flow graph">>;
    gr::BlockRegistry registry = [] {
        gr::BlockRegistry r;
        registerTestBlocks(r);
        return r;
    }();
    gr::PluginLoader      pluginLoader = gr::PluginLoader(registry, {});
    majordomo::Broker<>   broker       = majordomo::Broker<>("/PrimaryBroker");
    AcqWorker             acqWorker    = AcqWorker(broker, &pluginLoader, 50ms);
    FgWorker              fgWorker     = FgWorker(broker, &pluginLoader, {}, acqWorker);
    std::jthread          brokerThread;
    std::jthread          acqWorkerThread;
    std::jthread          fgWorkerThread;
    zmq::Context          ctx;
    client::ClientContext client = makeClient(ctx);

    explicit TestSetup(std::function<void(std::vector<SignalEntry>)> dnsCallback = {}) {
        const auto brokerPubAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
        expect((brokerPubAddress.has_value() == "bound successful"_b));
        const auto brokerRouterAddress = broker.bind(URI<>("mdp://127.0.0.1:12346"));
        expect((brokerRouterAddress.has_value() == "bound successful"_b));
        acqWorker.setUpdateSignalEntriesCallback(std::move(dnsCallback));

        brokerThread    = std::jthread([this] { broker.run(); });
        acqWorkerThread = std::jthread([this] { acqWorker.run(); });
        fgWorkerThread  = std::jthread([this] { fgWorker.run(); });
        // let's give everyone some time to spin up and sort themselves
        std::this_thread::sleep_for(100ms);
    }

    void subscribeClient(const URI<>& uri, std::function<void(const Acquisition&)>&& handlerFnc) {
        client.subscribe(uri, [handler = std::move(handlerFnc)](const mdp::Message& update) {
            fmt::println("Client 'received message from service '{}' for topic '{}'", update.serviceName, update.topic.str());
            Acquisition acq;
            IoBuffer    buffer(update.data);
            try {
                const auto result = deserialise<YaS, ProtocolCheck::ALWAYS>(buffer, acq);
                if (!result.exceptions.empty()) {
                    throw result.exceptions.front();
                }
                handler(acq);
            } catch (const ProtocolException& e) {
                fmt::println(std::cerr, "Parsing failed: {}", e.what());
                throw;
            }
        });
    }

    void setGrc(std::string_view grc, auto callback) {
        opendigitizer::flowgraph::Flowgraph fg{std::string(grc), {}};
        gr::Message                         message;
        message.endpoint = "ReplaceGraphGRC";
        opendigitizer::flowgraph::storeFlowgraphToMessage(fg, message);

        const auto serialised = serialiseMessage(message);
        IoBuffer   buffer(serialised.data(), serialised.size());

        fmt::print("Sending ReplaceGraphGRC message to the service\n");
        client.set(URI("mdp://127.0.0.1:12346/GnuRadio/FlowGraph"), std::move(callback), std::move(buffer));
    }

    void setGrc(std::string_view grc) {
        std::atomic<bool> receivedReply = false;
        setGrc(grc, [&](const auto& reply) {
            expect(eq(reply.error, std::string{}));
            expect(!reply.data.empty());
            receivedReply = true;
        });
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
      signal_unit: up unit
      signal_min: 0
      signal_max: 99
  - name: delay_up
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: count_down
    id: CountSource
    parameters:
      n_samples: 100
      initial_value: 99
      direction: down
      signal_unit: down unit
      signal_min: 0
      signal_max: 99
  - name: delay_down
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink_up
    id: gr::basic::DataSink
    parameters:
      signal_name: count_up
  - name: test_sink_down
    id: gr::basic::DataSink
    parameters:
      signal_name: count_down
connections:
  - [count_up, 0, delay_up, 0]
  - [delay_up, 0, test_sink_up, 0]
  - [count_down, 0, delay_down, 0]
  - [delay_down, 0, test_sink_down, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestSetup                  test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        constexpr std::size_t    kExpectedSamples = 100;
        const std::vector<float> expectedUpData   = getIota(kExpectedSamples);
        auto                     expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_up"), [&receivedUpData, &receivedUpCount](const auto& acq) {
            expect(acq.channelUnit.value() == "up unit"sv);
            expect(acq.channelRangeMin == 0.f);
            expect(acq.channelRangeMax == 99.f);
            receivedUpData.insert(receivedUpData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_down"), [&receivedDownData, &receivedDownCount](const auto& acq) {
            expect(acq.channelUnit.value() == "down unit"sv);
            expect(acq.channelRangeMin == 0.f);
            expect(acq.channelRangeMax == 99.f);
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
        expect(eq(lastDnsEntries.size(), 2UZ));
        std::ranges::sort(lastDnsEntries, {}, &SignalEntry::name);
        if (lastDnsEntries.size() >= 2UZ) {
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "count_down"sv));
            expect(eq(lastDnsEntries[0].unit, "down unit"sv));
            expect(eq(lastDnsEntries[1].type, SignalType::Plain));
            expect(eq(lastDnsEntries[1].name, "count_up"sv));
            expect(eq(lastDnsEntries[1].unit, "up unit"sv));
        }
    };

    "Flow graph management"_test = [] {
        constexpr std::string_view grc1 = R"(
blocks:
  - name: count_up
    id: CountSource
    parameters:
      n_samples: 100
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink_up
    id: gr::basic::DataSink
    parameters:
      signal_name: count_up
connections:
  - [count_up, 0, delay, 0]
  - [delay, 0, test_sink_up, 0]
)";
        constexpr std::string_view grc2 = R"(
blocks:
  - name: count_down
    id: CountSource
    parameters:
      n_samples: 100
      initial_value: 99
      direction: down
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink_down
    id: gr::basic::DataSink
    parameters:
      signal_name: count_down
connections:
  - [count_down, 0, delay, 0]
  - [delay, 0, test_sink_down, 0]
)";

        TestSetup test;

        constexpr std::size_t    kExpectedSamples = 100;
        const std::vector<float> expectedUpData   = getIota(kExpectedSamples);
        auto                     expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_up"), [&receivedUpData, &receivedUpCount](const auto& acq) {
            receivedUpData.insert(receivedUpData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_down"), [&receivedDownData, &receivedDownCount](const auto& acq) {
            receivedDownData.insert(receivedDownData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedDownCount = receivedDownData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);
        std::this_thread::sleep_for(1200ms);
        test.setGrc(grc2);

        waitWhile([&] { return receivedUpCount < kExpectedSamples || receivedDownCount < kExpectedSamples; });

        expect(eq(receivedUpData.size(), kExpectedSamples));
        expect(eq(receivedUpData, expectedUpData));
        expect(eq(receivedDownData.size(), kExpectedSamples));
        expect(eq(receivedDownData, expectedDownData));
    };

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

        std::mutex               dnsMutex;
        std::vector<SignalEntry> lastDnsEntries;
        TestSetup                test([&lastDnsEntries, &dnsMutex](auto entries) {
            if (!entries.empty()) {
                std::lock_guard lock(dnsMutex);
                lastDnsEntries = std::move(entries);
            }
        });

        std::atomic<std::size_t> receivedCount1 = 0;
        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=test1"), [&receivedCount1](const auto& acq) { receivedCount1 += acq.channelValue.size(); });

        std::atomic<std::size_t> receivedCount2 = 0;
        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=test2"), [&receivedCount2](const auto& acq) { receivedCount2 += acq.channelValue.size(); });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);
        std::this_thread::sleep_for(1200ms);

        {
            std::lock_guard lock(dnsMutex);
            expect(eq(lastDnsEntries.size(), 1UZ));
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "test1"sv));
        }
        test.setGrc(grc2);

        constexpr auto kExpectedSamples = 50000UZ;
        waitWhile([&] { return receivedCount1 < kExpectedSamples || receivedCount2 < kExpectedSamples; });

        std::lock_guard lock(dnsMutex);
        expect(eq(lastDnsEntries.size(), 1UZ));
        if (!lastDnsEntries.empty()) {
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "test2"sv));
        }
    };

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
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15"), [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.acqTriggerName.value(), "hello"sv));
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
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15"), [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.acqTriggerName.value(), "hello"sv));
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
      signal_unit: A unit
      sample_rate: 10
      timing_tags:
        - 30,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=1
        - 50,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2
        - 70,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=3
        - 80,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=4
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestSetup                  test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        // filter "[CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2, CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=3]" => samples [50..69]
        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=multiplexed&triggerNameFilter=%5BCMD_BP_START%2FFAIR.SELECTOR.C%3D1%3AS%3D1%3AP%3D2%2C%20CMD_BP_START%2FFAIR.SELECTOR.C%3D1%3AS%3D1%3AP%3D3%5D"), [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.acqTriggerName.value(), "CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2"sv));
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount < 20; });

        expect(eq(receivedData, getIota(20, 50)));
        expect(eq(lastDnsEntries.size(), 1UZ));
        if (!lastDnsEntries.empty()) {
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "count"sv));
            expect(eq(lastDnsEntries[0].unit, "A unit"sv));
            expect(eq(lastDnsEntries[0].sample_rate, 10.f));
        }
    };

    "Snapshot"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 100
      sample_rate: 10
      signal_unit: A unit
      signal_min: -42
      signal_max: 42
      timing_tags:
        - 40,hello
        - 50,shoot
        - 60,world
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;

        TestSetup test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count&acquisitionModeFilter=snapshot&triggerNameFilter=shoot&snapshotDelay=3000000000"), [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.acqTriggerName.value(), "shoot"sv));
            expect(eq(acq.channelUnit.value(), "A unit"sv));
            expect(eq(acq.channelRangeMin, -42.f));
            expect(eq(acq.channelRangeMax, 42.f));
            receivedData.insert(receivedData.end(), acq.channelValue.begin(), acq.channelValue.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount == 0; });

        // trigger + delay * sample_rate = 50 + 3 * 10 = 80
        expect(eq(receivedData, std::vector{80.f}));
        expect(eq(lastDnsEntries.size(), 1UZ));
        if (!lastDnsEntries.empty()) {
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "count"sv));
            expect(eq(lastDnsEntries[0].unit, "A unit"sv));
            expect(eq(lastDnsEntries[0].sample_rate, 10.f));
        }
    };

    "DataSet"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count
    id: CountSource
    parameters:
      n_samples: 100000
      signal_name: test signal
      signal_unit: test unit
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: fft
    id: gr::blocks::fft::FFT
  - name: test_sink
    id: gr::basic::DataSetSink
    parameters:
      signal_names:
        - Im(FFT(test signal)) # specified to avoid races when registering
        # Omit other signal_names to test propagation from DataSet objects
connections:
  - [count, 0, delay, 0]
  - [delay, 0, fft, 0]
  - [fft, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestSetup                  test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=Im%28FFT%28test%20signal%29%29&acquisitionModeFilter=dataset"), [&receivedCount](const auto& acq) {
            expect(eq(acq.channelValue.size(), 512UZ));
            expect(eq(acq.channelError.size(), 0UZ));
            expect(eq(acq.channelName.value(), "Im(FFT(test signal))"sv));
            expect(eq(acq.channelUnit.value(), "itest unit"sv));
            receivedCount++;
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&receivedCount] { return receivedCount < 97UZ; });
        expect(eq(receivedCount.load(), 97UZ));

        std::ranges::sort(lastDnsEntries, {}, &SignalEntry::name);

        expect(eq(lastDnsEntries.size(), 5UZ));
        if (lastDnsEntries.size() >= 5UZ) {
            expect(eq(lastDnsEntries[0].type, SignalType::DataSet));
            expect(eq(lastDnsEntries[0].name, "Im(FFT(test signal))"sv));
            expect(eq(lastDnsEntries[0].unit, "itest unit"sv));
            expect(eq(lastDnsEntries[0].sample_rate, 1.f));
            expect(eq(lastDnsEntries[1].type, SignalType::DataSet));
            expect(eq(lastDnsEntries[1].name, "Magnitude(test signal)"sv));
            expect(eq(lastDnsEntries[1].unit, "test unit/√Hz"sv));
            expect(eq(lastDnsEntries[1].sample_rate, 1.f));
            expect(eq(lastDnsEntries[2].type, SignalType::DataSet));
            expect(eq(lastDnsEntries[2].name, "Phase(test signal)"sv));
            expect(eq(lastDnsEntries[2].unit, "rad"sv));
            expect(eq(lastDnsEntries[2].sample_rate, 1.f));
            expect(eq(lastDnsEntries[3].type, SignalType::DataSet));
            expect(eq(lastDnsEntries[3].name, "Re(FFT(test signal))"sv));
            expect(eq(lastDnsEntries[3].unit, "test unit"sv));
            expect(eq(lastDnsEntries[3].sample_rate, 1.f));
            expect(eq(lastDnsEntries[4].type, SignalType::DataSet));
            expect(eq(lastDnsEntries[4].name, "test signal"sv));
            expect(eq(lastDnsEntries[4].unit, "Hz"sv));
            expect(eq(lastDnsEntries[4].sample_rate, 1.f));
        }
    };

    "Flow graph handling - Unknown block"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: unknown
    id: UnknownBlock
  - name: delay
    id: gr::testing::Delay
    parameters:
      delay_ms: 600
  - name: test_sink
    id: gr::basic::DataSink
    parameters:
      signal_name: count
connections:
  - [unknown, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        TestSetup                  test;

        std::atomic<bool> receivedReply = false;
        test.setGrc(grc, [&receivedReply](const auto& reply) {
            expect(eq(reply.data.asString(), std::string{}));
            expect(neq(reply.error, std::string{}));
            receivedReply = true;
        });

        waitWhile([&] { return !receivedReply; });
        expect(receivedReply.load());
    };

    "Dynamic signal metadata"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - name: count_up
    id: CountSource
    parameters:
      n_samples: 0
      signal_name: count_up
      signal_unit: Test unit A
      signal_min: -42
      signal_max: 42
  - name: count_down
    id: CountSource
    parameters:
      n_samples: 0
      direction: down
      signal_name: count_down
      signal_unit: Test unit B
      signal_min: 0
      signal_max: 100
  - name: test_sink_up
    id: gr::basic::DataSink
  - name: test_sink_down
    id: gr::basic::DataSink
connections:
  - [count_up, 0, test_sink_up, 0]
  - [count_down, 0, test_sink_down, 0]
)";

        // Here we rely on the signal_name propagation from the sources to the sinks. As that only happens at execution time, there's a delay between
        // the flowgraph execution starting and the listener registration succeeding, thus we don't get all the signal data from the start.
        std::atomic<std::size_t> receivedUpCount;
        std::atomic<std::size_t> receivedDownCount;
        std::vector<SignalEntry> lastDnsEntries;

        {
            TestSetup test([&lastDnsEntries](auto entries) {
                if (!entries.empty()) {
                    lastDnsEntries = std::move(entries);
                }
            });

            test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_up"), [&receivedUpCount](const Acquisition& acq) {
                expect(eq(acq.channelName.value(), "count_up"sv));
                expect(eq(acq.channelUnit.value(), "Test unit A"sv));
                expect(eq(acq.channelRangeMin, -42.f));
                expect(eq(acq.channelRangeMax, 42.f));
                receivedUpCount += acq.channelValue.size();
            });
            test.subscribeClient(URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?channelNameFilter=count_down"), [&receivedDownCount](const Acquisition& acq) {
                expect(eq(acq.channelName.value(), "count_down"sv));
                expect(eq(acq.channelUnit.value(), "Test unit B"sv));
                expect(eq(acq.channelRangeMin, 0.f));
                expect(eq(acq.channelRangeMax, 100.f));
                receivedDownCount += acq.channelValue.size();
            });

            test.setGrc(grc);
            waitWhile([&] { return receivedUpCount == 0 || receivedDownCount == 0; });
        }
        expect(receivedUpCount > 0);
        expect(receivedDownCount > 0);
        std::ranges::sort(lastDnsEntries, {}, &SignalEntry::name);
        expect(eq(lastDnsEntries.size(), 2UZ));
        if (lastDnsEntries.size() >= 2UZ) {
            expect(eq(lastDnsEntries[0].type, SignalType::Plain));
            expect(eq(lastDnsEntries[0].name, "count_down"sv));
            expect(eq(lastDnsEntries[0].unit, "Test unit B"sv));
            expect(eq(lastDnsEntries[1].type, SignalType::Plain));
            expect(eq(lastDnsEntries[1].name, "count_up"sv));
            expect(eq(lastDnsEntries[1].unit, "Test unit A"sv));
        }
    };
};

int main() { /* not needed for ut */ }
