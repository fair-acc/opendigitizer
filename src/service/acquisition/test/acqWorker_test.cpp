#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Settings.hpp>
#include <majordomo/Worker.hpp>
#include <IoSerialiserJson.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>

#include <fmt/format.h>
#define __cpp_lib_source_location
#include <boost/ut.hpp>

#include <acqWorker.hpp>
#include <mock_source.hpp>

using opencmw::majordomo::Broker;
using opencmw::majordomo::RestBackend;
using opencmw::majordomo::BrokerMessage;
using opencmw::majordomo::Command;
using opencmw::majordomo::MdpMessage;
using opencmw::majordomo::MessageFrame;
using opencmw::majordomo::Settings;
using opencmw::majordomo::Worker;

using opencmw::majordomo::DEFAULT_REST_PORT;
using opencmw::majordomo::PLAIN_HTTP;

template<typename Mode, typename VirtualFS>
class SimpleTestRestBackend : public opencmw::majordomo::RestBackend<Mode, VirtualFS> {
    using super_t = opencmw::majordomo::RestBackend<Mode, VirtualFS>;

public:
    using super_t::RestBackend;

    static MdpMessage deserializeMessage(std::string_view method, std::string_view serialized) {
        // clang-format off
        auto result = MdpMessage::createClientMessage(
                method == "SUB" ? Command::Subscribe :
                method == "PUT" ? Command::Set :
                /* default */     Command::Get);
        // clang-format on

        // For the time being, just use ';' as frame separator. Not meant
        // to be a safe long-term solution:
        auto       currentBegin = serialized.cbegin();
        const auto bodyEnd      = serialized.cend();
        auto       currentEnd   = std::find(currentBegin, serialized.cend(), ';');

        for (std::size_t i = 2; i < result.requiredFrameCount(); ++i) {
            result.setFrameData(i, std::string_view(currentBegin, currentEnd), MessageFrame::dynamic_bytes_tag{});
            currentBegin = (currentEnd != bodyEnd) ? currentEnd + 1 : bodyEnd;
            currentEnd   = std::find(currentBegin, serialized.cend(), ';');
        }
        return result;
    }

    void registerHandlers() override {
        super_t::registerHandlers();
    }
};

std::jthread makeGetRequestResponseCheckerThread(const std::string &address, const std::string &requiredResponse, [[maybe_unused]] boost::ut::reflection::source_location location = boost::ut::reflection::source_location::current()) {
    return std::jthread([&address, &requiredResponse, &location]() {
        using namespace boost::ut;
        httplib::Client http("localhost", DEFAULT_REST_PORT);
        http.set_keep_alive(true);
        const auto response = http.Get(address);

        expect(&response, location);
        fmt::print("msg: {}", response->body);
        expect(response->status == 200);
        expect(response->body.find(requiredResponse) != std::string::npos);
    });
}

const boost::ut::suite basic_acq_worker_tests = [] {
    using namespace boost::ut;

    "GetDataTest"_test = [] {
        using opencmw::URI;
        using namespace opendigitizer::acq;
        using namespace opencmw::majordomo;
        // broker
        Broker broker("PrimaryBroker");
        // REST backend
        auto fs           = cmrc::assets::get_filesystem();
        using RestBackend = SimpleTestRestBackend<PLAIN_HTTP, decltype(fs)>;
        RestBackend rest(broker, fs);

        const auto  brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
        if (!brokerRouterAddress) {
            std::cerr << "Could not bind to broker address" << std::endl;
            return 1;
        }
        std::jthread brokerThread([&broker] {
            broker.run();
        });

        // acquisition worker (mock) todo: implement
        using AcqWorker = AcquisitionWorker<"acquisition", description<"Provides data acquisition updates">>;
        std::vector<AcqWorker::sink_buffer> sinks = {{"sample-sine", "V", AcqWorker::streambuffer{AcqWorker::RING_BUFFER_SIZE}, AcqWorker::tagbuffer{AcqWorker::RING_BUFFER_SIZE}}};
        AcqWorker    acquisitionWorker(broker, sinks); // todo: change to 25Hz, just slow for debugging
        std::jthread acquisitionWorkerThread([&acquisitionWorker] { acquisitionWorker.run(); });

        // mock publisher, which publishes sine waves to the available sinks // todo: put into separate class
        auto src = opendigitizer::acq::mock_source<AcqWorker>{sinks};
        std::jthread source{[&src](std::stop_token stoken) {return src(stoken);}};

        // wait for startup
        std::this_thread::sleep_for(2s); // todo: replace with explicit wait for availability function

        // check rest reply
        const std::string addr = "acquisition?channelNameFilter=sample-sine";
        const std::string expected = "expected";
        auto checker = makeGetRequestResponseCheckerThread(addr, expected, boost::ut::reflection::source_location::current());
        checker.join(); // wait for the reply

        // shutdown
        broker.shutdown();
        source.request_stop();
        brokerThread.join();
        source.join();
        // workers terminate when broker shuts down
        acquisitionWorkerThread.join();
    };

};

int main() { /* not needed for ut */ }

//// We run both broker and worker inproc
//Broker                                          broker("TestBroker");
//auto                                            fs = cmrc::assets::get_filesystem();
//SimpleTestRestBackend<PLAIN_HTTP, decltype(fs)> rest(broker, fs);
//
//// The worker uses the same settings for matching, but as it knows about TimeDomainContext, it does this registration automatically.
//opencmw::query::registerTypes(TimeDomainContext(), broker);
//
//TimeDomainWorker<"test.service", description<"Time-Domain Worker">> timeDomainWorker(broker);
//
//// Run worker and broker in separate threads
//RunInThread brokerRun(broker);
//RunInThread workerRun(timeDomainWorker);
//
//REQUIRE(waitUntilServiceAvailable(broker.context, "test.service"));
//
//httplib::Client http("localhost", DEFAULT_REST_PORT);
//http.set_keep_alive(true);
//
//const char *path = "test.service/Acquisition?channelNameFilter=testSignal@20000Hz";
//// httplib::Headers headers({ { "X-OPENCMW-METHOD", "POLL" } });
//auto response = http.Get(path);
//for (size_t i = 0; i < 100; i++) {
//response = http.Get(path);
//if (response.error() == httplib::Error::Success) {
//break;
//}
//std::this_thread::sleep_for(std::chrono::milliseconds(10));
//}
//
//REQUIRE(response.error() == httplib::Error::Success);
//
//REQUIRE(response->status == 200);
//
//REQUIRE(response->body.find("refTriggerName") != std::string::npos);
//REQUIRE(response->body.find("refTriggerStamp") != std::string::npos);
//REQUIRE(response->body.find("channelTimeSinceRefTrigger") != std::string::npos);
//REQUIRE(response->body.find("channelUserDelay") != std::string::npos);
//REQUIRE(response->body.find("channelActualDelay") != std::string::npos);
//REQUIRE(response->body.find("channelNames") != std::string::npos);
//REQUIRE(response->body.find("channelValues") != std::string::npos);
//REQUIRE(response->body.find("channelErrors") != std::string::npos);
//REQUIRE(response->body.find("channelUnits") != std::string::npos);
//REQUIRE(response->body.find("status") != std::string::npos);
//REQUIRE(response->body.find("channelRangeMin") != std::string::npos);
//REQUIRE(response->body.find("channelRangeMax") != std::string::npos);
//REQUIRE(response->body.find("temperature") != std::string::npos);
//
//{
//opencmw::IoBuffer buffer;
//buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);
//Acquisition data;
//auto        result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, data);
//fmt::print("deserialisation finished: {}\n", result);
//REQUIRE(data.refTriggerName == "NO_REF_TRIGGER");
//REQUIRE(data.refTriggerStamp == 0);
//REQUIRE(data.channelTimeSinceRefTrigger.size() == 0);
//REQUIRE(data.channelUserDelay == 0.0F);
//REQUIRE(data.channelActualDelay == 0.0F);
//REQUIRE(data.channelNames.size() == 0);
//REQUIRE(data.channelValues.n(0) == 0);
//REQUIRE(data.channelValues.n(1) == 0);
//REQUIRE(data.channelErrors.n(0) == 0);
//REQUIRE(data.channelErrors.n(1) == 0);
//REQUIRE(data.channelUnits.size() == 0);
//REQUIRE(data.status.size() == 0);
//REQUIRE(data.channelRangeMin.size() == 0);
//REQUIRE(data.channelRangeMax.size() == 0);
//REQUIRE(data.temperature.size() == 0);
//}
//}
//
//TEST_CASE("gr-opencmw_time_sink", "[daq_api][time-domain][opencmw_time_sink]") {
//// top block
//auto top = gr::make_top_block("GNURadio");
//
//// gnuradio blocks
//// saw_tooth_signal --> throttle --> opencmw_time_sink
//const double      SAMPLING_RATE = 200'000.0;
//const double      SAW_AMPLITUDE = 1.0;
//const double      SAW_FREQUENCY = 50.0;
//const std::string signalName{ "saw" };
//const std::string signalUnit{ "unit" };
//auto              saw_signal_source         = gr::analog::sig_source_f::make(SAMPLING_RATE, gr::analog::GR_SAW_WAVE, SAW_FREQUENCY, SAW_AMPLITUDE, 0, 0);
//auto              throttle_block            = gr::blocks::throttle::make(sizeof(float) * 1, SAMPLING_RATE, true);
//auto              pulsed_power_opencmw_sink = gr::pulsed_power::opencmw_time_sink::make({ signalName }, { signalUnit }, SAMPLING_RATE);
//pulsed_power_opencmw_sink->set_max_noutput_items(640);
//
//// connections
//top->hier_block2::connect(saw_signal_source, 0, throttle_block, 0);
//top->hier_block2::connect(throttle_block, 0, pulsed_power_opencmw_sink, 0);
//
//// start gnuradio flowgraph
//top->start();
//
//// We run both broker and worker inproc
//Broker                                          broker("TestBroker");
//auto                                            fs = cmrc::assets::get_filesystem();
//SimpleTestRestBackend<PLAIN_HTTP, decltype(fs)> rest(broker, fs);
//
//// The worker uses the same settings for matching, but as it knows about TimeDomainContext, it does this registration automatically.
//opencmw::query::registerTypes(TimeDomainContext(), broker);
//
//TimeDomainWorker<"test.service", description<"Time-Domain Worker">> timeDomainWorker(broker);
//
//// Run worker and broker in separate threads
//RunInThread brokerRun(broker);
//RunInThread workerRun(timeDomainWorker);
//
//REQUIRE(waitUntilServiceAvailable(broker.context, "test.service"));
//
//httplib::Client http("localhost", DEFAULT_REST_PORT);
//http.set_keep_alive(true);
//
//const char      *path = "test.service/Acquisition?channelNameFilter=saw@200000Hz";
//httplib::Headers headers({ { "X-OPENCMW-METHOD", "POLL" } });
//auto             response = http.Get(path, headers);
//// auto response = http.Get(path);
//for (size_t i = 0; i < 100; i++) {
//// response = http.Get(path);
//response = http.Get(path, headers);
//if (response.error() == httplib::Error::Success) {
//if (response->status == 200) {
//break;
//}
//}
//std::this_thread::sleep_for(std::chrono::milliseconds(10));
//}
//
//REQUIRE(response.error() == httplib::Error::Success);
//
//REQUIRE(response->status == 200);
//
//{
//opencmw::IoBuffer buffer;
//buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);
//Acquisition data;
//auto        result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, data);
//fmt::print("deserialisation finished: {}\n", result);
//REQUIRE(data.refTriggerStamp > 0);
//REQUIRE(data.channelTimeSinceRefTrigger.size() == data.channelValues.n(1));
//REQUIRE(data.channelNames.size() == data.channelValues.n(0));
//REQUIRE(data.channelNames[0] == fmt::format("{}@{}Hz", signalName, SAMPLING_RATE));
//
//// check if it is actually sawtooth signal
//for (uint32_t i = 0; i < data.channelValues.n(1) - 1; ++i) {
//Approx saw_signal_slope = Approx(SAW_AMPLITUDE / SAMPLING_RATE * SAW_FREQUENCY).epsilon(0.01); // 1% difference
//Approx saw_timebase     = Approx(1 / SAMPLING_RATE).epsilon(0.01);                             // 1% difference
//Approx saw_amplitude    = Approx(SAW_AMPLITUDE).epsilon(0.01);                                 // 1% difference
//if (data.channelValues[i + 1] > data.channelValues[i]) {
//REQUIRE(saw_signal_slope == (data.channelValues[i + 1] - data.channelValues[i]));
//} else {
//REQUIRE(data.channelValues[i] == saw_amplitude);
//}
//REQUIRE(saw_timebase == (data.channelTimeSinceRefTrigger[i + 1] - data.channelTimeSinceRefTrigger[i]));
//}
//}
//
//top->stop();
//}
//
//TEST_CASE("request_multiple_chunks_from_time_domain_worker", "[daq_api][time-domain][opencmw_time_sink]") {
//// top block
//auto top = gr::make_top_block("GNURadio");
//
//// gnuradio blocks
//// saw_tooth_signal --> throttle --> opencmw_time_sink
//const double      SAMPLING_RATE = 200'000.0;
//const double      SAW_AMPLITUDE = 1.0;
//const double      SAW_FREQUENCY = 50.0;
//const std::string signalName{ "saw" };
//const std::string signalUnit{ "unit" };
//auto              saw_signal_source         = gr::analog::sig_source_f::make(SAMPLING_RATE, gr::analog::GR_SAW_WAVE, SAW_FREQUENCY, SAW_AMPLITUDE, 0, 0);
//auto              throttle_block            = gr::blocks::throttle::make(sizeof(float) * 1, SAMPLING_RATE, true);
//auto              pulsed_power_opencmw_sink = gr::pulsed_power::opencmw_time_sink::make({ signalName }, { signalUnit }, SAMPLING_RATE);
//pulsed_power_opencmw_sink->set_max_noutput_items(640);
//
//// connections
//top->hier_block2::connect(saw_signal_source, 0, throttle_block, 0);
//top->hier_block2::connect(throttle_block, 0, pulsed_power_opencmw_sink, 0);
//
//// start gnuradio flowgraph
//top->start();
//
//// We run both broker and worker inproc
//Broker                                          broker("TestBroker");
//auto                                            fs = cmrc::assets::get_filesystem();
//SimpleTestRestBackend<PLAIN_HTTP, decltype(fs)> rest(broker, fs);
//
//// The worker uses the same settings for matching, but as it knows about TimeDomainContext, it does this registration automatically.
//opencmw::query::registerTypes(TimeDomainContext(), broker);
//
//TimeDomainWorker<"test.service", description<"Time-Domain Worker">> timeDomainWorker(broker);
//
//// Run worker and broker in separate threads
//RunInThread brokerRun(broker);
//RunInThread workerRun(timeDomainWorker);
//
//REQUIRE(waitUntilServiceAvailable(broker.context, "test.service"));
//
//httplib::Client http("localhost", DEFAULT_REST_PORT);
//http.set_keep_alive(true);
//
//std::string path = "test.service/Acquisition?channelNameFilter=saw@200000Hz&lastRefTrigger=0";
//// httplib::Headers headers({ { "X-OPENCMW-METHOD", "GET" } });
//uint64_t previousRefTrigger       = 0;
//uint64_t lastTimeStamp            = 0;
//uint64_t firstTimeStampOfNewChunk = 0;
//for (int iChunk = 0; iChunk < 100; iChunk++) {
//auto response = http.Get(path.c_str());
//for (size_t i = 0; i < 100; i++) {
//response = http.Get(path);
//if (response.error() == httplib::Error::Success && response->status == 200) {
//break;
//}
//std::this_thread::sleep_for(std::chrono::milliseconds(10));
//}
//
//REQUIRE(response.error() == httplib::Error::Success);
//REQUIRE(response->status == 200);
//
//opencmw::IoBuffer buffer;
//buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>(response->body);
//Acquisition data;
//auto        result = opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buffer, data);
//fmt::print("deserialisation finished: {}\n", result);
//// REQUIRE(data.refTriggerStamp > 0);
//
//// check for non-empty data
//if (data.refTriggerStamp == 0) {
//continue;
//}
//
//REQUIRE(data.channelTimeSinceRefTrigger.size() == data.channelValues.n(1));
//REQUIRE(data.channelNames.size() == data.channelValues.n(0));
//REQUIRE(data.channelNames[0] == fmt::format("{}@{}Hz", signalName, SAMPLING_RATE));
//
//// check if it is actually sawtooth signal
//for (uint32_t i = 0; i < data.channelValues.n(1) - 1; ++i) {
//Approx saw_signal_slope = Approx(SAW_AMPLITUDE / SAMPLING_RATE * SAW_FREQUENCY).epsilon(0.01); // 1% difference
//Approx saw_timebase     = Approx(1 / SAMPLING_RATE).epsilon(0.01);                             // 1% difference
//Approx saw_amplitude    = Approx(SAW_AMPLITUDE).epsilon(0.01);                                 // 1% difference
//if (data.channelValues[i + 1] > data.channelValues[i]) {
//REQUIRE(saw_signal_slope == (data.channelValues[i + 1] - data.channelValues[i]));
//} else {
//REQUIRE(data.channelValues[i] == saw_amplitude);
//}
//REQUIRE(saw_timebase == (data.channelTimeSinceRefTrigger[i + 1] - data.channelTimeSinceRefTrigger[i]));
//}
//
//// Check time-continuity between chunks
//if (previousRefTrigger != 0) {
//Approx calculatedRefTriggerStamp = Approx(static_cast<double>(lastTimeStamp) / 1.0 + SAMPLING_RATE).epsilon(1e-10);
//firstTimeStampOfNewChunk         = data.refTriggerStamp + data.channelTimeSinceRefTrigger[0] * 1e9;
//REQUIRE(firstTimeStampOfNewChunk == calculatedRefTriggerStamp);
//REQUIRE(firstTimeStampOfNewChunk > lastTimeStamp);
//}
//
//previousRefTrigger = data.refTriggerStamp;
//lastTimeStamp      = data.refTriggerStamp + data.channelTimeSinceRefTrigger.back() * 1e9;
//path               = fmt::format("test.service/Acquisition?channelNameFilter=saw@200000Hz&lastRefTrigger={}", lastTimeStamp);
//}
//
//top->stop();
//}
//