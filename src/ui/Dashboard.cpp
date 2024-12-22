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
    T scale = static_cast<T>(rand()) / static_cast<T>(RAND_MAX);
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

template<std::size_t N>
auto fetch(const std::shared_ptr<DashboardSource>& source, const std::string& name, What const (&what)[N], std::function<void(std::array<std::string, arrsize<decltype(what)>::size>&&)>&& cb, std::function<void()>&& errCb) {
    if (source->path.starts_with("http://") || source->path.starts_with("https://")) {
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Get;
        auto        path = std::filesystem::path(source->path) / name;
        std::string whatStr;
        for (std::size_t i = 0UZ; i < N; ++i) {
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
                for (std::size_t i = 0UZ; i < N; ++i) {
                    // the format is: <size>;<content>
                    std::string_view sv(s, e);
                    auto             p = sv.find(';');
                    assert(p != sv.npos);
                    std::size_t size = std::stoul(s);
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
        for (std::size_t i = 0UZ; i < N; ++i) {
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
            for (std::size_t i = 0UZ; i < N; ++i) {
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
    window       = std::make_shared<DockSpace::Window>(name);
}

Dashboard::Dashboard(PrivateTag, FlowGraphItem* fgItem, const std::shared_ptr<DashboardDescription>& desc) : m_desc(desc), m_fgItem(fgItem) {
    m_desc->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

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
    } catch (const gr::exception& e) {
#ifndef NDEBUG
        fmt::println(stderr, "Dashboard::load(const std::string& grcData,const std::string& dashboardData): error: {}", e);
#endif
        components::Notification::error(fmt::format("Error: {}", e.what()));
        App::instance().closeDashboard();
    } catch (const std::exception& e) {
#ifndef NDEBUG
        fmt::println(stderr, "Dashboard::load(const std::string& grcData,const std::string& dashboardData): error: {}", e.what());
#endif
        components::Notification::error(fmt::format("Error: {}", e.what()));
        App::instance().closeDashboard();
    }
}

void Dashboard::doLoad(const std::string& desc) {
    using namespace gr;
    const auto yaml = pmtv::yaml::deserialize(desc);
    if (!yaml) {
        throw gr::exception(fmt::format("Could not parse yaml for Dashboard: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
    }

    const property_map& rootMap = yaml.value();

    auto path = std::filesystem::path(m_desc->source->path) / m_desc->filename;

    if (!rootMap.contains("sources") || !std::holds_alternative<std::vector<pmtv::pmt>>(rootMap.at("sources"))) {
        throw gr::exception("sources entry invalid");
    }
    auto sources = std::get<std::vector<pmtv::pmt>>(rootMap.at("sources"));

    for (const auto& src : sources) {
        if (!std::holds_alternative<property_map>(src)) {
            throw gr::exception("source is not a property_map");
        }
        const property_map srcMap = std::get<property_map>(src);

        if (!srcMap.contains("block") || !std::holds_alternative<std::string>(srcMap.at("block"))  //
            || !srcMap.contains("name") || !std::holds_alternative<std::string>(srcMap.at("name")) //
            || !srcMap.contains("color")) {
            throw std::runtime_error("invalid source color definition");
        }
        auto block = std::get<std::string>(srcMap.at("block"));
        auto name  = std::get<std::string>(srcMap.at("name"));
        auto color = pmtv::cast<std::uint32_t>(srcMap.at("color"));

        auto source = std::find_if(m_sources.begin(), m_sources.end(), [&](const auto& s) { return s.name == name; });
        if (source == m_sources.end()) {
            auto msg = fmt::format("Unable to find the source '{}'", block);
            components::Notification::warning(msg);
            continue;
        }

        source->name      = name;
        source->color     = color;
        source->blockName = block;
    }

    if (!rootMap.contains("plots") || !std::holds_alternative<std::vector<pmtv::pmt>>(rootMap.at("plots"))) {
        throw gr::exception("plots entry invalid");
    }
    auto plots = std::get<std::vector<pmtv::pmt>>(rootMap.at("plots"));

    for (const auto& plotPmt : plots) {
        if (!std::holds_alternative<property_map>(plotPmt)) {
            throw gr::exception("plot is not a property_map");
        }
        const property_map plotMap = std::get<property_map>(plotPmt);

        if (!plotMap.contains("name") || !std::holds_alternative<std::string>(plotMap.at("name"))                     //
            || !plotMap.contains("sources") || !std::holds_alternative<std::vector<pmtv::pmt>>(plotMap.at("sources")) //
            || !plotMap.contains("rect") || !std::holds_alternative<std::vector<pmtv::pmt>>(plotMap.at("rect"))) {
            throw gr::exception("invalid plot definition");
        }

        auto name        = std::get<std::string>(plotMap.at("name"));
        auto plotSources = std::get<std::vector<pmtv::pmt>>(plotMap.at("sources"));
        auto rect        = std::get<std::vector<pmtv::pmt>>(plotMap.at("rect"));
        if (rect.size() != 4) {
            throw gr::exception("invalid plot definition rect.size() != 4");
        }

        m_plots.emplace_back();
        auto& plot = m_plots.back();
        plot.name  = name;

        if (plotMap.contains("axes") && std::holds_alternative<std::vector<pmtv::pmt>>(plotMap.at("axes"))) {
            auto axes = std::get<std::vector<pmtv::pmt>>(plotMap.at("axes"));
            for (const auto& axisPmt : axes) {
                if (!std::holds_alternative<property_map>(axisPmt)) {
                    throw gr::exception("axis is not a property_map");
                }
                const property_map axisMap = std::get<property_map>(axisPmt);

                if (!axisMap.contains("axis") || !std::holds_alternative<std::string>(axisMap.at("axis")) //
                    || !axisMap.contains("min") || !axisMap.contains("max")) {
                    throw gr::exception("invalid axis definition");
                }

                plot.axes.push_back({});
                auto& axisData = plot.axes.back();

                auto axis = std::get<std::string>(axisMap.at("axis"));
                if (axis == "X") {
                    axisData.axis = Plot::Axis::X;
                } else if (axis == "Y") {
                    axisData.axis = Plot::Axis::Y;
                } else {
                    components::Notification::warning(fmt::format("Unknown axis {}", axis));
                    return;
                }

                axisData.min = pmtv::cast<float>(axisMap.at("min"));
                axisData.max = pmtv::cast<float>(axisMap.at("max"));
            }
        } else { // add default axes and ranges if not defined
            plot.axes.push_back({Plot::Axis::X});
            plot.axes.push_back({Plot::Axis::Y});
        }

        std::transform(plotSources.begin(), plotSources.end(), std::back_inserter(plot.sourceNames), [](const auto& elem) { return std::get<std::string>(elem); });
        plot.window->x      = pmtv::cast<int>(rect[0]);
        plot.window->y      = pmtv::cast<int>(rect[1]);
        plot.window->width  = pmtv::cast<int>(rect[2]);
        plot.window->height = pmtv::cast<int>(rect[3]);
    }

    if (m_fgItem) {
        const bool isGoodString = rootMap.contains("flowgraphLayout") && std::holds_alternative<std::string>(rootMap.at("flowgraphLayout"));
        m_fgItem->setSettings(&localFlowGraph, isGoodString ? std::get<std::string>(rootMap.at("flowgraphLayout")) : std::string{});
    }

    loadPlotSources();
}

void Dashboard::save() {
    using namespace gr;

    if (!m_desc->source->isValid) {
        return;
    }

    property_map headerYaml;
    headerYaml["favorite"] = m_desc->isFavorite;
    std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(m_desc->lastUsed.value()));
    char                        lastUsed[11];
    fmt::format_to(lastUsed, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
    headerYaml["lastUsed"] = std::string(lastUsed);

    property_map dashboardYaml;

    std::vector<pmtv::pmt> sources;
    for (auto& src : m_sources) {
        property_map map;
        map["name"]  = src.name;
        map["block"] = src.blockName;
        map["color"] = src.color;
        sources.emplace_back(std::move(map));
    }
    dashboardYaml["sources"] = sources;

    std::vector<pmtv::pmt> plots;
    for (auto& p : m_plots) {
        property_map plotMap;
        plotMap["name"] = p.name;

        std::vector<pmtv::pmt> plotAxes;
        for (const auto& axis : p.axes) {
            property_map axisMap;
            axisMap["axis"] = axis.axis == Plot::Axis::X ? "X" : "Y";
            axisMap["min"]  = axis.min;
            axisMap["max"]  = axis.max;
            plotAxes.emplace_back(std::move(axisMap));
        }
        plotMap["axes"] = plotAxes;

        std::vector<pmtv::pmt> plotSources;
        for (auto& s : p.sources) {
            plotSources.emplace_back(s->name);
        }
        plotMap["sources"] = plotSources;

        std::vector<int> plotRect;
        plotRect.emplace_back(p.window->x);
        plotRect.emplace_back(p.window->y);
        plotRect.emplace_back(p.window->width);
        plotRect.emplace_back(p.window->height);
        plotMap["rect"] = plotRect;

        plots.emplace_back(plotMap);
    }
    dashboardYaml["plots"] = plots;

    if (m_fgItem) {
        dashboardYaml["flowgraphLayout"] = m_fgItem->settings(&localFlowGraph);
    }

    if (m_desc->source->path.starts_with("http://") || m_desc->source->path.starts_with("https://")) {
        opencmw::client::RestClient client;
        auto                        path = std::filesystem::path(m_desc->source->path) / m_desc->filename;

        opencmw::client::Command hcommand;
        hcommand.command          = opencmw::mdp::Command::Set;
        std::string headerYamlStr = pmtv::yaml::serialize(headerYaml);
        hcommand.data.put(std::string_view(headerYamlStr.c_str(), headerYamlStr.size()));
        hcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "header").build();
        client.request(hcommand);

        opencmw::client::Command dcommand;
        dcommand.command             = opencmw::mdp::Command::Set;
        std::string dashboardYamlStr = pmtv::yaml::serialize(dashboardYaml);
        dcommand.data.put(std::string_view(dashboardYamlStr.c_str(), dashboardYamlStr.size()));
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

        uint32_t      headerStart      = 32;
        std::string   headerYamlStr    = pmtv::yaml::serialize(headerYaml);
        std::string   dashboardYamlStr = pmtv::yaml::serialize(dashboardYaml);
        std::uint32_t headerSize       = static_cast<uint32_t>(headerYamlStr.size());
        std::uint32_t dashboardStart   = headerStart + headerSize + 1;
        std::uint32_t dashboardSize    = static_cast<uint32_t>(dashboardYamlStr.size());
        stream.write(reinterpret_cast<char*>(&headerStart), 4);
        stream.write(reinterpret_cast<char*>(&headerSize), 4);
        stream.write(reinterpret_cast<char*>(&dashboardStart), 4);
        stream.write(reinterpret_cast<char*>(&dashboardSize), 4);

        stream.seekp(headerStart);
        stream << headerYamlStr.c_str() << '\n';
        stream << dashboardYamlStr.c_str() << '\n';
        std::uint32_t flowgraphStart = stream.tellp();
        std::uint32_t flowgraphSize  = localFlowGraph.save(stream);
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
    p.window->setGeometry({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(h)});
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
    using namespace gr;
    fetch(
        source, name, {What::Header},
        [cb, name, source](std::array<std::string, 1>&& desc) {
            const auto yaml = pmtv::yaml::deserialize(desc[0]);
            if (!yaml) {
                throw gr::exception(fmt::format("Could not parse yaml for DashboardDescription: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
            }
            const property_map& rootMap  = yaml.value();
            bool                favorite = rootMap.contains("favorite") && std::holds_alternative<bool>(rootMap.at("favorite")) && std::get<bool>(rootMap.at("favorite"));

            auto getDate = [](const std::string& str) -> decltype(DashboardDescription::lastUsed) {
                if (str.size() < 10) {
                    return {};
                }
                int      year  = std::atoi(str.data());
                unsigned month = std::atoi(str.c_str() + 5UZ);
                unsigned day   = std::atoi(str.c_str() + 8UZ);

                std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
                return std::chrono::sys_days(date);
            };

            auto lastUsed = rootMap.contains("lastUsed") && std::holds_alternative<bool>(rootMap.at("lastUsed")) ? getDate(std::get<std::string>(rootMap.at("lastUsed"))) : std::nullopt;

            cb(std::make_shared<DashboardDescription>(DashboardDescription{.name = std::filesystem::path(name).stem().native(), .source = source, .filename = name, .isFavorite = favorite, .lastUsed = lastUsed}));
        },
        [cb]() { cb({}); });
}

std::shared_ptr<DashboardDescription> DashboardDescription::createEmpty(const std::string& name) { return std::make_shared<DashboardDescription>(DashboardDescription{.name = name, .source = unsavedSource(), .isFavorite = false, .lastUsed = std::nullopt}); }

} // namespace DigitizerUi
