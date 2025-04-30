#include <ClientCommon.hpp>
#include <daq_api.hpp>
#include <format>

#include <Client.hpp>
#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <opencmw.hpp>
#include <type_traits>

/***
 * A simple test program which allows to subscribe to a specific acquisition property and displays the range and sample count and rate of the received
 * data acquisition Objects.
 *
 * Example use:
 * ``` bash
 * $ ./cli-signal-subscribe mds://localhost:12345/GnuRadio/Acquisition?channelNameFilter=test
 * Subscribing to mds://localhost:12345/GnuRadio/Acquisition?channelNameFilter=test
 * t = 26ms: Update received: 1, samples: 640, min-max: -0.0027466659-0.0025940733, total_samples: 640, avg_sampling_rate: 24615.384615384617
 * t = 76ms: Update received: 2, samples: 640, min-max: -0.0027466659-0.0025940733, total_samples: 1280, avg_sampling_rate: 16842.105263157893
 * [...]
 * $ ./cli-signal-subscribe https://localhost:8080/GnuRadio/Acquisition?channelNameFilter=test&LongPollingIdx=Next # TODO: fix http subscription to not need long polling index
 * Subscribing to mds://localhost:12345/GnuRadio/Acquisition?channelNameFilter=test
 * t = 26ms: Update received: 1, samples: 640, min-max: -0.0027466659-0.0025940733, total_samples: 640, avg_sampling_rate: 24615.384615384617
 * t = 76ms: Update received: 2, samples: 640, min-max: -0.0027466659-0.0025940733, total_samples: 1280, avg_sampling_rate: 16842.105263157893
 * [...]
 * ```
 */
int main(int argc, char** argv) {
    using opencmw::URI;
    using namespace opendigitizer::acq;
    using namespace std::chrono_literals;

    if (argc <= 1) {
        std::print("Please provide subscription URL to AcquisitionWorker.\n");
        return 1;
    }

    const opencmw::zmq::Context                               zctx{};
    std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
    clients.emplace_back(std::make_unique<opencmw::client::MDClientCtx>(zctx, 20ms, ""));
    clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY), opencmw::client::VerifyServerCertificates(false)));
    opencmw::client::ClientContext client{std::move(clients)};

    std::size_t samplesReceived = 0UZ;
    std::size_t signalsReceived = 0UZ;
    std::size_t updateCount     = 0UZ;
    const auto  start           = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());

    std::print("Subscribing to {}\n", argv[1]);

    client.subscribe(opencmw::URI<opencmw::STRICT>(argv[1]), [&samplesReceived, &updateCount, &signalsReceived, &start](const opencmw::mdp::Message& msg) {
        if (!msg.error.empty() || msg.data.empty()) {
            std::print("received error or data is empty, error msg: {}\n", msg.error);
            return;
        }
        const auto                      now    = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
        auto                            uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
        opendigitizer::acq::Acquisition acq{};
        auto                            buf = msg.data;
        try {
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);
        } catch (opencmw::ProtocolException& e) {
            std::print("deserialisation error: {}\n", e.what());
            return;
        }
        auto dataTimestamp = std::chrono::nanoseconds(acq.acqLocalTimeStamp.value());
        auto latency       = (dataTimestamp.count() == 0) ? 0ns : now - dataTimestamp;
        signalsReceived    = acq.channelValues.n(0UZ);
        samplesReceived += acq.channelValues.n(1UZ);
        updateCount++;
        double sample_rate = (static_cast<double>(samplesReceived) / static_cast<double>(uptime.count())) * 1000.0;
        auto [min, max]    = [&acq]() {
            if (acq.channelValues.elements().empty()) {
                return std::ranges::min_max_result{0.0f, 0.0f};
            } else {
                return std::ranges::minmax(acq.channelValues.elements());
            }
        }();
        std::print("t = {}ms: Update received: {}, samples: {}, signals: {}, min-max: {}-{}, total_samples: {}, avg_sampling_rate: {}, latency: {}s\n", //
            uptime.count(), updateCount, acq.channelValues.n(1UZ), acq.channelValues.n(0UZ), min, max, samplesReceived, sample_rate, 1e-9 * static_cast<double>(latency.count()));
    });

    while (true) {
        std::this_thread::sleep_for(1s);
    }

    client.stop();
}
