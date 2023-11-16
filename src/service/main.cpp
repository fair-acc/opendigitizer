#include <Client.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <services/dns.hpp>
#include <zmq/ZmqUtils.hpp>

#include <fstream>
#include <thread>

#include "dashboard/dashboardWorker.hpp"
#include "gnuradio/GnuRadioWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

// TODO instead of including and registering blocks manually here, rely on the plugin system
#include <gnuradio-4.0/basic/common_blocks.hpp>
#include <gnuradio-4.0/basic/function_generator.h>
#include <gnuradio-4.0/basic/selector.hpp>

#ifndef __EMSCRIPTEN__
#include <Picoscope4000a.hpp>
#endif

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
            auto &tag = this->output_tags()[0];
            tag       = { 0, { { std::string(gr::tag::SIGNAL_MIN.key()), -0.3f }, { std::string(gr::tag::SIGNAL_MAX.key()), 0.3f } } };
            this->forward_tags();
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
void registerTestBlocks(Registry *registry) {
    registerBuiltinBlocks(registry);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    GP_REGISTER_NODE_RUNTIME(registry, TestSource, double, float);
    GP_REGISTER_NODE_RUNTIME(registry, gr::basic::DataSink, double, float, int16_t);
#ifndef __EMSCRIPTEN__
    GP_REGISTER_NODE_RUNTIME(registry, fair::picoscope::Picoscope4000a, double, float, int16_t);
#endif
#pragma GCC diagnostic pop
}
} // namespace

using namespace opencmw::majordomo;

int main(int argc, char **argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opencmw::majordomo;
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

    Broker broker("PrimaryBroker");
    // REST backend
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<PLAIN_HTTP, decltype(fs)>;
    RestBackend rest(broker, fs, "./");

    const auto  requestedAddress    = URI<>("mds://127.0.0.1:12345");
    const auto  brokerRouterAddress = broker.bind(requestedAddress);
    if (!brokerRouterAddress) {
        fmt::println(std::cerr, "Could not bind to broker address {}", requestedAddress.str());
        return 1;
    }
    std::jthread brokerThread([&broker] {
        broker.run();
    });

    std::jthread restThread([&rest] { rest.run(); });

    opencmw::service::dns::DnsWorkerType dns_worker{broker, opencmw::service::dns::DnsHandler{}};
    std::jthread dnsThread([&dns_worker] {
        dns_worker.run();
    });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    using GrAcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using GrFgWorker  = GnuRadioFlowGraphWorker<GrAcqWorker, "flowgraph", description<"Provides access to the GnuRadio flow graph">>;
    gr::BlockRegistry registry;
    registerTestBlocks(&registry);
    gr::plugin_loader pluginLoader(&registry, {});
    GrAcqWorker       grAcqWorker(broker, std::chrono::milliseconds(50));
    GrFgWorker        grFgWorker(broker, &pluginLoader, { grc, {} }, grAcqWorker);

    std::jthread      grAcqWorkerThread([&grAcqWorker] { grAcqWorker.run(); });
    std::jthread      grFgWorkerThread([&grFgWorker] { grFgWorker.run(); });

    std::this_thread::sleep_for(100ms);

    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY)));
    opencmw::client::ClientContext client{ std::move(clients) };

    // create example signals
    opencmw::service::dns::DnsClient dns_client{client, "http://localhost:8080/dns"};
    dns_client.registerSignals({
        {"http", "localhost", 8080, "service 1", "service"},
        {"http", "localhost", 8080, "service 2", "service"},
        {"http", "localhost", 8080, "service 3", "service"},
        {"http", "localhost", 8080, "service 4", "service"}
    });
    // TODO this subscription needs to match what the UI should receive, because the UI only subscribes to "/GnuRadio/Acquisition"!
    // (query parameters are dropped by RestClient/Backend)
    const auto subscriptions = std::array{ URI("mds://127.0.0.1:12345/GnuRadio/Acquisition?contentType=application%2Fjson&channelNameFilter=test") };
    for (const auto &subscription : subscriptions) {
        fmt::println("Subscribing to {}", subscription.str());
        client.subscribe(subscription, [](const opencmw::mdp::Message &) {});
    }
    fmt::println("WARNING: The UI will receive the subscription(s) above and not the one specified in the UI, as the REST service currently only subscribes to 'GnuRadio/Acquisition'!");
    brokerThread.join();
    restThread.join();

    client.stop();

    dnsThread.join();
    dashboardWorkerThread.join();
    grAcqWorkerThread.join();
    grFgWorkerThread.join();
}
