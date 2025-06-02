#include "Dashboard.hpp"

#include <algorithm>
#include <fstream>
#include <ranges>

#include <format>

#include "common/Events.hpp"
#include "common/ImguiWrap.hpp"

#include <implot.h>

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <daq_api.hpp>
#include <opencmw.hpp>

#include "App.hpp"
#include "GraphModel.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "components/SignalSelector.hpp"

#include "conversion.hpp"

using namespace std::string_literals;

struct FlowgraphMessage {
    std::string flowgraph;
    std::string layout;
};

ENABLE_REFLECTION_FOR(FlowgraphMessage, flowgraph, layout)

namespace DigitizerUi {

namespace {
enum class What { Header, Dashboard, Flowgraph };

template<typename T>
struct arrsize;

template<typename T, std::size_t N>
struct arrsize<T const (&)[N]> {
    static constexpr auto size = N;
};

template<std::size_t N>
auto fetch(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, What const (&what)[N], std::function<void(std::array<std::string, arrsize<decltype(what)>::size>&&)>&& cb, std::function<void()>&& errCb) {
    if (storageInfo->path.starts_with("http://") || storageInfo->path.starts_with("https://")) {
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Get;
        auto        path = std::filesystem::path(storageInfo->path) / name;
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
                    std::size_t size = std::stoul(std::string(s, p));
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
    } else if (storageInfo->path.starts_with("example://")) {
        std::array<std::string, N> reply;
        auto                       fs = cmrc::sample_dashboards::get_filesystem();
        for (std::size_t i = 0UZ; i < N; ++i) {
            reply[i] = [&]() -> std::string {
                switch (what[i]) {
                case What::Dashboard: {
                    auto file = fs.open(std::format("assets/sampleDashboards/{}.yml", name));
                    return {file.begin(), file.end()};
                }
                case What::Flowgraph: {
                    auto file = fs.open(std::format("assets/sampleDashboards/{}.grc", name));
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
        auto          path = std::filesystem::path(storageInfo->path) / name;
        std::ifstream stream(path, std::ios::in);
        if (stream.is_open()) {
            stream.seekg(0, std::ios::end);
            const auto filesize = stream.tellg();
            stream.seekg(0);

#define ERR                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    \
    auto msg = std::format("Cannot load dashboard from '{}'. File is corrupted.", path.native());                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              \
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

DashboardStorageInfo::~DashboardStorageInfo() noexcept {
    std::erase_if(knownDashboardStorage(), [](const auto& s) { return s.expired(); });
}

std::vector<std::weak_ptr<DashboardStorageInfo>>& DigitizerUi::DashboardStorageInfo::knownDashboardStorage() {
    static std::vector<std::weak_ptr<DashboardStorageInfo>> sources;
    return sources;
}

std::shared_ptr<DashboardStorageInfo> DashboardStorageInfo::get(std::string_view path) {
    auto it = std::ranges::find_if(knownDashboardStorage(), [=](const auto& s) { return s.lock()->path == path; });
    if (it != knownDashboardStorage().end()) {
        return it->lock();
    }

    auto dashboardStorageInfo = std::make_shared<DashboardStorageInfo>(std::string(path), PrivateTag{});
    std::print("Creating dashboard source for path {}\n", path);
    knownDashboardStorage().push_back(dashboardStorageInfo);
    return dashboardStorageInfo;
}

// This does not really have a shared pointer semantics,
// it is a static shared pointer, so it has a static lifetime.
// It is a shared pointer because DashboardSources are used
// as shared pointers, and this is a single special instance
// of all dashboard sources which is not saved.
std::shared_ptr<DashboardStorageInfo> DashboardStorageInfo::memoryDashboardStorage() {
    static auto storageInfo = std::make_shared<DashboardStorageInfo>("Unsaved"s, PrivateTag{});
    return storageInfo;
}

Dashboard::Dashboard(PrivateTag, const std::shared_ptr<const DashboardDescription>& desc) : m_desc(desc) {
    m_desc->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

    const auto style = Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light;
    switch (style) {
    case LookAndFeel::Style::Dark: ImGui::StyleColorsDark(); break;
    case LookAndFeel::Style::Light: ImGui::StyleColorsLight(); break;
    }
    LookAndFeel::mutableInstance().style         = style;
    ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    m_graphModel.sendMessage = [this](gr::Message message) { m_scheduler.sendMessage(std::move(message)); };
}

Dashboard::~Dashboard() {}

std::unique_ptr<Dashboard> Dashboard::create(const std::shared_ptr<const DashboardDescription>& desc) { return std::make_unique<Dashboard>(PrivateTag{}, desc); }

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { m_desc = desc; }

void Dashboard::load() {
    if (!m_desc->storageInfo->isInMemoryDashboardStorage()) {
        m_isInitialised.store(false, std::memory_order_release);
        isInUse = true;
        fetch(
            m_desc->storageInfo, m_desc->filename, {What::Flowgraph, What::Dashboard}, //
            [this](std::array<std::string, 2>&& data) {
                //
                loadAndThen(std::move(data[0]), std::move(data[1]), [this](gr::Graph&& graph) { m_scheduler.emplaceGraph(std::move(graph)); });
                isInUse = false;
            },
            [this]() {
                auto error = std::format("Invalid flowgraph for dashboard {}/{}", m_desc->storageInfo->path, m_desc->filename);
                components::Notification::error(error);

                isInUse = false;
                if (requestClose) {
                    requestClose(this);
                }
            });
    }
}

void Dashboard::loadAndThen(const std::string& grcData, const std::string& dashboardData, std::function<void(gr::Graph&&)> assignScheduler) {
    try {
        gr::Graph grGraph = [this, &grcData]() -> gr::Graph {
            try {
                return gr::loadGrc(*pluginLoader, grcData);
            } catch (const gr::exception& e) {
                throw;
            } catch (const std::string& e) {
                throw gr::exception(e);
            } catch (...) {
                throw std::current_exception();
            }
        }();

        if (const auto dashboardUri = opencmw::URI<>(std::string(m_desc->storageInfo->path)); dashboardUri.hostName().has_value()) {
            const auto remoteUri = dashboardUri.factory().hostName(*dashboardUri.hostName()).port(dashboardUri.port().value_or(8080)).scheme(dashboardUri.scheme().value_or("https")).build();
            grGraph.forEachBlockMutable([this, &remoteUri](auto& block) {
                if (block.typeName().starts_with("opendigitizer::RemoteStreamSource") || block.typeName().starts_with("opendigitizer::RemoteDataSetSource")) {
                    auto* sourceBlock = static_cast<opendigitizer::RemoteSourceBase*>(block.raw());
                    sourceBlock->host = remoteUri.str();
                }
            });
        }

        assignScheduler(std::move(grGraph));

        // Load is called after parsing the flowgraph so that we already have the list of sources
        doLoad(dashboardData);
        m_isInitialised.store(true, std::memory_order_release);
    } catch (const gr::exception& e) {
#ifndef NDEBUG
        std::println(stderr, "Dashboard::load(const std::string& grcData,const std::string& dashboardData): error: {}", e);
#endif
        components::Notification::error(std::format("Error: {}", e.what()));
        if (requestClose) {
            requestClose(this);
        }
    } catch (const std::exception& e) {
#ifndef NDEBUG
        std::println(stderr, "Dashboard::load(const std::string& grcData,const std::string& dashboardData): error: {}", e.what());
#endif
        components::Notification::error(std::format("Error: {}", e.what()));
        if (requestClose) {
            requestClose(this);
        }
    }
}

void Dashboard::doLoad(const std::string& desc) {
    using namespace gr;
    const auto yaml = pmtv::yaml::deserialize(desc);
    if (!yaml) {
        throw gr::exception(std::format("Could not parse yaml for Dashboard: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
    }

    const property_map& rootMap = yaml.value();

    auto path = std::filesystem::path(m_desc->storageInfo->path) / m_desc->filename;

    if (rootMap.contains("layout") && std::holds_alternative<std::string>(rootMap.at("layout"))) {
        m_layout = magic_enum::enum_cast<DockingLayoutType>(std::get<std::string>(rootMap.at("layout")), magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
    }

    if (!rootMap.contains("sources") || !std::holds_alternative<std::vector<pmtv::pmt>>(rootMap.at("sources"))) {
        throw gr::exception("sources entry invalid");
    }
    auto sources = std::get<std::vector<pmtv::pmt>>(rootMap.at("sources"));

    for (const auto& src : sources) {
        if (!std::holds_alternative<property_map>(src)) {
            throw gr::exception("source is not a property_map");
        }
        const property_map srcMap = std::get<property_map>(src);

        if (!srcMap.contains("block") || !std::holds_alternative<std::string>(srcMap.at("block")) //
            || !srcMap.contains("name") || !std::holds_alternative<std::string>(srcMap.at("name"))) {
            throw std::runtime_error("invalid source name definition");
        }
        auto block = std::get<std::string>(srcMap.at("block"));
        auto name  = std::get<std::string>(srcMap.at("name"));

        auto* sink = opendigitizer::ImPlotSinkManager::instance().findSink([&](const auto& s) { return s.name() == block; });
        if (!sink) {
            auto msg = std::format("Unable to find the plot source -- sink: '{}'", block);
            components::Notification::warning(msg);
            continue;
        }

        sink->setName(block);
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

                if (!axisMap.contains("axis") || !std::holds_alternative<std::string>(axisMap.at("axis"))) {
                    throw gr::exception("invalid axis definition");
                }

                plot.axes.push_back({});
                auto& axisData = plot.axes.back();

                auto axis = std::get<std::string>(axisMap.at("axis"));
                if (axis == "X" || axis == "x") {
                    axisData.axis = Plot::AxisKind::X;
                } else if (axis == "Y" || axis == "y") {
                    axisData.axis = Plot::AxisKind::Y;
                } else {
                    components::Notification::warning(std::format("Unknown axis {}", axis));
                    return;
                }

                axisData.min         = axisMap.contains("min") ? pmtv::cast<float>(axisMap.at("min")) : std::numeric_limits<float>::quiet_NaN();
                axisData.max         = axisMap.contains("max") ? pmtv::cast<float>(axisMap.at("max")) : std::numeric_limits<float>::quiet_NaN();
                std::string scaleStr = axisMap.contains("scale") ? std::get<std::string>(axisMap.at("scale")) : "Linear";
                auto        trim     = [](const std::string& str) {
                    auto start = std::ranges::find_if_not(str, [](unsigned char ch) { return std::isspace(ch); });
                    auto end   = std::ranges::find_if_not(str.rbegin(), str.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
                    return (start < end) ? std::string(start, end) : std::string{};
                };
                axisData.scale = magic_enum::enum_cast<AxisScale>(trim(scaleStr), magic_enum::case_insensitive).value_or(AxisScale::Linear);

                if (axisMap.contains("plot_tags")) {
                    auto tagOpt = axisMap.at("plot_tags");
                    if (auto boolPtr = std::get_if<bool>(&tagOpt)) {
                        axisData.plotTags = *boolPtr;
                    }
                }
            }
        } else {
            // add default axes and ranges if not defined
            plot.axes.push_back({Plot::AxisKind::X});
            plot.axes.push_back({Plot::AxisKind::Y});
        }

        std::transform(plotSources.begin(), plotSources.end(), std::back_inserter(plot.sourceNames), [](const auto& elem) { return std::get<std::string>(elem); });
        plot.window->x      = pmtv::cast<int>(rect[0]);
        plot.window->y      = pmtv::cast<int>(rect[1]);
        plot.window->width  = pmtv::cast<int>(rect[2]);
        plot.window->height = pmtv::cast<int>(rect[3]);
    }

    // if (m_fgItem) {
    // TODO: Port loading and saving flowgraph layouts
    // const bool isGoodString = rootMap.contains("flowgraphLayout") && std::holds_alternative<std::string>(rootMap.at("flowgraphLayout"));
    // }

    loadPlotSources();
}

void Dashboard::save() {
    using namespace gr;

    if (m_desc->storageInfo->isInMemoryDashboardStorage()) {
        return;
    }

    property_map headerYaml;
    headerYaml["favorite"] = m_desc->isFavorite;
    std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(m_desc->lastUsed.value()));
    char                        lastUsed[11];
    std::format_to(lastUsed, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
    headerYaml["lastUsed"] = std::string(lastUsed);

    property_map dashboardYaml;

    std::vector<pmtv::pmt> sources;
    opendigitizer::ImPlotSinkManager::instance().forEach([&](auto& sink) {
        // TODO: Port saving and loading flowgraph layouts
        property_map map;
        map["name"]  = sink.name();
        map["color"] = sink.color();

        sources.emplace_back(std::move(map));
    });
    dashboardYaml["sources"] = sources;

    std::vector<pmtv::pmt> plots;
    for (auto& plot : m_plots) {
        property_map plotMap;
        plotMap["name"] = plot.name;

        std::vector<pmtv::pmt> plotAxes;
        for (const auto& axis : plot.axes) {
            property_map axisMap;
            axisMap["axis"]      = axis.axis == Plot::AxisKind::X ? "X" : "Y";
            axisMap["min"]       = axis.min;
            axisMap["max"]       = axis.max;
            axisMap["scale"]     = std::string(magic_enum::enum_name<AxisScale>(axis.scale));
            axisMap["plot_tags"] = axis.plotTags;
            plotAxes.emplace_back(std::move(axisMap));
        }
        plotMap["axes"] = plotAxes;

        std::vector<pmtv::pmt> plotSinkBlockNames;
        for (auto& plotSinkBlock : plot.plotSinkBlocks) {
            plotSinkBlockNames.emplace_back(plotSinkBlock->name());
        }
        plotMap["sources"] = plotSinkBlockNames;

        std::vector<int> plotRect;
        plotRect.emplace_back(plot.window->x);
        plotRect.emplace_back(plot.window->y);
        plotRect.emplace_back(plot.window->width);
        plotRect.emplace_back(plot.window->height);
        plotMap["rect"] = plotRect;

        plots.emplace_back(plotMap);
    }
    dashboardYaml["plots"] = plots;

    // if (m_fgItem) {
    // TODO: Port loading and saving flowgraph layouts
    // dashboardYaml["flowgraphLayout"] = m_fgItem->settings(&localFlowGraph);
    // }

    if (m_desc->storageInfo->path.starts_with("http://") || m_desc->storageInfo->path.starts_with("https://")) {
        opencmw::client::RestClient client;
        auto                        path = std::filesystem::path(m_desc->storageInfo->path) / m_desc->filename;

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
        fcommand.data.put(stream.str());
        fcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "flowgraph").build();
        client.request(fcommand);
    } else {
#ifndef EMSCRIPTEN
        auto path = std::filesystem::path(m_desc->storageInfo->path);

        std::ofstream stream(path / (m_desc->name + DashboardDescription::fileExtension), std::ios::out | std::ios::trunc);
        if (!stream.is_open()) {
            auto msg = std::format("can't open file for writing");
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
        // auto flowgraphStart = static_cast<std::uint32_t>(stream.tellp());
        // auto flowgraphSize  = static_cast<std::uint32_t>(localFlowGraph.save(stream));
        // stream.seekp(16);
        // stream.write(reinterpret_cast<char*>(&flowgraphStart), 4);
        // stream.write(reinterpret_cast<char*>(&flowgraphSize), 4);
        stream << '\n';
#endif
    }
}

DigitizerUi::Dashboard::Plot& Dashboard::newPlot(int x, int y, int w, int h) {
    m_plots.push_back({});
    auto& p = m_plots.back();
    p.axes.push_back({Plot::AxisKind::X});
    p.axes.push_back({Plot::AxisKind::Y});
    p.window->setGeometry({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(h)});
    return p;
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

void Dashboard::loadPlotSourcesFor(Plot& plot) {
    plot.plotSinkBlocks.clear();

    for (const auto& name : plot.sourceNames) {
        auto plotSource = opendigitizer::ImPlotSinkManager::instance().findSink([&name](const auto& sink) {
            // TODO: We should probably rely on block unique names for sink searching,
            // not 'signal names'
            return sink.signalName() == name || sink.name() == name;
        });
        if (!plotSource) {
            auto msg = std::format("Unable to find plot source -- sink: '{}'", name);
            components::Notification::warning(msg);
            continue;
        }
        plot.plotSinkBlocks.push_back(&*plotSource);
    }
}

void Dashboard::loadPlotSources() {
    for (auto& plot : m_plots) {
        loadPlotSourcesFor(plot);
    }
}

void Dashboard::registerRemoteService(std::string_view blockName, std::optional<opencmw::URI<>> uri) {
    if (!uri) {
        return;
    }

    const auto flowgraphUri = opencmw::URI<>::UriFactory(*uri).path("/flowgraph").setQuery({}).build().str();
    std::print("block {} adds subscription to remote flowgraph service: {} -> {}\n", blockName, uri->str(), flowgraphUri);
    m_flowgraphUriByRemoteSource.insert({std::string{blockName}, flowgraphUri});

    const auto it = std::ranges::find_if(m_services, [&](const auto& s) { return s.uri == flowgraphUri; });
    if (it == m_services.end()) {
        auto msg = std::format("Registering to remote flow graph for '{}' at {}", blockName, flowgraphUri);
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

void Dashboard::addRemoteSignal(const SignalData& signalData) {
    const auto& uriStr    = signalData.uri();
    auto        blockType = [&] {
        opencmw::URI<opencmw::RELAXED> uri{uriStr};
        const auto                     params  = uri.queryParamMap();
        const auto                     acqMode = params.find("acquisitionModeFilter");
        if (acqMode != params.end() && acqMode->second && acqMode->second != "streaming") {
            return "opendigitizer::RemoteDataSetSource";
        }
        return "opendigitizer::RemoteStreamSource";
    }();

    auto blockParams = [&] {
        opencmw::URI<opencmw::RELAXED> uri{uriStr};
        const auto                     params   = uri.queryParamMap();
        const auto                     dataType = params.find("acquisitionDataType");
        if (dataType != params.end() && dataType->second) {
            return *dataType->second;
        }
        return "<float32>"s;
    }();

    gr::Message message;
    message.cmd      = gr::message::Command::Set;
    message.endpoint = gr::graph::property::kEmplaceBlock;
    gr::property_map properties{
        {"remote_uri"s, uriStr},                 //
        {"signal_name"s, signalData.signalName}, //
        {"signal_unit"s, signalData.unit}        //
    }; //
    message.data = gr::property_map{                              //
        {"type"s, std::move(blockType) + std::move(blockParams)}, //
        {"properties"s, std::move(properties)}};

    graphModel().sendMessage(std::move(message));
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

UiGraphModel& Dashboard::graphModel() { return m_graphModel; }

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
    // TODO: Port loading and saving flowgraph layouts
    std::stringstream stream;

    opencmw::client::Command command;
    command.command = opencmw::mdp::Command::Set;
    command.topic   = opencmw::URI<>(s->uri);

    FlowgraphMessage msg;
    msg.flowgraph = std::move(stream).str();
    opencmw::serialise<opencmw::Json>(command.data, msg);
    s->client.request(command);
}

void DashboardDescription::loadAndThen(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, const std::function<void(std::shared_ptr<const DashboardDescription>&&)>& cb) {
    fetch(
        storageInfo, name, {What::Header},
        [cb, name, storageInfo](std::array<std::string, 1>&& desc) {
            const auto yaml = pmtv::yaml::deserialize(desc[0]);
            if (!yaml) {
                throw gr::exception(std::format("Could not parse yaml for DashboardDescription: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
            }
            const gr::property_map& rootMap    = yaml.value();
            bool                    isFavorite = rootMap.contains("favorite") && std::holds_alternative<bool>(rootMap.at("favorite")) && std::get<bool>(rootMap.at("favorite"));

            auto getDate = [](const std::string& str) -> decltype(DashboardDescription::lastUsed) {
                if (str.size() < 10) {
                    return {};
                }
                int      year  = std::atoi(str.data());
                unsigned month = cast_to_unsigned(std::atoi(str.c_str() + 5UZ));
                unsigned day   = cast_to_unsigned(std::atoi(str.c_str() + 8UZ));

                std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
                return std::chrono::sys_days(date);
            };

            auto lastUsed = rootMap.contains("lastUsed") && std::holds_alternative<bool>(rootMap.at("lastUsed")) ? getDate(std::get<std::string>(rootMap.at("lastUsed"))) : std::nullopt;

            cb(std::make_shared<DashboardDescription>(PrivateTag{}, std::filesystem::path(name).stem().native(), storageInfo, name, isFavorite, lastUsed));
        },
        [cb]() { cb({}); });
}

std::shared_ptr<const DashboardDescription> DashboardDescription::createEmpty(const std::string& name) { return std::make_shared<DashboardDescription>(PrivateTag{}, name, DashboardStorageInfo::memoryDashboardStorage(), std::string{}, false, std::nullopt); }
} // namespace DigitizerUi
