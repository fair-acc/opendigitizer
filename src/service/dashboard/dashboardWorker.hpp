#include <majordomo/base64pp.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/RestBackend.hpp>
#include <majordomo/Worker.hpp>
#include <URI.hpp>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <string_view>
#include <thread>

CMRC_DECLARE(dashboardFilesystem);

using namespace opencmw::majordomo;
using namespace std::chrono_literals;

struct DashboardFilterContext {
    opencmw::MIME::MimeType contentType = opencmw::MIME::YAML;
};

ENABLE_REFLECTION_FOR(DashboardFilterContext, contentType)

struct Dashboard {
    std::string dashboard;
    std::string flowgraph;
};

ENABLE_REFLECTION_FOR(Dashboard, dashboard, flowgraph)

using namespace opencmw::majordomo;

template<units::basic_fixed_string serviceName, typename... Meta>
class DashboardWorker : public BasicWorker<serviceName, Meta...> {
    std::string header;
    std::string dashboard;
    std::string flowgraph;
    std::mutex  lock;

public:
    using super_t = BasicWorker<serviceName, Meta...>;

    template<typename BrokerType>
    explicit DashboardWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setHandler([this](RequestContext &ctx) {
            if (ctx.request.command() == Command::Get) {
                fmt::print("worker received 'get' request\n");
                auto        uri    = opencmw::URI<opencmw::RELAXED>(std::string(ctx.request.topic()));
                const auto &params = uri.queryParamMap();
                auto        it     = params.find("what");
                std::string what   = it != params.end() && it->second.has_value() ? it->second.value() : std::string{};
                if (what == "dashboard") {
                    ctx.reply.setBody(dashboard, MessageFrame::dynamic_bytes_tag{});
                } else if (what == "flowgraph") {
                    ctx.reply.setBody(flowgraph, MessageFrame::dynamic_bytes_tag{});
                } else {
                    ctx.reply.setBody(header, MessageFrame::dynamic_bytes_tag{});
                }
            } else if (ctx.request.command() == Command::Set) {
            }
        });
        auto fs         = cmrc::dashboardFilesystem::get_filesystem();
        auto headerFile = fs.open("header.yaml");
        header.resize(headerFile.size());
        std::copy(headerFile.begin(), headerFile.end(), header.begin());

        auto dashboardFile = fs.open("dashboard.yaml");
        dashboard.resize(dashboardFile.size());
        std::copy(dashboardFile.begin(), dashboardFile.end(), dashboard.begin());

        auto flowgraphFile = fs.open("dashboard.grc");
        flowgraph.resize(flowgraphFile.size());
        std::copy(flowgraphFile.begin(), flowgraphFile.end(), flowgraph.begin());
    }

private:
};
