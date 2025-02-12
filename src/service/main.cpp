#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <services/dns.hpp>
#include <zmq/ZmqUtils.hpp>

#include <algorithm>
#include <fstream>
#include <thread>

#include <fmt/core.h>
#include <fmt/ranges.h>

#include "FAIR/DeviceNameHelper.hpp"
#include "dashboard/dashboardWorker.hpp"
#include "gnuradio/GnuRadioAcquisitionWorker.hpp"
#include "gnuradio/GnuRadioFlowgraphWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

// TODO instead of including and registering blocks manually here, rely on the plugin system
#include "build_configuration.hpp"
#include "settings.hpp"
#include <Picoscope4000a.hpp>
#include <gnuradio-4.0/basic/ConverterBlocks.hpp>
#include <gnuradio-4.0/basic/FunctionGenerator.hpp>
#include <gnuradio-4.0/basic/Selector.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#include <gnuradio-4.0/basic/StreamToDataSet.hpp>
#include <gnuradio-4.0/basic/clock_source.hpp>
#include <gnuradio-4.0/basic/common_blocks.hpp>
#include <gnuradio-4.0/electrical/PowerEstimators.hpp>
#include <gnuradio-4.0/filter/FrequencyEstimator.hpp>
#include <gnuradio-4.0/math/Math.hpp>
#include <gnuradio-4.0/testing/NullSources.hpp>

namespace {
template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<gr::basic::DataSink, double, float, std::int16_t>(registry);
    gr::registerBlock<gr::basic::DataSetSink, double, float, std::int16_t>(registry);
    gr::registerBlock<gr::blocks::type::converter::Convert, gr::BlockParameters<double, float>, gr::BlockParameters<float, double>>(registry);
    gr::registerBlock<fair::picoscope::Picoscope4000a, fair::picoscope::AcquisitionMode::Streaming, float, std::int16_t>(registry); // ommitting gr::UncertainValue<float> for now, which would also be supported by picoscope block
    gr::registerBlock<gr::basic::FunctionGenerator, float>(registry);
    gr::registerBlock<gr::basic::SignalGenerator, float>(registry);
    gr::registerBlock<gr::basic::DefaultClockSource, std::uint8_t, double>(registry);
    gr::registerBlock<MultiAdder, float>(registry);
    gr::registerBlock<gr::basic::StreamToDataSet, double>(registry);
    gr::registerBlock<gr::blocks::math::MathOpImpl, '*', float>(registry);
    gr::registerBlock<gr::electrical::PowerMetrics, 1, double>(registry);
    gr::registerBlock<gr::electrical::PowerMetrics, 3, double>(registry);
    gr::registerBlock<gr::electrical::PowerFactor, 1, double>(registry);
    gr::registerBlock<gr::electrical::PowerFactor, 3, double>(registry);
    gr::registerBlock<gr::electrical::SystemUnbalance, 3, double>(registry);
    gr::registerBlock<gr::filter::FrequencyEstimatorTimeDomain, double>(registry);
    gr::registerBlock<gr::filter::FrequencyEstimatorFrequencyDomain, double>(registry);
    gr::registerBlock<gr::testing::NullSink, float, double>(registry);

    fmt::print("providedBlocks:\n");
    for (auto& blockName : registry.providedBlocks()) {
        fmt::print("  - {}: {}\n", blockName, registry.knownBlockParameterizations(blockName));
    }
#pragma GCC diagnostic pop
}
} // namespace

using namespace opencmw::majordomo;

int main(int argc, char** argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opendigitizer::gnuradio;
    using namespace opencmw::majordomo;
    using namespace opencmw::service;
    using namespace std::chrono_literals;

    std::string grc = R"(blocks:
  - name: source
    id: gr::basic::DefaultClockSource
    template_args: double
  - name: sink
    id: gr::basic::DataSink
    parameters:
      signal_name: test
    template_args: double
connections:
  - [source, 0, sink, 0]
)";
    if (argc > 1) {
        std::ifstream     in(argv[1]);
        std::stringstream grcBuffer;
        if (!(grcBuffer << in.rdbuf())) {
            fmt::println(stderr, "Could not read GRC file: {}", strerror(errno));
            return 1;
        }
        grc = grcBuffer.str();
    }

    Digitizer::Settings& settings = Digitizer::Settings::instance();
    fmt::print("Settings: host/port: {}:{}, {} {}\nwasmServeDir: {}\n", settings.hostname, settings.port, settings.disableHttps ? "(http disabled), " : "", settings.checkCertificates ? "(cert check disabled), " : "", settings.wasmServeDir);
    Broker broker("/PrimaryBroker");
    // REST backend
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<HTTPS, decltype(fs)>;
    RestBackend rest(broker, fs, settings.wasmServeDir != "" ? settings.wasmServeDir : SERVING_DIR, settings.serviceUrl().build());

    const auto requestedAddress    = URI<>("mds://127.0.0.1:12345");
    const auto brokerRouterAddress = broker.bind(requestedAddress);
    if (!brokerRouterAddress) {
        fmt::println(std::cerr, "Could not bind to broker address {}", requestedAddress.str());
        return 1;
    }
    std::jthread brokerThread([&broker] { broker.run(); });

    std::jthread restThread([&rest] { rest.run(); });

    dns::DnsWorkerType dns_worker{broker, dns::DnsHandler{}};
    std::jthread       dnsThread([&dns_worker] { dns_worker.run(); });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"/dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    using GrAcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using GrFgWorker  = GnuRadioFlowGraphWorker<GrAcqWorker, "/flowgraph", description<"Provides access to the GnuRadio flow graph">>;
    gr::BlockRegistry registry;
    registerTestBlocks(registry);
    gr::PluginLoader pluginLoader(registry, {});
    GrAcqWorker      grAcqWorker(broker, &pluginLoader, 50ms);
    GrFgWorker       grFgWorker(broker, &pluginLoader, {grc, {}}, grAcqWorker);

    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY)));
    opencmw::client::ClientContext client{std::move(clients)};

    dns::DnsClient dns_client{client, settings.serviceUrl().path("/dns").build()};

    const auto restUrl = settings.serviceUrl().build();

    std::vector<SignalEntry> registeredSignals;
    grAcqWorker.setUpdateSignalEntriesCallback([&registeredSignals, &dns_client, &restUrl](std::vector<SignalEntry> signals) {
        if (::getenv("OPENDIGITIZER_LOAD_TEST_SIGNALS")) {
            size_t x = 0;
            for (auto& i : fair::testDeviceNames) {
                if (x >= 12) {
                    break;
                }
                const auto  info = fair::getDeviceInfo(i);
                SignalEntry entry;
                entry.name        = info.name;
                entry.sample_rate = 1.f;
                entry.unit        = "TEST unit";
                signals.push_back(entry);
                x++;
            }
        }

        std::ranges::sort(signals);
        std::vector<SignalEntry> toUnregister;
        std::ranges::set_difference(registeredSignals, signals, std::back_inserter(toUnregister));
        std::vector<SignalEntry> toRegister;
        std::ranges::set_difference(signals, registeredSignals, std::back_inserter(toRegister));

        auto dnsEntriesForSignal = [&restUrl](const SignalEntry& entry) {
            // TODO publish acquisition modes other than streaming?
            // TODO mdp not functional (not implemented in worker)
            const auto type = entry.type == SignalType::Plain ? "STREAMING" : "DATASET";
            return std::vector{
                dns::Entry{*restUrl.scheme(), *restUrl.hostName(), *restUrl.port(), "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, type},
                // dns::Entry{"mdp", *restUrl.hostName(), 12345, "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, "STREAMING"},
                // dns::Entry{"mds", *restUrl.hostName(), 12345, "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, "STREAMING"}
            };
        };
        std::vector<dns::Entry> toUnregisterEntries;
        for (const auto& signal : toUnregister) {
            auto dns = dnsEntriesForSignal(signal);
            toUnregisterEntries.insert(toUnregisterEntries.end(), dns.begin(), dns.end());
        }
        dns_client.unregisterSignals(std::move(toUnregisterEntries));

        std::vector<dns::Entry> toRegisterEntries;
        for (const auto& signal : toRegister) {
            auto dns = dnsEntriesForSignal(signal);
            toRegisterEntries.insert(toRegisterEntries.end(), dns.begin(), dns.end());
        }
        dns_client.registerSignals(std::move(toRegisterEntries));
        registeredSignals = std::move(signals);
    });

    std::jthread grAcqWorkerThread([&grAcqWorker] { grAcqWorker.run(); });
    std::jthread grFgWorkerThread([&grFgWorker] { grFgWorker.run(); });

    brokerThread.join();
    restThread.join();

    client.stop();

    dnsThread.join();
    dashboardWorkerThread.join();
    grAcqWorkerThread.join();
    grFgWorkerThread.join();
}
