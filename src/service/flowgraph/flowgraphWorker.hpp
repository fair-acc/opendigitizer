#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <string_view>
#include <thread>

CMRC_DECLARE(flowgraphFilesystem);

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct FilterContext {
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};
ENABLE_REFLECTION_FOR(FilterContext, contentType)

struct Flowgraph {
    std::string flowgraph;
};

ENABLE_REFLECTION_FOR(Flowgraph, flowgraph)

using namespace opencmw::majordomo;

template<units::basic_fixed_string serviceName, typename... Meta>
class FlowgraphWorker : public Worker<serviceName, FilterContext, Flowgraph, Flowgraph, Meta...> {
    std::string flowgraph;
    std::mutex  flowgraphLock;

public:
    using super_t = Worker<serviceName, FilterContext, Flowgraph, Flowgraph, Meta...>;

    template<typename BrokerType>
    explicit FlowgraphWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setCallback([this](const RequestContext &rawCtx, const FilterContext &filterIn, const Flowgraph &in, FilterContext &filterOut, Flowgraph &out) {
            if (rawCtx.request.command() == Command::Get) {
                fmt::print("worker received 'get' request\n");
                handleGetRequest(filterIn, filterOut, out);
            } else if (rawCtx.request.command() == Command::Set) {
                fmt::print("worker received 'set' request\n");
                handleSetRequest(filterIn, filterOut, in, out);
            }
        });
        auto fs                   = cmrc::flowgraphFilesystem::get_filesystem();
        auto defaultFlowgraphFile = fs.open("flowgraph.grc");
        flowgraph.resize(defaultFlowgraphFile.size());
        std::copy(defaultFlowgraphFile.begin(), defaultFlowgraphFile.end(), flowgraph.begin());
    }

private:
    void handleGetRequest(const FilterContext & /*filterIn*/, FilterContext & /*filterOut*/, Flowgraph &out) {
        fmt::print("handleGetRequest for flowgraph\n");
        std::lock_guard lockGuard(flowgraphLock);
        out.flowgraph = flowgraph;
    }

    void handleSetRequest(const FilterContext & /*filterIn*/, FilterContext & /*filterOut*/, const Flowgraph &in, Flowgraph &out) {
        fmt::print("handleSetRequest for flowgraph\n");
        std::lock_guard lockGuard(flowgraphLock);
        flowgraph     = in.flowgraph;
        out.flowgraph = flowgraph;
        notifyUpdate();
    }

    void notifyUpdate() {
        for (auto subTopic : super_t::activeSubscriptions()) { // loop over active subscriptions
            const auto          queryMap  = subTopic.queryParamMap();
            const FilterContext filterIn  = opencmw::query::deserialise<FilterContext>(queryMap);
            FilterContext       filterOut = filterIn;
            Flowgraph           subscriptionReply;
            try {
                handleGetRequest(filterIn, filterOut, subscriptionReply);
                super_t::notify(std::string(serviceName.c_str()), filterOut, subscriptionReply);
            } catch (const std::exception &ex) {
                fmt::print("caught specific exception '{}'\n", ex.what());
            } catch (...) {
                fmt::print("caught unknown generic exception\n");
            }
        }
    }
};
