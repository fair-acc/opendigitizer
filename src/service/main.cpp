#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>

#include <fstream>
#include <iomanip>
#include <thread>

//#include "flowgraph/flowgraphWorker.hpp"
//#include "acquisition/acqWorker.hpp"
#include "rest/fileserverRestBackend.hpp"

using namespace opencmw::majordomo;

struct TestContext {
    opencmw::TimingCtx      ctx;
    std::string             testFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

// TODO using unsupported types throws in the mustache serialiser, the exception isn't properly handled,
// the browser just shows a bit of gibberish instead of the error message.

ENABLE_REFLECTION_FOR(TestContext, ctx, testFilter, contentType)

struct Request {
    std::string             name;
    opencmw::TimingCtx      timingCtx;
    std::string             customFilter;
    opencmw::MIME::MimeType contentType = opencmw::MIME::BINARY;
};

ENABLE_REFLECTION_FOR(Request, name, timingCtx, customFilter /*, contentType*/)

struct Reply {
    // TODO java demonstrates custom enums here - we don't support that, but also the example doesn't need it
    /*
    enum class Option {
        REPLY_OPTION1,
        REPLY_OPTION2,
        REPLY_OPTION3,
        REPLY_OPTION4
    };
*/
    std::string        name;
    bool               booleanReturnType;
    int8_t             byteReturnType;
    int16_t            shortReturnType;
    int32_t            intReturnType;
    int64_t            longReturnType;
    std::string        byteArray;
    opencmw::TimingCtx timingCtx;
    std::string        lsaContext;
    // Option replyOption = Option::REPLY_OPTION2;
};

ENABLE_REFLECTION_FOR(Reply, name, booleanReturnType, byteReturnType, shortReturnType, intReturnType, longReturnType, timingCtx, lsaContext /*, replyOption*/)

struct HelloWorldHandler {
    std::string customFilter = "uninitialised";

    void        operator()(RequestContext &rawCtx, const TestContext &requestContext, const Request &in, TestContext &replyContext, Reply &out) {
        using namespace std::chrono;
        const auto now        = system_clock::now();
        const auto sinceEpoch = system_clock::to_time_t(now);
        out.name              = fmt::format("Hello World! The local time is: {}", std::put_time(std::localtime(&sinceEpoch), "%Y-%m-%d %H:%M:%S"));
        out.byteArray         = in.name; // doesn't really make sense atm
        out.byteReturnType    = 42;

        out.timingCtx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        if (rawCtx.request.command() == Command::Set) {
            customFilter = in.customFilter;
        }
        out.lsaContext           = customFilter;

        replyContext.ctx         = out.timingCtx;
        replyContext.ctx         = opencmw::TimingCtx(3, {}, {}, {}, duration_cast<microseconds>(now.time_since_epoch()));
        replyContext.contentType = requestContext.contentType;
        replyContext.testFilter  = fmt::format("HelloWorld - reply topic = {}", requestContext.testFilter);
    }
};

CMRC_DECLARE(assets_static);

int main() {
    using opencmw::URI;
    // broker
    Broker broker("PrimaryBroker");
    // REST backend
    // auto fs           = cmrc::assets_opencmw::get_filesystem();
    auto fs           = cmrc::assets::get_filesystem();
    using RestBackend = FileServerRestBackend<PLAIN_HTTP, decltype(fs)>;
    RestBackend rest(broker, fs, "./");

    const auto  brokerRouterAddress = broker.bind(URI<>("mds://127.0.0.1:12345"));
    if (!brokerRouterAddress) {
        std::cerr << "Could not bind to broker address" << std::endl;
        return 1;
    }
    std::jthread brokerThread([&broker] {
        broker.run();
    });

    // hello world worker
    Worker<"helloWorld", TestContext, Request, Reply, description<"A friendly service saying hello">> helloWorldWorker(broker, HelloWorldHandler());
    std::jthread                                                                                      helloWorldThread([&helloWorldWorker] {
        helloWorldWorker.run();
                                                                                         });

    // flowgraph worker (mock)
    // FlowgraphWorker<"flowgraph", description<"Provides R/W access to the flowgraph as a yaml serialized string">> flowgraphWorker(broker);
    // std::jthread flowgraphWorkerThread([&flowgraphWorker] {
    //    flowgraphWorker.run();
    //});

    // acquisition worker (mock) todo: implement
    // AcquisitionWorker<"acquisition", description<"Provides data acquisition updates">> acquisitionWorker(broker);
    // std::jthread acquisitionWorkerThread([&acquisitionWorker] {
    //     acquisitionWorker.run();
    // });

    // shutdown
    brokerThread.join();
    // workers terminate when broker shuts down
    // flowgraphWorkerThread.join();
    // acquisitionWorkerThread.join();
    helloWorldThread.join();
}