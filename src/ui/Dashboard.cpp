#include "Dashboard.hpp"

#include <algorithm>
#include <fstream>
#include <ranges>

#include <fmt/format.h>

#include "common/Events.hpp"
#include "common/ImguiWrap.hpp"

#include <implot.h>

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <daq_api.hpp>
#include <opencmw.hpp>

#include "App.hpp"
#include "Flowgraph.hpp"
#include "GraphModel.hpp"

#include "utils/Yaml.hpp"

using namespace std::string_literals;

struct FlowgraphMessage {
    std::string flowgraph;
    std::string layout;
};

ENABLE_REFLECTION_FOR(FlowgraphMessage, flowgraph, layout)

namespace DigitizerUi {

namespace {
template<typename T>
inline T randomRange(T min, T max) {
    T scale = rand() / (T)RAND_MAX;
    return min + scale * (max - min);
}

uint32_t randomColor() {
    uint8_t x = randomRange(0.0f, 255.0f);
    uint8_t y = randomRange(0.0f, 255.0f);
    uint8_t z = randomRange(0.0f, 255.0f);
    return 0xff000000u | x << 16 | y << 8 | z;
}

std::shared_ptr<DashboardSource> unsavedSource() {
    static auto source = std::make_shared<DashboardSource>(DashboardSource{
        .path    = "Unsaved",
        .isValid = false,
    });
    return source;
}

auto& sources() {
    static std::vector<std::weak_ptr<DashboardSource>> sources;
    return sources;
}

enum class What { Header, Dashboard, Flowgraph };

template<typename T>
struct arrsize;

template<typename T, int N>
struct arrsize<T const (&)[N]> {
    static constexpr auto size = N;
};

template<int N>
auto fetch(const std::shared_ptr<DashboardSource>& source, const std::string& name, What const (&what)[N], std::function<void(std::array<std::string, arrsize<decltype(what)>::size>&&)>&& cb, std::function<void()>&& errCb) {
    if (source->path.starts_with("http://") || source->path.starts_with("https://")) {
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Get;
        auto        path = std::filesystem::path(source->path) / name;
        std::string whatStr;
        for (int i = 0; i < N; ++i) {
            if (i > 0) {
                whatStr += ",";
            }
            whatStr += [&]() {
                switch (what[i]) {
                case What::Header: return "header";
                case What::Dashboard: return "dashboard";
                case What::Flowgraph: return "flowgraph";
                }
                return "header";
            }();
        }

        command.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", whatStr).build();

        command.callback = [callback = std::move(cb), errCallback = std::move(errCb)](const opencmw::mdp::Message& rep) mutable {
            std::array<std::string, N> reply;

            const char* s = reinterpret_cast<const char*>(rep.data.data());
            const char* e = reinterpret_cast<const char*>(s + rep.data.size());
            if (rep.data.data()) {
                for (int i = 0; i < N; ++i) {
                    // the format is: <size>;<content>
                    std::string_view sv(s, e);
                    auto             p = sv.find(';');
                    assert(p != sv.npos);
                    int size = std::atoi(s);
                    s += p + 1; // the +1 is for the ';'

                    reply[i].resize(size);
                    memcpy(reply[i].data(), s, size);
                    s += size;
                }
            }

            if (reply[0].empty()) {
                EventLoop::instance().executeLater(std::move(errCallback));
            } else {
                // schedule the callback so it runs on the main thread
                EventLoop::instance().executeLater([callback, reply]() mutable { callback(std::move(reply)); });
            }
        };

        // static because ~RestClient() waits for the request to finish, and we don't want that
        static opencmw::client::RestClient client;
        client.request(command);
        return;
    } else if (source->path.starts_with("example://")) {
        std::array<std::string, N> reply;
        auto                       fs = cmrc::sample_dashboards::get_filesystem();
        for (int i = 0; i < N; ++i) {
            reply[i] = [&]() -> std::string {
                switch (what[i]) {
                case What::Dashboard: {
                    auto file = fs.open(fmt::format("assets/sampleDashboards/{}.yml", name));
                    return {file.begin(), file.end()};
                }
                case What::Flowgraph: {
                    auto file = fs.open(fmt::format("assets/sampleDashboards/{}.grc", name));
                    return {file.begin(), file.end()};
                }
                default:
                case What::Header: return {"favorite: false\nlastUsed: 07/04/2023"};
                }
            }();
        }
        cb(std::move(reply));
        return;
    } else {
#ifndef EMSCRIPTEN
        auto          path = std::filesystem::path(source->path) / name;
        std::ifstream stream(path, std::ios::in);
        if (stream.is_open()) {
            stream.seekg(0, std::ios::end);
            const auto filesize = stream.tellg();
            stream.seekg(0);

#define ERR                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    \
    auto msg = fmt::format("Cannot load dashboard from '{}'. File is corrupted.", path.native());                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              \
    components::Notification::warning(msg);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    \
    errCb();                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   \
    return;

            if (filesize < 32) {
                ERR
            }

            std::array<std::string, N> desc;
            for (int i = 0; i < N; ++i) {
                auto w = what[i];
                stream.seekg(w == What::Header ? 0 : (w == What::Dashboard ? 8 : 16));

                uint32_t start, size;
                stream.read(reinterpret_cast<char*>(&start), 4);
                stream.read(reinterpret_cast<char*>(&size), 4);

                stream.seekg(start);

                if (filesize < start + size) {
                    ERR
                }
                desc[i].resize(size);
                stream.read(desc[i].data(), size);
            }
#undef ERR
            cb(std::move(desc));
            return;
        }
#endif
    }

    errCb();
}

} // namespace

DashboardSource::~DashboardSource() noexcept {
    sources().erase(std::remove_if(sources().begin(), sources().end(), [](const auto& s) { return s.expired(); }), sources().end());
}

std::shared_ptr<DashboardSource> DashboardSource::get(std::string_view path) {
    auto it = std::find_if(sources().begin(), sources().end(), [=](const auto& s) { return s.lock()->path == path; });
    if (it != sources().end()) {
        return it->lock();
    }

    auto s = std::make_shared<DashboardSource>(DashboardSource{std::string(path), true});
    sources().push_back(s);
    return s;
}

Dashboard::Plot::Plot() {
    static int n = 1;
    name         = fmt::format("Plot {}", n++);
}

Dashboard::Dashboard(PrivateTag, FlowGraphItem* fgItem, const std::shared_ptr<DashboardDescription>& desc) : m_desc(desc), m_fgItem(fgItem) {
    m_desc->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

    // TODO: Block pointer
    localFlowGraph.plotSinkBlockAddedCallback = [this](Block* b) {
        const auto color = std::get_if<uint32_t>(&b->settings().at("color"));
        assert(color);
        m_sources.insert({b->name, b->name, (*color << 8) | 0xff});
    };
    localFlowGraph.blockDeletedCallback = [this](Block* b) {
        const auto blockName = b->name;
        for (auto& p : m_plots) {
            std::erase_if(p.sources, [&blockName](const auto& s) { return s->blockName == blockName; });
        }
        if (b->typeName() == "opendigitizer::RemoteStreamSource" || b->typeName() == "opendigitizer::RemoteDataSetSource") {
            unregisterRemoteService(b->name);
        }
        std::erase_if(m_sources, [&blockName](const auto& s) { return s.blockName == blockName; });
    };
}

Dashboard::~Dashboard() {}

std::shared_ptr<Dashboard> Dashboard::create(FlowGraphItem* fgItem, const std::shared_ptr<DashboardDescription>& desc) { return std::make_shared<Dashboard>(PrivateTag{}, fgItem, desc); }

Block* Dashboard::createSink() {
    const auto sinkCount = std::ranges::count_if(localFlowGraph.blocks(), [](const auto& b) { return b->type().isPlotSink(); });
    auto       name      = fmt::format("sink {}", sinkCount + 1);
    auto       sink      = BlockRegistry::instance().get("opendigitizer::ImPlotSink")->createBlock(name);
    sink->updateSettings({{"color", randomColor()}});
    auto sinkptr = sink.get();
    localFlowGraph.addBlock(std::move(sink));
    return sinkptr;
}

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { m_desc = desc; }

void Dashboard::load() {
    if (m_desc->source != unsavedSource()) {
        fetch(
            m_desc->source, m_desc->filename, {What::Flowgraph, What::Dashboard}, //
            [_this = shared()](std::array<std::string, 2>&& data) { _this->load(std::move(data[0]), std::move(data[1])); },
            [_this = shared()]() {
                auto error = fmt::format("Invalid flowgraph for dashboard {}/{}", _this->m_desc->source->path, _this->m_desc->filename);
                components::Notification::error(error);

                App::instance().closeDashboard();
            });
    } else if (m_fgItem) {
        m_fgItem->setSettings(&localFlowGraph, {});
    }
}

void Dashboard::load(const std::string& grcData, const std::string& dashboardData) {
    try {
        localFlowGraph.parse(grcData);
        // Load is called after parsing the flowgraph so that we already have the list of sources
        doLoad(dashboardData);
    } catch (const std::exception& e) {
        components::Notification::error(fmt::format("Error: {}", e.what()));
        App::instance().closeDashboard();
    }
}

void Dashboard::doLoad(const std::string& desc) {
    YAML::Node tree = YAML::Load(desc);

    auto path = std::filesystem::path(m_desc->source->path) / m_desc->filename;

    auto sources = tree["sources"];
    if (!sources || !sources.IsSequence()) {
        throw std::runtime_error("sources entry invalid");
    }

    for (const auto& s : sources) {
        if (!s.IsMap()) {
            throw std::runtime_error("source is no map");
        }

        auto block = s["block"];
        auto port  = s["port"];
        auto name  = s["name"];
        auto color = s["color"];
        if (!block || !block.IsScalar() || !port || !port.IsScalar() || !name || !name.IsScalar() || !color || !color.IsScalar()) {
            throw std::runtime_error("invalid source color definition");
        }

        auto blockStr = block.as<std::string>();
        auto portNum  = port.as<int>();
        auto nameStr  = name.as<std::string>();
        auto colorNum = color.as<uint32_t>();

        auto source = std::find_if(m_sources.begin(), m_sources.end(), [&](const auto& s) { return s.name == nameStr; });
        if (source == m_sources.end()) {
            auto msg = fmt::format("Unable to find the source '{}.{}'", blockStr, portNum);
            components::Notification::warning(msg);
            continue;
        }

        source->name  = nameStr;
        source->color = colorNum;
    }

    auto plots = tree["plots"];
    if (!plots || !plots.IsSequence()) {
        throw std::runtime_error("plots invalid");
    }

    for (const auto& p : plots) {
        if (!p.IsMap()) {
            throw std::runtime_error("plots is not map");
        }

        auto name        = p["name"];
        auto axes        = p["axes"];
        auto plotSources = p["sources"];
        auto rect        = p["rect"];
        if (!name || !name.IsScalar() || !axes || !axes.IsSequence() || !plotSources || !plotSources.IsSequence() || !rect || !rect.IsSequence() || rect.size() != 4) {
            throw std::runtime_error("invalid plot definition");
        }

        m_plots.emplace_back();
        auto& plot = m_plots.back();
        plot.name  = name.as<std::string>();

        for (const auto& a : axes) {
            if (!a.IsMap()) {
                throw std::runtime_error("axes is no map");
            }

            auto axis = a["axis"];
            auto min  = a["min"];
            auto max  = a["max"];

            if (!axis || !axis.IsScalar() || !min || !min.IsScalar() || !max || !max.IsScalar()) {
                throw std::runtime_error("invalid axis definition");
            }

            plot.axes.push_back({});
            auto& ax = plot.axes.back();

            auto axisStr = axis.as<std::string>();

            if (axisStr == "X") {
                ax.axis = Plot::Axis::X;
            } else if (axisStr == "Y") {
                ax.axis = Plot::Axis::Y;
            } else {
                auto msg = fmt::format("Unknown axis {}", axisStr);
                components::Notification::warning(msg);
                return;
            }

            ax.min = min.as<double>();
            ax.max = max.as<double>();
        }

        for (const auto& s : plotSources) {
            if (!s.IsScalar()) {
                throw std::runtime_error("plot source is no scalar");
            }

            auto str = s.as<std::string>();
            plot.sourceNames.push_back(str);
        }

        plot.rect.x = rect[0].as<int>();
        plot.rect.y = rect[1].as<int>();
        plot.rect.w = rect[2].as<int>();
        plot.rect.h = rect[3].as<int>();
    }

    if (m_fgItem) {
        auto fgLayout = tree["flowgraphLayout"];
        m_fgItem->setSettings(&localFlowGraph, fgLayout && fgLayout.IsScalar() ? fgLayout.as<std::string>() : std::string{});
    }

    loadPlotSources();
}

void Dashboard::save() {
    if (!m_desc->source->isValid) {
        return;
    }

    YAML::Emitter headerOut;
    {
        YamlMap root(headerOut);

        root.write("favorite", m_desc->isFavorite);
        std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(m_desc->lastUsed.value()));
        char                        lastUsed[11];
        fmt::format_to(lastUsed, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
        root.write("lastUsed", lastUsed);
    }

    YAML::Emitter dashboardOut;
    {
        YamlMap root(dashboardOut);

        root.write("sources", [&]() {
            YamlSeq sources(dashboardOut);

            for (auto& s : m_sources) {
                YamlMap source(dashboardOut);
                source.write("name", s.name);

                source.write("block", s.blockName);
                source.write("color", s.color);
            }
        });

        root.write("plots", [&]() {
            YamlSeq plots(dashboardOut);

            for (auto& p : m_plots) {
                YamlMap plot(dashboardOut);
                plot.write("name", p.name);
                plot.write("axes", [&]() {
                    YamlSeq axes(dashboardOut);

                    for (const auto& axis : p.axes) {
                        YamlMap a(dashboardOut);
                        a.write("axis", axis.axis == Plot::Axis::X ? "X" : "Y");
                        a.write("min", axis.min);
                        a.write("max", axis.max);
                    }
                });
                plot.write("sources", [&]() {
                    YamlSeq sources(dashboardOut);

                    for (auto& s : p.sources) {
                        dashboardOut << s->name;
                    }
                });
                plot.write("rect", [&]() {
                    YamlSeq rect(dashboardOut);
                    dashboardOut << p.rect.x;
                    dashboardOut << p.rect.y;
                    dashboardOut << p.rect.w;
                    dashboardOut << p.rect.h;
                });
            }
        });

        if (m_fgItem) {
            root.write("flowgraphLayout", m_fgItem->settings(&localFlowGraph));
        }
    }

    if (m_desc->source->path.starts_with("http://") || m_desc->source->path.starts_with("https://")) {
        opencmw::client::RestClient client;
        auto                        path = std::filesystem::path(m_desc->source->path) / m_desc->filename;

        opencmw::client::Command hcommand;
        hcommand.command = opencmw::mdp::Command::Set;
        hcommand.data.put(std::string_view(headerOut.c_str(), headerOut.size()));
        hcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "header").build();
        client.request(hcommand);

        opencmw::client::Command dcommand;
        dcommand.command = opencmw::mdp::Command::Set;
        dcommand.data.put(std::string_view(dashboardOut.c_str(), dashboardOut.size()));
        dcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "dashboard").build();
        client.request(dcommand);

        opencmw::client::Command fcommand;
        fcommand.command = opencmw::mdp::Command::Set;
        std::stringstream stream;
        localFlowGraph.save(stream);
        fcommand.data.put(stream.str());
        fcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "flowgraph").build();
        client.request(fcommand);

    } else {
#ifndef EMSCRIPTEN
        auto path = std::filesystem::path(m_desc->source->path);

        std::ofstream stream(path / (m_desc->name + DashboardDescription::fileExtension), std::ios::out | std::ios::trunc);
        if (!stream.is_open()) {
            auto msg = fmt::format("can't open file for writing");
            components::Notification::warning(msg);
            return;
        }

        uint32_t headerStart    = 32;
        uint32_t headerSize     = headerOut.size();
        uint32_t dashboardStart = headerStart + headerSize + 1;
        uint32_t dashboardSize  = dashboardOut.size();
        stream.write(reinterpret_cast<char*>(&headerStart), 4);
        stream.write(reinterpret_cast<char*>(&headerSize), 4);
        stream.write(reinterpret_cast<char*>(&dashboardStart), 4);
        stream.write(reinterpret_cast<char*>(&dashboardSize), 4);

        stream.seekp(headerStart);
        stream << headerOut.c_str() << '\n';
        stream << dashboardOut.c_str() << '\n';
        uint32_t flowgraphStart = stream.tellp();
        uint32_t flowgraphSize  = localFlowGraph.save(stream);
        stream.seekp(16);
        stream.write(reinterpret_cast<char*>(&flowgraphStart), 4);
        stream.write(reinterpret_cast<char*>(&flowgraphSize), 4);
        stream << '\n';
#endif
    }
}

void Dashboard::newPlot(int x, int y, int w, int h) {
    m_plots.push_back({});
    auto& p = m_plots.back();
    p.axes.push_back({Plot::Axis::X});
    p.axes.push_back({Plot::Axis::Y});
    p.rect = {x, y, w, h};
}

void Dashboard::deletePlot(Plot* plot) {
    auto it = std::find_if(m_plots.begin(), m_plots.end(), [=](const Plot& p) { return plot == &p; });
    m_plots.erase(it);
}

void Dashboard::removeSinkFromPlots(std::string_view sinkName) {
    for (auto& plot : m_plots) {
        std::erase(plot.sourceNames, std::string(sinkName));
    }
    std::erase_if(m_plots, [](const Plot& p) { return p.sourceNames.empty(); });
}

void Dashboard::loadPlotSources() {
    for (auto& plot : m_plots) {
        plot.sources.clear();

        for (const auto& name : plot.sourceNames) {
            auto source = std::ranges::find_if(m_sources.begin(), m_sources.end(), [&](const auto& s) { return s.name == name; });
            if (source == m_sources.end()) {
                auto msg = fmt::format("Unable to find source {}", name);
                components::Notification::warning(msg);
                continue;
            }
            plot.sources.push_back(&*source);
        }
    }
}

void Dashboard::registerRemoteService(std::string_view blockName, std::string_view uri_) {
    const auto uri = [&] -> std::optional<opencmw::URI<>> {
        try {
            return opencmw::URI<>(std::string(uri_));
        } catch (const std::exception& e) {
            auto msg = fmt::format("remote_source of '{}' is not a valid URI '{}': {}", blockName, uri_, e.what());
            components::Notification::error(msg);
            return {};
        }
    }();

    if (!uri) {
        return;
    }

    const auto flowgraphUri = opencmw::URI<>::UriFactory(*uri).path("/flowgraph").setQuery({}).build().str();
    m_flowgraphUriByRemoteSource.insert({std::string{blockName}, flowgraphUri});

    const auto it = std::ranges::find_if(m_services, [&](const auto& s) { return s.uri == flowgraphUri; });
    if (it == m_services.end()) {
        auto msg = fmt::format("Registering to remote flow graph for '{}' at {}", blockName, flowgraphUri);
        components::Notification::warning(msg);
        auto& s = *m_services.emplace(flowgraphUri, flowgraphUri);
        s.reload();
    }
    removeUnusedRemoteServices();
}

void Dashboard::unregisterRemoteService(std::string_view blockName) {
    m_flowgraphUriByRemoteSource.erase(std::string{blockName});
    removeUnusedRemoteServices();
}

void Dashboard::removeUnusedRemoteServices() {
    std::erase_if(m_services, [&](const auto& s) { return std::ranges::none_of(m_flowgraphUriByRemoteSource | std::views::values, [&s](const auto& uri) { return uri == s.uri; }); });
}

void Dashboard::Service::reload() {
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Get;
    command.topic    = opencmw::URI<>(uri);
    command.callback = [&](const opencmw::mdp::Message& rep) {
        auto buf = rep.data;

        opendigitizer::flowgraph::SerialisedFlowgraphMessage serialisedMessage;
        opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buf, serialisedMessage);

        gr::Message message      = opendigitizer::gnuradio::deserialiseMessage(serialisedMessage.data);
        auto        newFlowgraph = opendigitizer::flowgraph::getFlowgraphFromMessage(message);

        if (newFlowgraph) {
            grc    = newFlowgraph->serialisedFlowgraph;
            layout = newFlowgraph->serialisedUiLayout;

#if 0 // TODO this crashes on e.g. unknown blocks
            s.flowGraph.parse(reply.flowgraph);
#endif
            App::instance().fgItem.setSettings(&flowGraph, newFlowgraph->serialisedUiLayout);
        } else {
            components::Notification::warning("Error reading flowgraph from the service reply");
        }
    };
    client.request(command);
}

void Dashboard::Service::emplaceBlock(std::string type, std::string params) {
    gr::Message message;
    message.cmd      = gr::message::Command::Set;
    message.endpoint = gr::graph::property::kEmplaceBlock;
    message.data     = gr::property_map{
            {"type"s, std::move(type)},        //
            {"parameters"s, std::move(params)} //
    };

    opendigitizer::flowgraph::SerialisedFlowgraphMessage serialisedMessage{opendigitizer::gnuradio::serialiseMessage(message)};

    opencmw::client::Command command;
    command.command = opencmw::mdp::Command::Set;

    opencmw::serialise<opencmw::Json>(command.data, serialisedMessage);

    command.topic    = opencmw::URI<>(uri);
    command.callback = [&](const opencmw::mdp::Message& rep) {
        if (!rep.error.empty()) {
            components::Notification::warning(rep.error);
        }
    };
    client.request(command);
}

void Dashboard::Service::execute() {
    opencmw::client::Command command;
    command.command = opencmw::mdp::Command::Set;

    FlowgraphMessage request;
    request.flowgraph = grc;
    request.layout    = layout;
    opencmw::serialise<opencmw::Json>(command.data, request);

    command.topic    = opencmw::URI<>(uri);
    command.callback = [&](const opencmw::mdp::Message& rep) {
        if (!rep.error.empty()) {
            components::Notification::warning(rep.error);
        }
    };
    client.request(command);
}

void Dashboard::saveRemoteServiceFlowgraph(Service* s) {
    std::stringstream stream;
    s->flowGraph.save(stream);

    opencmw::client::Command command;
    command.command = opencmw::mdp::Command::Set;
    command.topic   = opencmw::URI<>(s->uri);

    FlowgraphMessage msg;
    msg.flowgraph = std::move(stream).str();
    if (m_fgItem) {
        msg.layout = m_fgItem->settings(&s->flowGraph);
    }
    opencmw::serialise<opencmw::Json>(command.data, msg);
    s->client.request(command);
}

void DashboardDescription::load(const std::shared_ptr<DashboardSource>& source, const std::string& name, const std::function<void(std::shared_ptr<DashboardDescription>&&)>& cb) {
    fetch(
        source, name, {What::Header},
        [cb, name, source](std::array<std::string, 1>&& desc) {
            YAML::Node tree = YAML::Load(desc[0]);

            auto favorite = tree["favorite"];
            auto lastUsed = tree["lastUsed"];

            auto getDate = [](const auto& str) -> decltype(DashboardDescription::lastUsed) {
                if (str.size() < 10) {
                    return {};
                }
                int      year  = std::atoi(str.data());
                unsigned month = std::atoi(str.c_str() + 5);
                unsigned day   = std::atoi(str.c_str() + 8);

                std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
                return std::chrono::sys_days(date);
            };

            cb(std::make_shared<DashboardDescription>(DashboardDescription{.name = std::filesystem::path(name).stem().native(), .source = source, .filename = name, .isFavorite = favorite.IsScalar() ? favorite.as<bool>() : false, .lastUsed = lastUsed.IsScalar() ? getDate(lastUsed.as<std::string>()) : std::nullopt}));
        },
        [cb]() { cb({}); });
}

std::shared_ptr<DashboardDescription> DashboardDescription::createEmpty(const std::string& name) { return std::make_shared<DashboardDescription>(DashboardDescription{.name = name, .source = unsavedSource(), .isFavorite = false, .lastUsed = std::nullopt}); }

} // namespace DigitizerUi
