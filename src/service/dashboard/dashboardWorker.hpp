#include <URI.hpp>
#include <majordomo/Broker.hpp>
#include <majordomo/Worker.hpp>
#include <majordomo/base64pp.hpp>

#include <settings.hpp>

#include <gnuradio-4.0/meta/formatter.hpp>

#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <string_view>
#include <thread>

using namespace opencmw::majordomo;
using namespace std::chrono_literals;
using namespace std::string_literals;

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

public:
    using super_t = BasicWorker<serviceName, Meta...>;

    template<typename BrokerType>
    explicit DashboardWorker(const BrokerType& broker) : super_t(broker, {}) {
        super_t::setHandler([this](RequestContext& ctx) {
            auto whatParam = [&]() {
                const auto& params = ctx.request.topic.queryParamMap();
                auto        it     = params.find("what");
                return it != params.end() && it->second.has_value() ? it->second.value() : std::string{};
            };

            auto getDashboard = [&](std::string_view name) -> Dashboard* {
                auto it = std::find(names.begin(), names.end(), name);
                if (it != names.end()) {
                    std::string what = whatParam();
                    auto        i    = std::size_t(it - names.begin());
                    return &dashboards[i];
                }
                return nullptr;
            };

            auto topicPath = ctx.request.topic.path().value_or("/");
            auto pathView  = std::string_view{topicPath};
            if (!pathView.starts_with(DashboardWorker::name)) {
                throw std::invalid_argument(std::format("Unexpected service name in topic ('{}'), must start with '{}'", topicPath, DashboardWorker::name));
            }

            pathView.remove_prefix(DashboardWorker::name.size());

            std::filesystem::path         path(pathView);
            std::vector<std::string_view> parts;

            for (const auto& p : path) {
                parts.push_back(p.native());
            }
            if (parts.empty()) {
                parts.push_back("/");
            }

            if (ctx.request.command == opencmw::mdp::Command::Get) {
                if (parts.size() == 1) {
                    opencmw::IoBuffer buffer;
                    opencmw::IoSerialiser<opencmw::Json, decltype(names)>::serialise(buffer, opencmw::FieldDescriptionShort{}, names);
                    ctx.reply.data = std::move(buffer);
                } else if (parts.size() == 2) {
                    if (auto* ds = getDashboard(parts[1])) {
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
                        ctx.reply.data.put<opencmw::IoBuffer::WITHOUT>(std::move(body));
                    } else {
                        ctx.reply.error = "invalid request: unknown dashboard";
                    }
                } else {
                    ctx.reply.error = "invalid request: invalid path";
                }
            } else if (ctx.request.command == opencmw::mdp::Command::Set) {
                if (parts.size() == 1) {
                    ctx.reply.error = "invalid request: dashboard not specified";
                } else if (parts.size() == 2) {
                    auto* ds           = getDashboard(parts[1]);
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
                    std::string data = std::string(reinterpret_cast<char*>(body.data()) + 4, std::size_t(size - 1));

                    if (what == "dashboard") {
                        ds->dashboard = std::move(data);
                        ctx.reply.data.put<opencmw::IoBuffer::WITHOUT>(std::move(ds->dashboard));
                    } else if (what == "flowgraph") {
                        ds->flowgraph = std::move(data);
                        ctx.reply.data.put<opencmw::IoBuffer::WITHOUT>(std::move(ds->flowgraph));
                    } else {
                        ds->header = std::move(data);
                        ctx.reply.data.put<opencmw::IoBuffer::WITHOUT>(std::move(ds->header));
                    }

                    if (newDashboard) {
                        RequestContext rawCtx;
                        rawCtx.reply.topic = opencmw::URI<>("/dashboards"s);
                        ;

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

        auto readFile = [](const std::filesystem::path& filePath, std::string& contents) {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::print("DashboardWorker: could not read file with default flowgraph: {}\n", filePath.string());
                return;
            }
            contents.assign((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));
            return;
        };
        auto& settings = Digitizer::Settings::instance();
        try {
            for (const auto& dir : std::filesystem::directory_iterator(settings.remoteDashboards)) {
                if (!dir.is_directory() || !exists(dir.path() / "header") || !exists(dir.path() / "dashboard") || !exists(dir.path() / "flowgraph")) {
                    continue;
                }
                std::string name = dir.path().filename();
                Dashboard   dashboard;
                readFile(dir.path() / "header", dashboard.header);
                readFile(dir.path() / "dashboard", dashboard.dashboard);
                readFile(dir.path() / "flowgraph", dashboard.flowgraph);
                names.push_back(name);
                dashboards.push_back(dashboard);
            }
            std::print("DashboardWorker: loaded dashboards: {}\n", gr::join(names));
        } catch (std::filesystem::filesystem_error& e) {
            std::print("DashboardWorker: failed to load default remote Dashboards: {}\n", e.what());
        } catch (...) {
            std::print("DashboardWorker: failed to load default remote Dashboards\n");
        }
    }

private:
};
