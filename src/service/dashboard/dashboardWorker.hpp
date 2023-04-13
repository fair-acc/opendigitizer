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
            auto whatParam = [&]() {
                auto        uri    = opencmw::URI<opencmw::RELAXED>(std::string(ctx.request.topic()));
                const auto &params = uri.queryParamMap();
                auto        it     = params.find("what");
                return it != params.end() && it->second.has_value() ? it->second.value() : std::string{};
            };
            if (ctx.request.command() == Command::Get) {
                fmt::print("worker received 'get' request\n");
                std::string what   = whatParam();
                if (what == "dashboard") {
                    ctx.reply.setBody(dashboard, MessageFrame::dynamic_bytes_tag{});
                } else if (what == "flowgraph") {
                    ctx.reply.setBody(flowgraph, MessageFrame::dynamic_bytes_tag{});
                } else {
                    ctx.reply.setBody(header, MessageFrame::dynamic_bytes_tag{});
                }
            } else if (ctx.request.command() == Command::Set) {
                std::string what   = whatParam();
                auto body = ctx.request.body();
                // The first 4 bytes contain the size of the string, including the terminating null byte
                int32_t size;
                memcpy(&size, body.data(), 4);
                std::string data = std::string(body.data() + 4, std::size_t(size));

                if (what == "dashboard") {
                    dashboard = std::move(data);
                    ctx.reply.setBody(dashboard, MessageFrame::dynamic_bytes_tag{});
                } else if (what == "flowgraph") {
                    flowgraph = std::move(data);
                    ctx.reply.setBody(flowgraph, MessageFrame::dynamic_bytes_tag{});
                } else {
                    header = std::move(data);
                    ctx.reply.setBody(header, MessageFrame::dynamic_bytes_tag{});
                }
            }
        });
        auto fs         = cmrc::dashboardFilesystem::get_filesystem();
        auto     file       = fs.open("dashboard.ddd");

        uint32_t hstart, hsize;
        uint32_t dstart, dsize;
        uint32_t fstart, fsize;

        memcpy(&hstart, file.begin(), 4);
        memcpy(&hsize, file.begin() + 4, 4);

        memcpy(&dstart, file.begin() + 8, 4);
        memcpy(&dsize, file.begin() + 12, 4);

        memcpy(&fstart, file.begin() + 16, 4);
        memcpy(&fsize, file.begin() + 20, 4);

        header.resize(hsize);
        memcpy(header.data(), file.begin() + hstart, hsize);

        dashboard.resize(dsize);
        memcpy(dashboard.data(), file.begin() + dstart, dsize);

        flowgraph.resize(fsize);
        memcpy(flowgraph.data(), file.begin() + fstart, fsize);
    }

private:
};
