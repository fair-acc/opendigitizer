#include <Client.hpp>
#include <RestDefaultClientCertificates.hpp>
#include <cmrc/cmrc.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/LoadTestWorker.hpp>
#include <majordomo/Rest.hpp>
#include <majordomo/Worker.hpp>
#include <services/dns.hpp>
#include <services/dns_client.hpp>
#include <zmq/ZmqUtils.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <thread>

#include <gnuradio-4.0/Export.hpp>

#include <GrBasicBlocks.hpp>
#include <GrElectricalBlocks.hpp>
#include <GrFileIoBlocks.hpp>
#include <GrFilterBlocks.hpp>
#include <GrFourierBlocks.hpp>
#include <GrHttpBlocks.hpp>
#include <GrMathBlocks.hpp>
#include <GrTestingBlocks.hpp>

#include "FAIR/DeviceNameHelper.hpp"
#include "dashboard/dashboardWorker.hpp"
#include "gnuradio/GnuRadioAcquisitionWorker.hpp"
#include "gnuradio/GnuRadioFlowgraphWorker.hpp"

#include <version.hpp>

// TODO instead of including and registering blocks manually here, rely on the plugin system
#include "build_configuration.hpp"
#include "settings.hpp"

#include <fair/picoscope/Picoscope.hpp>
#include <fair/picoscope/Picoscope3000a.hpp>
#include <fair/picoscope/Picoscope4000a.hpp>
#include <fair/picoscope/Picoscope5000a.hpp>
#include <fair/picoscope/Picoscope6000.hpp>
#include <fair/timing/TimingSource.hpp>

namespace {
template<typename Registry>
void registerTestBlocks(Registry& registry) {
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrElectricalBlocks(registry);
    gr::blocklib::initGrFileIoBlocks(registry);
    gr::blocklib::initGrFilterBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrHttpBlocks(registry);
    gr::blocklib::initGrMathBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    // TODO: make gr-digitizers a proper OOT module
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope3000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope4000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope5000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope6000>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope3000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope4000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope5000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope6000>, "">(registry);
    gr::registerBlock<gr::timing::TimingSource, "">(registry);
}
} // namespace

using namespace opencmw::majordomo;

void registerDefaultThreadPool() {
    using namespace gr::thread_pool;
    Manager::instance().replacePool(std::string(kDefaultCpuPoolId), std::make_shared<ThreadPoolWrapper>(std::make_unique<BasicThreadPool>(std::string(kDefaultCpuPoolId), TaskType::CPU_BOUND, 2U, 2U), "CPU"));
}

std::unique_ptr<Broker<>> setupBroker(const Digitizer::Settings& settings) {
    auto broker = std::make_unique<Broker<>>("/PrimaryBroker");

    const auto wasmServeDir = !settings.wasmServeDir.empty() ? settings.wasmServeDir : SERVING_DIR;

    auto getEnvironmentVariable = [](const char* name, std::string_view defaultValue) {
        if (const auto value = std::getenv(name); value) {
            return std::string(value);
        }
        return std::string(defaultValue);
    };

    bool           httpInitialized  = false;
    bool           httpsInitialized = false;
    rest::Settings restSettings;
    for (auto& bindAddress : settings.bindAddresses) {
        opencmw::URI<> bindUrl{bindAddress};
        if (bindAddress.starts_with("mdp://") || bindAddress.starts_with("mds://") || bindAddress.starts_with("tcp://") || bindAddress.starts_with("inproc://")) {
            const auto brokerRouterAddress = broker->bind(bindUrl);
            if (!brokerRouterAddress) {
                std::println(std::cerr, "Could not bind to broker address {}", bindAddress);
            }
            continue;
        }
        if (bindAddress.starts_with("http://") || bindAddress.starts_with("https://")) {
            if (!httpInitialized) {
                std::vector<std::pair<std::string, std::string>> extraHeaders = {
                    {"cross-origin-opener-policy", "same-origin"},
                    {"cross-origin-embedder-policy", "require-corp"},
                    {"cache-control", "public, max-age=3600"},
                };

                auto       cmrcFs   = std::make_shared<cmrc::embedded_filesystem>(cmrc::assets::get_filesystem());
                const auto mainPath = std::format("web/index.html#dashboard={}{}", settings.defaultDashboard, settings.darkMode ? "&darkMode=true" : "");

                auto redirectHandler = [](auto from, auto to) {
                    return rest::Handler{.method = "GET", .path = from, .handler = [to](const rest::Request&) {
                                             rest::Response response;
                                             response.headers = {{"location", to}};
                                             response.code    = 302;
                                             return response;
                                         }};
                };
                restSettings.handlers = {
                    rest::cmrcHandler("/assets/*", "", cmrcFs, ""),                     //
                    rest::fileSystemHandler("/web/*", "/", wasmServeDir, extraHeaders), //
                    redirectHandler("/", mainPath),                                     //
                    redirectHandler("/index.html", mainPath)                            //
                };
                httpInitialized = true;
            }
        }
        if (bindAddress.starts_with("https://")) {
            if (!httpsInitialized) {
                restSettings.certificateFilePath = getEnvironmentVariable("OPENCMW_REST_CERT_FILE", "demo_public.crt");
                restSettings.keyFilePath         = getEnvironmentVariable("OPENCMW_REST_PRIVATE_KEY_FILE", "demo_private.key");
                std::println(std::cout, "Using certificate file: {}", restSettings.certificateFilePath.string());
                std::println(std::cout, "Using private key file: {}", restSettings.keyFilePath.string());
                httpsInitialized = true;
            }
            restSettings.port = bindUrl.port().value();
        }
    }
    if (httpInitialized) {
        if (auto rc = broker->bindRest(restSettings); !rc) {
            std::println(std::cerr, "Could not bind REST bridge: {}", rc.error());
        }
    }

    return broker;
};

int main(int argc, char** argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace opendigitizer::gnuradio;
    using namespace opencmw::majordomo;
    using namespace opencmw::service;
    using namespace std::chrono_literals;

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        std::println("opendigitizer [--enable-load-test] [<path to flowgraph>]");
        std::println("    launch opendigitizer with the provided flow graph or a default flowgraph if omitted");
        std::println("opendigitizer --list-registered-blocks");
        std::println("    list all blocks that are registered in the service");
        std::println("opendigitizer --version");
        std::println("    print version of the opendigitizer");
        std::println("opendigitizer --help");
        std::println("    show this help message");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--list-registered-blocks") == 0) {
        gr::BlockRegistry registry;
        registerTestBlocks(registry);
        std::print("Available blocks:\n");
        for (auto& blockName : registry.keys()) {
            std::print("  - {}\n", blockName);
        }
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        std::println("{}", kOpendigitizerVersion);
        return 0;
    }

    int  argi     = 1;
    bool loadTest = false;
    if (argc > 1 && strcmp(argv[1], "--enable-load-test-worker") == 0) {
        loadTest = true;
        argi++;
    }

    std::string grc = R"(blocks:
  - id: gr::basic::ClockSource
    parameters:
      name: ClockSource1
      n_samples_max: 0
  - id: gr::basic::SignalGenerator<float32>
    parameters:
      name: SignalGenerator1
      frequency: 1
      amplitude: 5
      sample_rate: 4096
      signal_type: Sin
  - id: gr::basic::DataSink<float32>
    parameters:
      name: Sink
      signal_name: test
connections:
  - [ClockSource1, 0, SignalGenerator1, 0]
  - [SignalGenerator1, 0, Sink, 0]
)";
    if (argc > argi) {
        std::ifstream     in(argv[argi]);
        std::stringstream grcBuffer;
        if (!(grcBuffer << in.rdbuf())) {
            std::println(stderr, "Could not read GRC file: {}", strerror(errno));
            return 1;
        }
        grc = grcBuffer.str();
    }

    Digitizer::Settings& settings = Digitizer::Settings::instance();

    registerDefaultThreadPool();
    auto broker = setupBroker(settings);
    if (broker == nullptr) {
        return 1;
    }
    std::jthread brokerThread([&broker] { broker->run(); });

    dns::DnsWorkerType dns_worker{*broker, dns::DnsHandler{}};
    std::jthread       dnsThread([&dns_worker] { dns_worker.run(); });

    // dashboard worker (mock)
    using DsWorker = DashboardWorker<"/dashboards", description<"Provides R/W access to the dashboard as a yaml serialized string">>;
    DsWorker     dashboardWorker(*broker);
    std::jthread dashboardWorkerThread([&dashboardWorker] { dashboardWorker.run(); });

    using GrAcqWorker = GnuRadioAcquisitionWorker<"/GnuRadio/Acquisition", description<"Provides data from a GnuRadio flow graph execution">>;
    using GrFgWorker  = GnuRadioFlowGraphWorker<GrAcqWorker, "/flowgraph", description<"Provides access to the GnuRadio flow graph">>;
    gr::BlockRegistry registry;
    registerTestBlocks(registry);
    gr::PluginLoader                                       pluginLoader(registry, gr::globalSchedulerRegistry(), {});
    GrAcqWorker                                            grAcqWorker(*broker, &pluginLoader, 50ms);
    GrFgWorker                                             grFgWorker(*broker, &pluginLoader, opendigitizer::flowgraph::Flowgraph{grc, {}}, grAcqWorker);
    std::optional<opencmw::majordomo::load_test::Worker<>> loadTestWorker{};
    if (loadTest) {
        loadTestWorker.emplace(*broker);
    }

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

    std::jthread                grAcqWorkerThread([&grAcqWorker] { grAcqWorker.run(); });
    std::jthread                grFgWorkerThread([&grFgWorker] { grFgWorker.run(); });
    std::optional<std::jthread> loadTestWorkerThread{};
    if (loadTestWorker && loadTest) {
        loadTestWorkerThread.emplace([&loadTestWorker] { loadTestWorker->run(); });
    }

    brokerThread.join();
    client.stop();
    dnsThread.join();
    dashboardWorkerThread.join();
    grAcqWorkerThread.join();
    grFgWorkerThread.join();
    if (loadTestWorkerThread) {
        loadTestWorkerThread->join();
    }
}
