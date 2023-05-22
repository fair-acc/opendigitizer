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

struct Dashboard {
    std::string header;
    std::string dashboard;
    std::string flowgraph;
};

using namespace opencmw::majordomo;

template<units::basic_fixed_string serviceName, typename... Meta>
class DashboardWorker : public BasicWorker<serviceName, Meta...> {
    std::vector<std::string> names;
    std::vector<Dashboard>   dashboards;
    std::mutex               lock;

public:
    using super_t = BasicWorker<serviceName, Meta...>;

    template<typename BrokerType>
    explicit DashboardWorker(const BrokerType &broker)
        : super_t(broker, {}) {
        super_t::setHandler([this](RequestContext &ctx) {
            auto whatParam = [&]() {
                auto        uri    = ctx.request.endpoint;
                const auto &params = uri.queryParamMap();
                auto        it     = params.find("what");
                return it != params.end() && it->second.has_value() ? it->second.value() : std::string{};
            };

            auto getDashboard = [&](std::string_view name) -> Dashboard * {
                auto it = std::find(names.begin(), names.end(), name);
                if (it != names.end()) {
                    std::string what = whatParam();
                    auto        i    = std::size_t(it - names.begin());
                    return &dashboards[i];
                }
                return nullptr;
            };

            fmt::print("E {}\n",ctx.request.endpoint);
            auto                          uri   = ctx.request.endpoint;
            std::filesystem::path         path(uri.path().value_or("/"));
            std::vector<std::string_view> parts;

            for (auto &p : path) {
                parts.push_back(p.native());
            }
            assert(parts.size() > 0);
            assert(parts[0] == "/");

            if (ctx.request.command == opencmw::mdp::Command::Get) {
                fmt::print("worker received 'get' request\n");

                if (parts.size() == 1) {
                    opencmw::IoBuffer buffer;
                    opencmw::IoSerialiser<opencmw::Json, decltype(names)>::serialise(buffer, opencmw::FieldDescriptionShort{}, names);
                    ctx.reply.data = std::move(buffer);
                } else if (parts.size() == 2) {
                    if (auto *ds = getDashboard(parts[1])) {
                        std::string      what = whatParam();
                        std::string_view view(what);
                        std::string      body;

                        // If more than one 'what' was requested we reply with all of them in the requested order.
                        // the reply format of a 'what' is <size>;<content> and they are all immediately following
                        // the previous one.
                        auto append = [&](std::string_view s) {
                            body += std::to_string(s.size());
                            body += ";";
                            body += s;
                        };
                        while (true) {
                            auto             split = view.find(',');
                            std::string_view w(view.data(), split != view.npos ? split : view.size());
                            if (w == "dashboard") {
                                append(ds->dashboard);
                            } else if (w == "flowgraph") {
                                append(ds->flowgraph);
                            } else {
                                append(ds->header);
                            }
                            if (split == view.npos) {
                                break;
                            }
                            view = std::string_view(view.data() + split + 1, view.size() - split - 1);
                        }
                        ctx.reply.data.put(std::move(body));
                    } else {
                        ctx.reply.error = "invalid request: unknown dashboard";
                    }
                } else {
                    ctx.reply.error = "invalid request: invalid path";
                }
            } else if (ctx.request.command == opencmw::mdp::Command::Set) {
                if (parts.size() == 1) {
                    ctx.reply.error = "invalid request: dashboard not specified";
                } else if (parts.size() == 23) {
                    auto *ds           = getDashboard(parts[1]);
                    bool  newDashboard = false;
                    if (!ds) { // if we couldn't find a dashboard make a new one
                        names.push_back(std::string(parts[1]));
                        dashboards.push_back({});
                        ds           = &dashboards.back();
                        newDashboard = true;
                    }

                    std::string what = whatParam();
                    auto        body = std::move(ctx.request.data);
                    // The first 4 bytes contain the size of the string, including the terminating null byte
                    int32_t size;
                    memcpy(&size, body.data(), 4);
                    std::string data = std::string(reinterpret_cast<char *>(body.data()) + 4, std::size_t(size - 1));

                    if (what == "dashboard") {
                        ds->dashboard = std::move(data);
                        ctx.reply.data.put(std::move(ds->dashboard));
                    } else if (what == "flowgraph") {
                        ds->flowgraph = std::move(data);
                        ctx.reply.data.put(std::move(ds->flowgraph));
                    } else {
                        ds->header = std::move(data);
                        ctx.reply.data.put(std::move(ds->header));
                    }

                    if (newDashboard) {
                        RequestContext rawCtx;
                        rawCtx.reply.endpoint = opencmw::URI<>("/dashboards"s);;

                        opencmw::IoBuffer buffer;
                        opencmw::IoSerialiser<opencmw::Json, decltype(names)>::serialise(buffer, opencmw::FieldDescriptionShort{}, names);
                        rawCtx.reply.data = std::move(buffer);

                        super_t::notify(std::move(rawCtx.reply));
                    }

                } else {
                    ctx.reply.error = "invalid request: invalid path";
                }
            }
        });
        auto     fs   = cmrc::dashboardFilesystem::get_filesystem();
        auto     file = fs.open("dashboard.ddd");

        uint32_t hstart, hsize;
        uint32_t dstart, dsize;
        uint32_t fstart, fsize;

        memcpy(&hstart, file.begin(), 4);
        memcpy(&hsize, file.begin() + 4, 4);

        memcpy(&dstart, file.begin() + 8, 4);
        memcpy(&dsize, file.begin() + 12, 4);

        memcpy(&fstart, file.begin() + 16, 4);
        memcpy(&fsize, file.begin() + 20, 4);

        Dashboard ds;
        ds.header.resize(hsize);
        memcpy(ds.header.data(), file.begin() + hstart, hsize);

        ds.dashboard.resize(dsize);
        memcpy(ds.dashboard.data(), file.begin() + dstart, dsize);

        ds.flowgraph.resize(fsize);
        memcpy(ds.flowgraph.data(), file.begin() + fstart, fsize);

        names.push_back("dashboard1");
        dashboards.push_back(ds);
        names.push_back("dashboard2");
        dashboards.push_back(ds);
        names.push_back("dashboard3");
        dashboards.push_back(ds);
    }

private:
};
