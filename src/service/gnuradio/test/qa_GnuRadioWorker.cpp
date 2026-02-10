#include <Client.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYAML.hpp>
#include <IoSerialiserYaS.hpp>
#include <RestClient.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <zmq/ZmqUtils.hpp>

#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/fourier/fft.hpp>
#include <gnuradio-4.0/meta/UnitTestHelper.hpp>
#include <gnuradio-4.0/meta/formatter.hpp>
#include <gnuradio-4.0/testing/Delay.hpp>

#include <boost/ut.hpp>
#include <format>
#include <print>

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
    gr::registerBlock<CountSource, float>(registry);
    gr::registerBlock<ForeverSource, float>(registry);
    gr::registerBlock<gr::basic::DataSetSink, float>(registry);
    gr::registerBlock<gr::basic::DataSink, float>(registry);
    gr::registerBlock<gr::blocks::fft::DefaultFFT, float>(registry);
    gr::registerBlock<gr::testing::Delay, float>(registry);
    gr::registerBlock<gr::basic::StreamToDataSet, float>(registry);
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

namespace {
std::vector<float> getIota(std::size_t n, float first = 0.f) {
    std::vector<float> v(n);
    std::iota(v.begin(), v.end(), first);
    return v;
}

client::ClientContext makeClient(zmq::Context& ctx) {
    std::vector<std::unique_ptr<client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<client::MDClientCtx>(ctx, 20ms, ""));
    clients.emplace_back(std::make_unique<client::RestClient>(client::DefaultContentTypeHeader(MIME::BINARY), opencmw::client::VerifyServerCertificates(false)));
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

template<typename T>
std::span<T> samplesForSignalIndex(MultiArray<T, 2>& arr, size_t signalInd) {
    const size_t nSamples = arr.dimensions()[1];
    return std::span<T>(arr.elements().data() + signalInd * nSamples, nSamples);
}

template<typename T>
std::span<const T> samplesForSignalIndex(const MultiArray<T, 2>& arr, size_t signalInd) {
    const size_t nSamples = arr.dimensions()[1];
    return std::span<const T>(arr.elements().data() + signalInd * nSamples, nSamples);
}

void checkAcquisitionMeta(const Acquisition& acq, std::size_t nSignals_, std::size_t nSamples_, const std::vector<std::string>& names, const std::vector<std::string>& units, const std::vector<std::string>& quantities, //
    const std::vector<float>& rangeMin, const std::vector<float>& rangeMax, std::string_view configStr, std::source_location location = std::source_location::current()) {
    const auto nSignals = static_cast<std::uint32_t>(nSignals_);
    const auto nSamples = static_cast<std::uint32_t>(nSamples_);

    std::string configAndLoc = std::format("\n{} \ncheckAcquisitionMeta() at {}:{}", configStr, location.file_name(), location.line());

    expect(eq(acq.channelValues.n(0), nSignals)) << configAndLoc;
    expect(eq(acq.channelValues.n(1), nSamples)) << configAndLoc;
    expect(eq(acq.channelValues.elements().size(), nSignals * nSamples)) << configAndLoc;
    expect(eq(acq.channelErrors.n(0), nSignals)) << configAndLoc;
    expect(eq(acq.channelErrors.n(1), nSamples)) << configAndLoc;
    expect(eq(acq.channelErrors.elements().size(), nSignals * nSamples)) << configAndLoc;

    if (!names.empty()) {
        expect(eq(acq.channelNames.size(), nSignals)) << configAndLoc;
        expect(eq(acq.channelNames, names)) << configAndLoc;
    }
    if (!units.empty()) {
        expect(eq(acq.channelUnits.size(), nSignals)) << configAndLoc;
        expect(eq(acq.channelUnits, units)) << configAndLoc;
    }
    if (!quantities.empty()) {
        expect(eq(acq.channelQuantities.size(), nSignals)) << configAndLoc;
        expect(eq(acq.channelQuantities, quantities)) << configAndLoc;
    }
    if (!rangeMin.empty()) {
        expect(eq(acq.channelRangeMin.size(), nSignals)) << configAndLoc;
        expect(eq(acq.channelRangeMin, rangeMin)) << configAndLoc;
    }
    if (!rangeMax.empty()) {
        expect(eq(acq.channelRangeMax.size(), nSignals)) << configAndLoc;
        expect(eq(acq.channelRangeMax, rangeMax)) << configAndLoc;
    }
}

void checkDnsEntries(std::vector<SignalEntry> lastDnsEntries, const std::vector<SignalType>& types, const std::vector<std::string>& names, const std::vector<std::string>& units, //
    const std::vector<std::string>& quantities, const std::vector<float>& sampleRates, std::string_view configStr, std::source_location location = std::source_location::current()) {
    std::string configAndLoc = std::format("\n{} \ncheckDnsEntries() at {}:{}", configStr, location.file_name(), location.line());

    std::ranges::sort(lastDnsEntries, {}, &SignalEntry::name);

    if (!types.empty()) {
        expect(eq(lastDnsEntries.size(), types.size())) << configAndLoc;
        expect(eq(lastDnsEntries | std::views::transform([](const auto& dnsEntry) { return dnsEntry.type; }), types)) << configAndLoc;
    }
    if (!names.empty()) {
        expect(eq(lastDnsEntries.size(), names.size())) << configAndLoc;
        expect(eq(lastDnsEntries | std::views::transform([](const auto& dnsEntry) { return dnsEntry.name; }), names)) << configAndLoc;
    }
    if (!units.empty()) {
        expect(eq(lastDnsEntries.size(), units.size())) << configAndLoc;
        expect(eq(lastDnsEntries | std::views::transform([](const auto& dnsEntry) { return dnsEntry.unit; }), units)) << configAndLoc;
    }
    if (!quantities.empty()) {
        expect(eq(lastDnsEntries.size(), quantities.size())) << configAndLoc;
        expect(eq(lastDnsEntries | std::views::transform([](const auto& dnsEntry) { return dnsEntry.quantity; }), quantities)) << configAndLoc;
    }
    if (!sampleRates.empty()) {
        expect(eq(lastDnsEntries.size(), sampleRates.size())) << configAndLoc;
        expect(eq(lastDnsEntries | std::views::transform([](const auto& dnsEntry) { return dnsEntry.sample_rate; }), sampleRates)) << configAndLoc;
    }
}
} // namespace

struct TestConfig {
    enum class Protocol { http, mds };
    enum class Serializer { YaS, CmwLight, Json };

    Protocol   protocol   = Protocol::mds;
    Serializer serializer = Serializer::YaS;

    std::string toString() const { return std::format("TestConfig(protocol: {}, serializer: {})", magic_enum::enum_name(protocol), magic_enum::enum_name(serializer)); }
};

struct TestApp {
    using AcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data acquisition updates">>;
    using FgWorker  = GnuRadioFlowGraphWorker<AcqWorker, "/GnuRadio/FlowGraph", description<"Provides access to flow graph">>;

    inline static constexpr std::uint16_t    httpPort = 12347;
    inline static constexpr std::string_view httpHost = "https://127.0.0.1:12347";
    inline static constexpr std::string_view mdpHost  = "mdp://127.0.0.1:12346";
    inline static constexpr std::string_view mdsHost  = "mds://127.0.0.1:12345";

    TestConfig config;

    gr::BlockRegistry registry = [] {
        gr::BlockRegistry r;
        registerTestBlocks(r);
        return r;
    }();
    gr::PluginLoader      pluginLoader = gr::PluginLoader(registry, gr::globalSchedulerRegistry(), {});
    majordomo::Broker<>   broker       = majordomo::Broker<>("/PrimaryBroker");
    AcqWorker             acqWorker    = AcqWorker(broker, &pluginLoader, 50ms);
    FgWorker              fgWorker     = FgWorker(broker, &pluginLoader, {}, acqWorker);
    std::jthread          brokerThread;
    std::jthread          acqWorkerThread;
    std::jthread          fgWorkerThread;
    zmq::Context          ctx;
    client::ClientContext client = makeClient(ctx);

    explicit TestApp(std::function<void(std::vector<SignalEntry>)> dnsCallback = {}) {
        const auto brokerPubAddress = broker.bind(URI<>(std::string(mdsHost)));
        expect((brokerPubAddress.has_value() == "bound successful"_b));
        const auto brokerRouterAddress = broker.bind(URI<>(std::string(mdpHost)));
        expect((brokerRouterAddress.has_value() == "bound successful"_b));
        using namespace opencmw::majordomo;
        opencmw::majordomo::rest::Settings restSettings;

        restSettings.certificateFilePath = getEnvironmentVariable("OPENCMW_REST_CERT_FILE", "demo_public.crt");
        restSettings.keyFilePath         = getEnvironmentVariable("OPENCMW_REST_PRIVATE_KEY_FILE", "demo_private.key");
        std::println("Using certificate file: {}", restSettings.certificateFilePath.string());
        std::println("Using private key file: {}", restSettings.keyFilePath.string());
        restSettings.port      = httpPort;
        restSettings.protocols = opencmw::majordomo::rest::Protocol::Http2;
        if (auto rc = broker.bindRest(restSettings); !rc) {
            std::println(std::cerr, "Could not bind REST bridge: {}", rc.error());
        }
        acqWorker.setUpdateSignalEntriesCallback(std::move(dnsCallback));

        brokerThread    = std::jthread([this] { broker.run(); });
        acqWorkerThread = std::jthread([this] { acqWorker.run(); });
        fgWorkerThread  = std::jthread([this] { fgWorker.run(); });
        // let's give everyone some time to spin up and sort themselves
        std::this_thread::sleep_for(100ms);
    }

    std::string getEnvironmentVariable(const char* name, std::string_view defaultValue) {
        if (auto value = std::getenv(name); value) {
            return std::string(value);
        } else {
            return std::string(defaultValue);
        }
    };

    void subscribeClient(std::string_view relativeUri, std::function<void(const Acquisition&)>&& handlerFnc) {
        std::string_view host          = config.protocol == TestConfig::Protocol::http ? httpHost : mdsHost;
        std::string      serializerStr = "";
        if (config.serializer == TestConfig::Serializer::Json) {
            serializerStr = "&contentType=application/json";
        } else if (config.serializer == TestConfig::Serializer::CmwLight) {
            serializerStr = "&contentType=application/cmwlight";
        }
        const auto uri = URI(std::format("{}{}{}", host, relativeUri, serializerStr));

        client.subscribe(uri, [cfg = config, handler = std::move(handlerFnc)](const mdp::Message& update) {
            std::println("Client 'received message protocol:{} from service '{}' for topic '{}'", update.protocolName, update.serviceName, update.topic.str());
            if (update.error != "") {
                return;
            }
            Acquisition acq;
            IoBuffer    buffer(update.data);
            try {
                DeserialiserInfo result;
                if (cfg.serializer == TestConfig::Serializer::YaS) {
                    result = deserialise<opencmw::YaS, ProtocolCheck::ALWAYS>(buffer, acq);
                } else if (cfg.serializer == TestConfig::Serializer::Json) {
                    result = deserialise<opencmw::Json, ProtocolCheck::IGNORE>(buffer, acq);
                } else if (cfg.serializer == TestConfig::Serializer::CmwLight) {
                    result = deserialise<opencmw::CmwLight, ProtocolCheck::IGNORE>(buffer, acq);
                }
                if (!result.exceptions.empty()) {
                    throw result.exceptions.front();
                }
                handler(acq);
            } catch (const ProtocolException& e) {
                std::println(std::cerr, "Parsing failed: {}", e.what());
                throw;
            }
        });
    }

    void setGrc(std::string_view grc, auto callback) {
        opendigitizer::flowgraph::Flowgraph fg{std::string(grc), {}};
        gr::Message                         message;
        message.endpoint = "ReplaceGraphGRC";
        opendigitizer::flowgraph::storeFlowgraphToMessage(fg, message);

        const auto serialisedMessage = serialiseMessage(message);

        opendigitizer::flowgraph::SerialisedFlowgraphMessage serialised;
        serialised.data = serialisedMessage;

        IoBuffer buffer;
        opencmw::serialise<opencmw::Json>(buffer, serialised);

        std::print("Sending ReplaceGraphGRC message to the service {}\n", buffer.asString());
        client.set(URI(std::string(mdpHost) + "/GnuRadio/FlowGraph"s), std::move(callback), std::move(buffer));
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

    ~TestApp() {
        client.stop();
        broker.shutdown();
        brokerThread.join();
        acqWorkerThread.join();
        fgWorkerThread.join();
    }
};

const boost::ut::suite GnuRadioWorker_tests = [] {
    using enum TestConfig::Protocol;
    using enum TestConfig::Serializer;
    constexpr std::array testConfigs{
        TestConfig{http, YaS},
        TestConfig{http, Json},
        TestConfig{http, CmwLight},
        TestConfig{mds, YaS},
        TestConfig{mds, Json},
        TestConfig{mds, CmwLight},
    };

    "Streaming"_test = [](auto config) {
        constexpr std::string_view grc = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count_up
      n_samples: 100000
      signal_unit: "Unit_Up"
      signal_quantity: "Quantity_Up"
      signal_min: 0
      signal_max: 99
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay_up
      delay_ms: 600
  - id: CountSource<float32>
    parameters:
      name: count_down
      n_samples: 100000
      initial_value: 99999
      direction: down
      signal_unit: "Unit_Down"
      signal_quantity: "Quantity_Down"
      signal_min: 0
      signal_max: 99
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay_down
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_up
      signal_name: "Signal_Up"
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_down
      signal_name: "Signal_Down"
connections:
  - [count_up, 0, delay_up, 0]
  - [delay_up, 0, test_sink_up, 0]
  - [count_down, 0, delay_down, 0]
  - [delay_down, 0, test_sink_down, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestApp                    test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });
        test.config = config;

        constexpr std::size_t    kExpectedSamples = 100'000; // this number should be greater than the buffer size to ensure that the metadata is propagated
        const std::vector<float> expectedUpData   = getIota(kExpectedSamples);
        auto                     expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_Up", [&receivedUpData, &receivedUpCount, &config](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_Up"}, {"Unit_Up"}, {"Quantity_Up"}, {0.f}, {99.f}, config.toString());
            receivedUpData.insert(receivedUpData.end(), samples.begin(), samples.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_Down", [&receivedDownData, &receivedDownCount, &config](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_Down"}, {"Unit_Down"}, {"Quantity_Down"}, {0.f}, {99.f}, config.toString());
            receivedDownData.insert(receivedDownData.end(), samples.begin(), samples.end());
            receivedDownCount = receivedDownData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedUpCount < kExpectedSamples || receivedDownCount < kExpectedSamples; });

        expect(eq(receivedUpData.size(), kExpectedSamples)) << config.toString();
        expect(eq(receivedUpData, expectedUpData)) << config.toString();
        expect(eq(receivedDownData.size(), kExpectedSamples)) << config.toString();
        expect(eq(receivedDownData, expectedDownData)) << config.toString();

        std::this_thread::sleep_for(50ms);
        checkDnsEntries(lastDnsEntries, {SignalType::Plain, SignalType::Plain}, {"Signal_Down", "Signal_Up"}, {"Unit_Down", "Unit_Up"}, {"Quantity_Down", "Quantity_Up"}, {}, config.toString());
    } | testConfigs;

    "Flow graph management"_test = [] {
        constexpr std::string_view grc1 = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count_up
      n_samples: 100
      signal_unit: "Unit_Up"
      signal_quantity: "Quantity_Up"
      signal_min: 0
      signal_max: 99
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_up
      signal_name: "Signal_Up"
connections:
  - [count_up, 0, delay, 0]
  - [delay, 0, test_sink_up, 0]
)";
        constexpr std::string_view grc2 = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count_down
      n_samples: 100
      initial_value: 99
      direction: down
      signal_unit: "Unit_Down"
      signal_quantity: "Quantity_Down"
      signal_min: 0
      signal_max: 99
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_down
      signal_name: "Signal_Down"
connections:
  - [count_down, 0, delay, 0]
  - [delay, 0, test_sink_down, 0]
)";

        TestApp test;

        constexpr std::size_t    kExpectedSamples = 100;
        const std::vector<float> expectedUpData   = getIota(kExpectedSamples);
        auto                     expectedDownData = expectedUpData;
        std::reverse(expectedDownData.begin(), expectedDownData.end());

        std::vector<float>       receivedUpData;
        std::atomic<std::size_t> receivedUpCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_Up", [&receivedUpData, &receivedUpCount](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_Up"}, {"Unit_Up"}, {"Quantity_Up"}, {0.f}, {99.f}, "");
            receivedUpData.insert(receivedUpData.end(), samples.begin(), samples.end());
            receivedUpCount = receivedUpData.size();
        });

        std::vector<float>       receivedDownData;
        std::atomic<std::size_t> receivedDownCount = 0;
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_Down", [&receivedDownData, &receivedDownCount](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_Down"}, {"Unit_Down"}, {"Quantity_Down"}, {0.f}, {99.f}, "");
            receivedDownData.insert(receivedDownData.end(), samples.begin(), samples.end());
            receivedDownCount = receivedDownData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);
        std::this_thread::sleep_for(2000ms);
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
  - id: ForeverSource<float32>
    parameters:
      name: source1
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink1
      signal_name: "Signal_A"
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
      sample_rate: 1234
connections:
  - [source1, 0, test_sink1, 0]
)";
        constexpr std::string_view grc2 = R"(
blocks:
  - id: ForeverSource<float32>
    parameters:
      name: source2
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink2
      signal_name: "Signal_B"
      signal_unit: "Unit_B"
      signal_quantity: "Quantity_B"
      sample_rate: 123456
connections:
  - [source2, 0, test_sink2, 0]
)";

        std::mutex               dnsMutex;
        std::vector<SignalEntry> lastDnsEntries;
        TestApp                  test([&lastDnsEntries, &dnsMutex](auto entries) {
            if (!entries.empty()) {
                std::lock_guard lock(dnsMutex);
                lastDnsEntries = std::move(entries);
            }
        });

        std::atomic<std::size_t> receivedCount1 = 0;
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A", [&receivedCount1](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {}, {}, "");
            receivedCount1 += samples.size();
        });

        std::atomic<std::size_t> receivedCount2 = 0;
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_B", [&receivedCount2](const auto& acq) {
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_B"}, {"Unit_B"}, {"Quantity_B"}, {}, {}, "");
            receivedCount2 += samples.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc1);
        std::this_thread::sleep_for(2000ms);

        {
            std::lock_guard lock(dnsMutex);
            checkDnsEntries(lastDnsEntries, {SignalType::Plain}, {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {1234.f}, "");
        }
        test.setGrc(grc2);

        constexpr auto kExpectedSamples = 50000UZ;
        waitWhile([&] { return receivedCount1 < kExpectedSamples || receivedCount2 < kExpectedSamples; });

        std::lock_guard lock(dnsMutex);
        checkDnsEntries(lastDnsEntries, {SignalType::Plain}, {"Signal_B"}, {"Unit_B"}, {"Quantity_B"}, {123456.f}, "");
    };

    "Trigger - tightly packed tags"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 100
      timing_tags: !!str
        - 40,notatrigger
        - 50,hello
        - 60,ignoreme
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink
      signal_name: "Signal_A"
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";

        TestApp test;

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15", [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.refTriggerName.value(), "hello"sv));
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {}, {}, "");
            receivedData.insert(receivedData.end(), samples.begin(), samples.end());
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
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 10000000
      timing_tags: !!str
        - 1000,notatrigger
        - 800000,hello
        - 900000,ignoreme
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink
      signal_name: "Signal_A"
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";

        TestApp test;

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A&acquisitionModeFilter=triggered&triggerNameFilter=hello&preSamples=5&postSamples=15", [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.refTriggerName.value(), "hello"sv));
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {}, {}, "");
            receivedData.insert(receivedData.end(), samples.begin(), samples.end());
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
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 100000
      sample_rate: 10.0
      timing_tags: !!str
        - 30,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=1
        - 50,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2
        - 70,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=3
        - 80,CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=4
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink
      signal_name: "Signal_A"
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestApp                    test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        // filter "[CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2, CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=3]" => samples [50..69]
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A&acquisitionModeFilter=multiplexed&triggerNameFilter=%5BCMD_BP_START%2FFAIR.SELECTOR.C%3D1%3AS%3D1%3AP%3D2%2C%20CMD_BP_START%2FFAIR.SELECTOR.C%3D1%3AS%3D1%3AP%3D3%5D", [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.refTriggerName.value(), "CMD_BP_START/FAIR.SELECTOR.C=1:S=1:P=2"sv));
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            receivedData.insert(receivedData.end(), samples.begin(), samples.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount < 20; });
        std::this_thread::sleep_for(50ms);

        expect(eq(receivedData, getIota(20, 50)));

        checkDnsEntries(lastDnsEntries, {SignalType::Plain}, {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {10.f}, "");
    };

    "Snapshot"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 100000
      sample_rate: 10
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
      signal_min: -42
      signal_max: 42
      timing_tags: !!str
        - 40,hello
        - 50,shoot
        - 60,world
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink
      signal_name: "Signal_A"
connections:
  - [count, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;

        TestApp test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        std::vector<float>       receivedData;
        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A&acquisitionModeFilter=snapshot&triggerNameFilter=shoot&snapshotDelay=3000000000", [&receivedData, &receivedCount](const auto& acq) {
            expect(eq(acq.refTriggerName.value(), "shoot"s));
            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            checkAcquisitionMeta(acq, 1UZ, samples.size(), {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {-42.f}, {42.f}, "");
            receivedData.insert(receivedData.end(), samples.begin(), samples.end());
            receivedCount = receivedData.size();
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&] { return receivedCount == 0; });

        std::this_thread::sleep_for(50ms);

        // trigger + delay * sample_rate = 50 + 3 * 10 = 80
        expect(eq(receivedData, std::vector{80.f}));

        checkDnsEntries(lastDnsEntries, {SignalType::Plain}, {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {10.f}, "");
    };

    "DataSet FFT"_test = [](auto config) {
        constexpr std::string_view grc = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 100000
      signal_name: test signal
      signal_unit: test unit
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::blocks::fft::FFT<float32, gr::DataSet<float32>, gr::algorithm::FFT>
    parameters:
      name: fft
  - id: gr::basic::DataSetSink<float32>
    parameters:
      name: test_sink
      signal_name: FFTTestSignal
connections:
  - [count, 0, delay, 0]
  - [delay, 0, fft, 0]
  - [fft, 0, test_sink, 0]
)";
        std::vector<SignalEntry>   lastDnsEntries;
        TestApp                    test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });
        test.config = config;

        std::atomic<std::size_t> receivedCount = 0;

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=FFTTestSignal&acquisitionModeFilter=dataset", [&receivedCount, &config](const auto& acq) {
            checkAcquisitionMeta(acq, 4UZ, 512UZ, {"Magnitude(test signal)", "Phase(test signal)", "Re(FFT(test signal))", "Im(FFT(test signal))"}, {"test unit/√Hz", "rad", "Retest unit", "Imtest unit"}, //
                {"Magnitude(FFT)", "Phase(FFT)", "Re(FFT)", "Im(FFT)"}, {}, {}, config.toString());
            receivedCount++;
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&receivedCount] { return receivedCount < 97UZ; });
        expect(eq(receivedCount.load(), 97UZ)) << config.toString();

        checkDnsEntries(lastDnsEntries, {SignalType::DataSet}, {"FFTTestSignal"}, {}, {}, {/*1.0f*/}, config.toString()); // TODO: verify correct handling of sample rate
    } | testConfigs;

    "DataSet signal values"_test = [](auto config) {
        constexpr std::string_view grc = R"(
blocks:
  - id: CountSource<float32>
    parameters:
      name: count
      n_samples: 100
      timing_tags: !!str
        - 20,mytrigger
        - 50,mytrigger
        - 70,mytrigger
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::StreamFilterImpl<float32, false, gr::trigger::BasicTriggerNameCtxMatcher::Filter>
    parameters:
      name: stream_to_dataset
      filter: "mytrigger"
      n_pre: 2
      n_post: 2
  - id: gr::basic::DataSetSink<float32>
    parameters:
      name: test_sink
      signal_name: Signal_A
connections:
  - [count, 0, delay, 0]
  - [delay, 0, stream_to_dataset, 0]
  - [stream_to_dataset, 0, test_sink, 0]
)";
        std::vector<float>         receivedData;
        std::atomic<std::size_t>   receivedCount        = 0;
        std::atomic<std::size_t>   receivedDataSetCount = 0;
        TestApp                    test;
        test.config = config;
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A&acquisitionModeFilter=dataset", [&receivedCount, &receivedData, &receivedDataSetCount, &config](const auto& acq) {
            checkAcquisitionMeta(acq, 1UZ, 4UZ, {}, {}, {}, {}, {}, config.toString());
            receivedCount++;

            const auto samples = samplesForSignalIndex(acq.channelValues, 0);
            receivedData.insert(receivedData.end(), samples.begin(), samples.end());
            receivedCount = receivedData.size();
            receivedDataSetCount++;
        });

        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);

        waitWhile([&receivedCount] { return receivedCount < 12UZ; });
        expect(eq(receivedCount.load(), 12UZ)) << config.toString();
        expect(eq(receivedDataSetCount.load(), 3UZ)) << config.toString();
        std::vector<float> expectedData = {18.f, 19.f, 20.f, 21.f, 48.f, 49.f, 50.f, 51.f, 68.f, 69.f, 70.f, 71.f};
        expect(eq(receivedData, expectedData)) << config.toString();
    } | testConfigs;

    "Flow graph handling - Unknown block"_test = [] {
        constexpr std::string_view grc = R"(
blocks:
  - id: UnknownBlock<float32>
    parameters:
      name: unknown
  - id: gr::testing::Delay<float32>
    parameters:
      name: delay
      delay_ms: 600
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink
      signal_name: count
connections:
  - [unknown, 0, delay, 0]
  - [delay, 0, test_sink, 0]
)";

        TestApp test;

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
  - id: CountSource<float32>
    parameters:
      name: count_up
      n_samples: 0
      signal_min: -42
      signal_max: 42
  - id: CountSource<float32>
    parameters:
      name: count_down
      n_samples: 0
      direction: down
      signal_min: 0
      signal_max: 100
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_up
      signal_name: "Signal_A"
      signal_unit: "Unit_A"
      signal_quantity: "Quantity_A"
  - id: gr::basic::DataSink<float32>
    parameters:
      name: test_sink_down
      signal_name: "Signal_B"
      signal_unit: "Unit_B"
      signal_quantity: "Quantity_B"
connections:
  - [count_up, 0, test_sink_up, 0]
  - [count_down, 0, test_sink_down, 0]
)";
        std::atomic<std::size_t>   receivedUpCount;
        std::atomic<std::size_t>   receivedDownCount;
        std::vector<SignalEntry>   lastDnsEntries;

        TestApp test([&lastDnsEntries](auto entries) {
            if (!entries.empty()) {
                lastDnsEntries = std::move(entries);
            }
        });

        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_A", [&receivedUpCount](const Acquisition& acq) {
            const auto nSamples = samplesForSignalIndex(acq.channelValues, 0).size();
            checkAcquisitionMeta(acq, 1UZ, nSamples, {"Signal_A"}, {"Unit_A"}, {"Quantity_A"}, {-42.f}, {42.f}, "");
            receivedUpCount += nSamples;
        });
        // TODO: A second client uses always `mds` as a workaround due to a bug in RestClientNative, which prevents creating multiple subscriptions with a single client instance.
        test.subscribeClient("/GnuRadio/Acquisition?channelNameFilter=Signal_B", [&receivedDownCount](const Acquisition& acq) {
            const auto nSamples = samplesForSignalIndex(acq.channelValues, 0).size();
            checkAcquisitionMeta(acq, 1UZ, nSamples, {"Signal_B"}, {"Unit_B"}, {"Quantity_B"}, {0.f}, {100.f}, "");
            receivedDownCount += nSamples;
        });
        std::this_thread::sleep_for(50ms);
        test.setGrc(grc);
        waitWhile([&] { return receivedUpCount == 0 || receivedDownCount == 0; });

        expect(receivedUpCount > 0);
        expect(receivedDownCount > 0);

        checkDnsEntries(lastDnsEntries, {SignalType::Plain, SignalType::Plain}, {"Signal_A", "Signal_B"}, {"Unit_A", "Unit_B"}, {"Quantity_A", "Quantity_B"}, {}, "");
    };
};

int main() { /* not needed for ut */ }
