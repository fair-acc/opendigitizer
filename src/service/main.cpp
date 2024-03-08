#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <services/dns.hpp>
#include <zmq/ZmqUtils.hpp>

#include <algorithm>
#include <fstream>
#include <thread>

#include "dashboard/dashboardWorker.hpp"
#include "gnuradio/GnuRadioWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

// TODO instead of including and registering blocks manually here, rely on the plugin system
#include <gnuradio-4.0/basic/common_blocks.hpp>
#include <gnuradio-4.0/basic/function_generator.h>
#include <gnuradio-4.0/basic/Selector.hpp>

#ifndef __EMSCRIPTEN__
#include <Picoscope4000a.hpp>
#endif

#include "build_configuration.hpp"
#include "settings.h"

// TODO use built-in GR blocks

template<typename T>
struct TestSource : public gr::Block<TestSource<T>> {
    using clock      = std::chrono::system_clock;
    using time_point = clock::time_point;
    gr::PortOut<T>            out;
    float                     sample_rate = 20000;
    std::size_t               _produced   = 0;
    std::optional<time_point> _start;

    void
    settingsChanged(const gr::property_map & /*old_settings*/, const gr::property_map & /*new_settings*/) {
        _produced = 0;
    }

    gr::work::Status
    processBulk(gr::PublishableSpan auto &output) noexcept {
        using enum gr::work::Status;
        auto       n   = output.size();
        const auto now = clock::now();
        if (_start) {
            const std::chrono::duration<float> duration = now - *_start;
            n                                           = std::min(static_cast<std::size_t>(duration.count() * sample_rate) - _produced, n);
        } else {
            _start = now;
            output.publish(0);
            return gr::work::Status::OK;
        }

        if (_produced == 0 && n > 0) {
            this->publishTag({ { std::string(gr::tag::SIGNAL_MIN.key()), -0.3f }, { std::string(gr::tag::SIGNAL_MAX.key()), 0.3f } }, 0);
        }

        const auto edgeLength = static_cast<std::size_t>(sample_rate / 200.f);
        auto       low        = (_produced / edgeLength) % 2 == 0;
        auto       firstChunk = std::span(output).first(std::min(n, edgeLength - (_produced % edgeLength)));
        std::fill(firstChunk.begin(), firstChunk.end(), low ? static_cast<T>(-0.3) : static_cast<T>(0.3));
        auto written = firstChunk.size();
        while (written < n) {
            low              = !low;
            const auto num   = std::min(n - written, edgeLength);
            auto       chunk = std::span(output).subspan(written, num);
            std::fill(chunk.begin(), chunk.end(), low ? static_cast<T>(-0.3) : static_cast<T>(0.3));
            written += num;
        }
        _produced += n;
        output.publish(n);
        return gr::work::Status::OK;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE(TestSource, out, sample_rate);

namespace {
template<typename Registry>
void                   registerTestBlocks(Registry &registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<TestSource, double, float>(registry);
    gr::registerBlock<gr::basic::DataSink, double, float, std::int16_t>(registry);
#ifndef __EMSCRIPTEN__
    gr::registerBlock<fair::picoscope::Picoscope4000a, double, float, std::int16_t>(registry);
#endif
#pragma GCC diagnostic pop
}
} // namespace

using namespace opencmw::majordomo;

int main(int argc, char **argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opencmw::majordomo;
    using namespace opencmw::service;
    using namespace std::chrono_literals;

    std::string grc = R"(
blocks:
  - name: source
    id: TestSource
  - name: sink
    id: gr::basic::DataSink
    parameters:
      signal_name: test
connections:
  - [source, 0, sink, 0]
)";
    if (argc > 1) {
        std::ifstream     in(argv[1]);
        std::stringstream grcBuffer;
        if (!(grcBuffer << in.rdbuf())) {
            fmt::println(std::cerr, "Could not read GRC file: {}", strerror(errno));
            return 1;
        }
        grc = grcBuffer.str();
    }

    Digitizer::Settings settings;
    Broker              broker("/PrimaryBroker");
    // REST backend
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<HTTPS, decltype(fs)>;
    RestBackend rest(broker, fs, SERVING_DIR);

    const auto  requestedAddress    = URI<>("mds://127.0.0.1:12345");
    const auto  brokerRouterAddress = broker.bind(requestedAddress);
    if (!brokerRouterAddress) {
        fmt::println(std::cerr, "Could not bind to broker address {}", requestedAddress.str());
        return 1;
    }
    std::jthread       brokerThread([&broker] {
        broker.run();
    });

    std::jthread       restThread([&rest] { rest.run(); });

    dns::DnsWorkerType dns_worker{ broker, dns::DnsHandler{} };
    std::jthread       dnsThread([&dns_worker] {
        dns_worker.run();
    });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"/dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    using GrAcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using GrFgWorker  = GnuRadioFlowGraphWorker<GrAcqWorker, "/flowgraph", description<"Provides access to the GnuRadio flow graph">>;
    gr::BlockRegistry registry;
    registerTestBlocks(registry);
    gr::PluginLoader                                          pluginLoader(registry, {});
    GrAcqWorker                                               grAcqWorker(broker, &pluginLoader, std::chrono::milliseconds(50));
    GrFgWorker                                                grFgWorker(broker, &pluginLoader, { grc, {} }, grAcqWorker);

    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY)));
    opencmw::client::ClientContext client{ std::move(clients) };

    dns::DnsClient                 dns_client{ client, "http://localhost:8080/dns" };

    std::vector<SignalEntry>       registeredSignals;
    grAcqWorker.setUpdateSignalEntriesCallback([&registeredSignals, &dns_client](std::vector<SignalEntry> signals) {
        std::ranges::sort(signals);
        std::vector<SignalEntry> toUnregister;
        std::ranges::set_difference(registeredSignals, signals, std::back_inserter(toUnregister));
        std::vector<SignalEntry> toRegister;
        std::ranges::set_difference(signals, registeredSignals, std::back_inserter(toRegister));

        auto dnsEntriesForSignal = [](const SignalEntry &entry) {
            // TODO do smarter host name and port, publish acquisition modes other than streaming?
            return std::vector{
                dns::Entry{ "http", "localhost", 8080, "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, "STREAMING" },
                dns::Entry{ "https", "localhost", 8080, "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, "STREAMING" },
                dns::Entry{ "mdp", "localhost", 12345, "/GnuRadio/Acquisition", "", entry.name, entry.unit, entry.sample_rate, "STREAMING" },
            };
        };
        std::vector<dns::Entry> toUnregisterEntries;
        for (const auto &signal : toUnregister) {
            auto dns = dnsEntriesForSignal(signal);
            toUnregisterEntries.insert(toUnregisterEntries.end(), dns.begin(), dns.end());
        }
        dns_client.unregisterSignals(std::move(toUnregisterEntries));

        std::vector<dns::Entry> toRegisterEntries;
        for (const auto &signal : toRegister) {
            auto dns = dnsEntriesForSignal(signal);
            toRegisterEntries.insert(toRegisterEntries.end(), dns.begin(), dns.end());
        }
        dns_client.registerSignals(std::move(toRegisterEntries));
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
