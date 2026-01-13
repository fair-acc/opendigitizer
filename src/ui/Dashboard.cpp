#include "Dashboard.hpp"

#include <algorithm>
#include <fstream>
#include <print>
#include <ranges>

#include <format>

#include "common/Events.hpp"
#include "common/ImguiWrap.hpp"

#include <implot.h>

#include <opencmw.hpp>

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <daq_api.hpp>

#include "App.hpp"
#include "GraphModel.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"
#include "components/SignalSelector.hpp"

#include "charts/Charts.hpp"
#include "charts/SinkRegistry.hpp"
#include "conversion.hpp"

using namespace std::string_literals;

struct FlowgraphMessage {
    std::string flowgraph;
    std::string layout;
};

ENABLE_REFLECTION_FOR(FlowgraphMessage, flowgraph, layout)

namespace DigitizerUi {

namespace {
enum class What { Header, Flowgraph };

template<typename T>
struct arrsize;

template<typename T, std::size_t N>
struct arrsize<T const (&)[N]> {
    static constexpr auto size = N;
};

template<std::size_t N>
auto fetch(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, What const (&what)[N], std::function<void(std::array<std::string, arrsize<decltype(what)>::size>&&)>&& cb, std::function<void()>&& errCb) {
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

        client->request(command);
        return;
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
    } else if (storageInfo->path.starts_with("example://")) {
        std::array<std::string, N> reply;
        auto                       fs = cmrc::sample_dashboards::get_filesystem();
        for (std::size_t i = 0UZ; i < N; ++i) {
            reply[i] = [&]() -> std::string {
                switch (what[i]) {
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
#endif
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
                stream.seekg(w == What::Header ? 0 : 16);

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

Dashboard::Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> restClient, const std::shared_ptr<const DashboardDescription>& desc) : m_restClient(std::move(restClient)), m_desc(desc) {
    m_desc->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

    const auto style = Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light;
    switch (style) {
    case LookAndFeel::Style::Dark: ImGui::StyleColorsDark(); break;
    case LookAndFeel::Style::Light: ImGui::StyleColorsLight(); break;
    }
    LookAndFeel::mutableInstance().style         = style;
    ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    m_graphModel.sendMessage_ = [this](gr::Message message, std::source_location location) { m_scheduler.sendMessage(std::move(message), std::move(location)); };
}

Dashboard::~Dashboard() {}

std::unique_ptr<Dashboard> Dashboard::create(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc) { return std::make_unique<Dashboard>(PrivateTag{}, client, desc); }

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { m_desc = desc; }

void Dashboard::load() {
    if (!m_desc->storageInfo->isInMemoryDashboardStorage()) {
        m_isInitialised.store(false, std::memory_order_release);
        isInUse = true;

        fetch(
            m_restClient, m_desc->storageInfo, m_desc->filename, {What::Flowgraph}, //
            [this](std::array<std::string, 1>&& data) {
                //
                loadAndThen(std::move(data[0]), [this](gr::Graph&& graph) { m_scheduler.emplaceGraph(std::move(graph)); });
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

void Dashboard::loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler) {
    try {
        const auto yaml = pmtv::yaml::deserialize(grcData);
        if (!yaml) {
            throw gr::exception(std::format("Could not parse yaml: {}:{}\n{}", yaml.error().message, yaml.error().line, grcData));
        }
        const gr::property_map& rootMap = yaml.value();

        gr::Graph grGraph = [this, &rootMap]() -> gr::Graph {
            try {
                gr::Graph resultGraph;
                gr::detail::loadGraphFromMap(*pluginLoader, resultGraph, rootMap);
                return resultGraph;
            } catch (const gr::exception& e) {
                throw;
            } catch (const std::string& e) {
                throw gr::exception(e);
            } catch (...) {
                throw;
            }
        }();

        if (const auto dashboardUri = opencmw::URI<>(std::string(m_desc->storageInfo->path)); dashboardUri.hostName().has_value()) {
            const auto remoteUri = dashboardUri.factory().hostName(*dashboardUri.hostName()).port(dashboardUri.port().value_or(8080)).scheme(dashboardUri.scheme().value_or("https")).build();
            gr::graph::forEachBlock<gr::block::Category::NormalBlock>(grGraph, [&remoteUri, this](auto& block) {
                if (block->typeName().starts_with("opendigitizer::RemoteStreamSource") || block->typeName().starts_with("opendigitizer::RemoteDataSetSource")) {
                    auto* sourceBlock = static_cast<opendigitizer::RemoteSourceBase*>(block->raw());
                    sourceBlock->host = remoteUri.str();
                }
            });
        }

        assignScheduler(std::move(grGraph));

        // Load is called after parsing the flowgraph so that we already have the list of sources
        const auto dashboard = std::get<gr::property_map>(rootMap.at("dashboard"));
        doLoad(dashboard);
        m_isInitialised.store(true, std::memory_order_release);
    } catch (const gr::exception& e) {
#ifndef NDEBUG
        std::println(stderr, "Dashboard::load(const std::string& grcData): error: {}", e);
#endif
        components::Notification::error(std::format("Error: {}", e.what()));
        if (requestClose) {
            requestClose(this);
        }
    } catch (const std::exception& e) {
#ifndef NDEBUG
        std::println(stderr, "Dashboard::load(const std::string& grcData): error: {}", e.what());
#endif
        components::Notification::error(std::format("Error: {}", e.what()));
        if (requestClose) {
            requestClose(this);
        }
    } catch (...) {
        components::Notification::error(std::format("Error: {}", "Unkonwn exception"));
        if (requestClose) {
            requestClose(this);
        }
    }
}

void Dashboard::doLoad(const gr::property_map& dashboard) {
    using namespace gr;
    auto path = std::filesystem::path(m_desc->storageInfo->path) / m_desc->filename;

    if (dashboard.contains("layout") && std::holds_alternative<std::string>(dashboard.at("layout"))) {
        m_layout = magic_enum::enum_cast<DockingLayoutType>(std::get<std::string>(dashboard.at("layout")), magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
    }

    if (!dashboard.contains("sources") || !std::holds_alternative<std::vector<pmtv::pmt>>(dashboard.at("sources"))) {
        throw gr::exception("sources entry invalid");
    }
    auto sources = std::get<std::vector<pmtv::pmt>>(dashboard.at("sources"));

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

        auto sinkPtr = opendigitizer::charts::SinkRegistry::instance().findSink([&](const auto& s) { return s.name() == block; });
        if (!sinkPtr) {
            auto msg = std::format("Unable to find the plot source -- sink: '{}'", block);
            components::Notification::warning(msg);
            continue;
        }

        // Signal name is set via block properties during initialization
        // SignalSink interface is read-only for name
    }

    if (!dashboard.contains("plots") || !std::holds_alternative<std::vector<pmtv::pmt>>(dashboard.at("plots"))) {
        throw gr::exception("plots entry invalid");
    }
    auto plots = std::get<std::vector<pmtv::pmt>>(dashboard.at("plots"));

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

        std::string chartTypeName = "XYChart";
        if (plotMap.contains("type") && std::holds_alternative<std::string>(plotMap.at("type"))) {
            chartTypeName = std::get<std::string>(plotMap.at("type"));
        }

        // Parse chart parameters from .grc (e.g., show_legend, show_grid, etc.)
        gr::property_map chartParameters;
        if (plotMap.contains("parameters") && std::holds_alternative<property_map>(plotMap.at("parameters"))) {
            chartParameters = std::get<property_map>(plotMap.at("parameters"));
        }

        // TODO(deprecated): Transfer 'sources:' to 'data_sinks' for backward compatibility with older .grc files.
        // The 'sources:' section in .grc will eventually be deprecated. Charts should define their signals
        // directly via the 'data_sinks' field in the 'parameters:' section. Once all .grc files are migrated,
        // this transfer code and the 'sources:' parsing above can be removed.
        std::vector<std::string> dataSinks;
        std::transform(plotSources.begin(), plotSources.end(), std::back_inserter(dataSinks), [](const auto& elem) { return std::get<std::string>(elem); });
        chartParameters["data_sinks"] = dataSinks;

        // Parse axes config (will be set on block's uiConstraints after creation)
        std::vector<pmtv::pmt> axesConfig;
        if (plotMap.contains("axes") && std::holds_alternative<std::vector<pmtv::pmt>>(plotMap.at("axes"))) {
            axesConfig = std::get<std::vector<pmtv::pmt>>(plotMap.at("axes"));
        }

        // Create chart block in UI Graph with parameters
        auto [blockPtr, chartPtr] = emplaceChartBlock(chartTypeName, name, chartParameters);

        if (!blockPtr) {
            components::Notification::warning(std::format("Failed to create chart block of type '{}'", chartTypeName));
            continue;
        }

        // Set uiConstraints on the block (axes config and window layout for chart to read in draw())
        blockPtr->uiConstraints()["axes"] = axesConfig;
        blockPtr->uiConstraints()["window"] = gr::property_map{
            {"x", pmtv::cast<std::size_t>(rect[0])},
            {"y", pmtv::cast<std::size_t>(rect[1])},
            {"width", pmtv::cast<std::size_t>(rect[2])},
            {"height", pmtv::cast<std::size_t>(rect[3])}
        };

        // Find the shared_ptr for this block in uiGraph
        std::shared_ptr<gr::BlockModel> blockShared;
        for (auto& blk : m_uiGraph.blocks()) {
            if (blk.get() == blockPtr) {
                blockShared = blk;
                break;
            }
        }

        // Create UIWindow for this chart (sink management done via settings interface)
        UIWindow uiWindow(blockShared, name);
        if (uiWindow.window) {
            uiWindow.window->x      = pmtv::cast<std::size_t>(rect[0]);
            uiWindow.window->y      = pmtv::cast<std::size_t>(rect[1]);
            uiWindow.window->width  = pmtv::cast<std::size_t>(rect[2]);
            uiWindow.window->height = pmtv::cast<std::size_t>(rect[3]);
        }

        m_uiWindows.push_back(std::move(uiWindow));
    }

    // if (m_fgItem) {
    // TODO: Port loading and saving flowgraph layouts
    // const bool isGoodString = dashboard.contains("flowgraphLayout") && std::holds_alternative<std::string>(dashboard.at("flowgraphLayout"));
    // }

    // Load sinks for all UIWindows (sinks should be registered in SinkRegistry by now)
    loadUIWindowSources();
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

    property_map dashboardYaml = gr::detail::saveGraphToMap(*pluginLoader, scheduler()->graph());

    std::vector<pmtv::pmt> sources;
    // Use SinkRegistry with SignalSink interface for serialization
    opendigitizer::charts::SinkRegistry::instance().forEach([&](const opendigitizer::charts::SignalSink& sink) {
        // TODO: Port saving and loading flowgraph layouts
        property_map map;
        map["name"]  = std::string(sink.name());
        map["color"] = sink.color();

        sources.emplace_back(std::move(map));
    });
    dashboardYaml["sources"] = sources;

    std::vector<pmtv::pmt> plots;
    // Iterate UIWindows for chart serialization (new API)
    for (const auto& uiWindow : m_uiWindows) {
        if (!uiWindow.isChart() || !uiWindow.block) {
            continue;
        }

        property_map plotMap;
        plotMap["name"] = uiWindow.window ? uiWindow.window->name : std::string(uiWindow.block->uniqueName());
        // Extract short chart type name from fully-qualified name (e.g., "opendigitizer::charts::XYChart" -> "XYChart")
        std::string fullTypeName = std::string(uiWindow.block->typeName());
        if (auto pos = fullTypeName.rfind("::"); pos != std::string::npos) {
            plotMap["type"] = fullTypeName.substr(pos + 2);
        } else {
            plotMap["type"] = fullTypeName;
        }

        // Serialize axes from uiConstraints
        std::vector<pmtv::pmt> plotAxes;
        const auto&            constraints = uiWindow.block->uiConstraints();
        if (constraints.contains("axes") && std::holds_alternative<std::vector<pmtv::pmt>>(constraints.at("axes"))) {
            plotAxes = std::get<std::vector<pmtv::pmt>>(constraints.at("axes"));
        }
        plotMap["axes"] = plotAxes;

        // Serialize signal sinks from data_sinks property
        std::vector<pmtv::pmt> plotSinkBlockNames;
        for (const auto& sinkName : uiWindow.sinkNames()) {
            plotSinkBlockNames.emplace_back(sinkName);
        }
        plotMap["sources"] = plotSinkBlockNames;

        // Serialize window rect from uiConstraints or DockSpace::Window
        std::vector<int> plotRect;
        if (constraints.contains("window") && std::holds_alternative<property_map>(constraints.at("window"))) {
            const auto& windowMap = std::get<property_map>(constraints.at("window"));
            plotRect.emplace_back(static_cast<int>(pmtv::cast<std::size_t>(windowMap.at("x"))));
            plotRect.emplace_back(static_cast<int>(pmtv::cast<std::size_t>(windowMap.at("y"))));
            plotRect.emplace_back(static_cast<int>(pmtv::cast<std::size_t>(windowMap.at("width"))));
            plotRect.emplace_back(static_cast<int>(pmtv::cast<std::size_t>(windowMap.at("height"))));
        } else if (uiWindow.window) {
            plotRect.emplace_back(uiWindow.window->x);
            plotRect.emplace_back(uiWindow.window->y);
            plotRect.emplace_back(uiWindow.window->width);
            plotRect.emplace_back(uiWindow.window->height);
        } else {
            plotRect = {0, 0, 1, 1}; // Default
        }
        plotMap["rect"] = plotRect;

        plots.emplace_back(plotMap);
    }
    dashboardYaml["plots"] = plots;

    // if (m_fgItem) {
    // TODO: Port loading and saving flowgraph layouts
    // dashboardYaml["flowgraphLayout"] = m_fgItem->settings(&localFlowGraph);
    // }

    if (m_desc->storageInfo->path.starts_with("http://") || m_desc->storageInfo->path.starts_with("https://")) {
        auto path = std::filesystem::path(m_desc->storageInfo->path) / m_desc->filename;

        opencmw::client::Command hcommand;
        hcommand.command          = opencmw::mdp::Command::Set;
        std::string headerYamlStr = pmtv::yaml::serialize(headerYaml);
        hcommand.data.put(std::string_view(headerYamlStr.c_str(), headerYamlStr.size()));
        hcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "header").build();
        m_restClient->request(hcommand);

        opencmw::client::Command dcommand;
        dcommand.command             = opencmw::mdp::Command::Set;
        std::string dashboardYamlStr = pmtv::yaml::serialize(dashboardYaml);
        dcommand.data.put(std::string_view(dashboardYamlStr.c_str(), dashboardYamlStr.size()));
        dcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "dashboard").build();
        m_restClient->request(dcommand);

        opencmw::client::Command fcommand;
        fcommand.command = opencmw::mdp::Command::Set;
        std::stringstream stream;
        fcommand.data.put(stream.str());
        fcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "flowgraph").build();
        m_restClient->request(fcommand);
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

DigitizerUi::Dashboard::UIWindow& Dashboard::newChart(int x, int y, int w, int h, std::string_view chartType) {
    std::string chartTypeName = chartType.empty() ? "XYChart" : std::string(chartType);

    // Generate a unique name for the chart
    static int chartCounter = 1;
    std::string chartName = std::format("Chart {}", chartCounter++);

    // Create chart block in UI Graph
    auto [blockPtr, chartPtr] = emplaceChartBlock(chartTypeName, chartName);

    if (!blockPtr) {
        // Return a reference to an empty UIWindow on failure (shouldn't happen with known types)
        static UIWindow emptyWindow;
        return emptyWindow;
    }

    // Store window layout and default axes in uiConstraints
    blockPtr->uiConstraints()["window"] = gr::property_map{
        {"x", static_cast<std::size_t>(x)},
        {"y", static_cast<std::size_t>(y)},
        {"width", static_cast<std::size_t>(w)},
        {"height", static_cast<std::size_t>(h)}
    };

    // Find the shared_ptr for this block in uiGraph
    std::shared_ptr<gr::BlockModel> blockShared;
    for (auto& blk : m_uiGraph.blocks()) {
        if (blk.get() == blockPtr) {
            blockShared = blk;
            break;
        }
    }

    // Create UIWindow for this chart
    UIWindow uiWindow(blockShared, chartName);
    if (uiWindow.window) {
        uiWindow.window->setGeometry({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(h)});
    }
    m_uiWindows.push_back(std::move(uiWindow));

    return m_uiWindows.back();
}

DigitizerUi::Dashboard::Plot& Dashboard::newPlot(int x, int y, int w, int h, std::string_view chartType) {
    // Legacy wrapper - creates a UIWindow and returns a placeholder Plot
    // TODO: Remove this once all callers are migrated to newChart()
    auto& uiWindow = newChart(x, y, w, h, chartType);

    // Create a legacy Plot entry for backward compatibility
    m_plots.push_back({});
    auto& p = m_plots.back();
    if (uiWindow.window) {
        p.name = uiWindow.window->name;
    }
    // Extract short chart type name from block's fully-qualified type name
    std::string fullTypeName = std::string(uiWindow.block->typeName());
    if (auto pos = fullTypeName.rfind("::"); pos != std::string::npos) {
        p.chartTypeName = fullTypeName.substr(pos + 2);
    } else {
        p.chartTypeName = fullTypeName;
    }
    p.chartBlock = uiWindow.block.get();
    p.chart      = nullptr; // Legacy: Chart* is no longer maintained via UIWindow
    p.axes.push_back({AxisConfig::AxisKind::X});
    p.axes.push_back({AxisConfig::AxisKind::Y});
    p.window->setGeometry({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(h)});

    return p;
}

void Dashboard::deletePlot(Plot* plot) {
    if (!plot) {
        return;
    }
    // Also remove the corresponding UIWindow if it exists
    if (plot->chartBlock) {
        // Find and remove UIWindow with matching block
        std::erase_if(m_uiWindows, [plot](const UIWindow& w) {
            return w.block && w.block.get() == plot->chartBlock;
        });
        // Remove block from UI graph
        m_uiGraph.removeBlockByName(plot->chartBlock->uniqueName());
    }
    auto it = std::find_if(m_plots.begin(), m_plots.end(), [=](const Plot& p) { return plot == &p; });
    if (it != m_plots.end()) {
        m_plots.erase(it);
    }
}

void Dashboard::deleteChart(UIWindow* uiWindow) {
    if (!uiWindow || !uiWindow->block) {
        return;
    }
    // Remove block from UI graph
    m_uiGraph.removeBlockByName(uiWindow->block->uniqueName());
    // Remove the UIWindow
    std::erase_if(m_uiWindows, [uiWindow](const UIWindow& w) { return &w == uiWindow; });
}

bool Dashboard::transmuteChart(Plot& plot, std::string_view newChartType) {
    using namespace opendigitizer::charts;

    // Skip if same type
    if (plot.chartTypeName == newChartType) {
        return true;
    }

    // Save current state from old chart
    std::vector<std::shared_ptr<SignalSink>> savedSinks;
    gr::property_map                         savedUiConstraints;
    std::string                              oldUniqueName;

    if (plot.hasChart()) {
        // Save signal sinks
        for (const auto& sink : plot.chart->signalSinks()) {
            savedSinks.push_back(sink);
        }
        // Save uiConstraints (axes config, window layout)
        savedUiConstraints = plot.chartBlock->uiConstraints();
        oldUniqueName      = std::string(plot.chart->uniqueId());
    }

    // Remove old block from UI graph
    if (!oldUniqueName.empty()) {
        m_uiGraph.removeBlockByName(oldUniqueName);
    }

    // Create new chart block
    gr::property_map chartParameters;
    // Convert saved sinks to data_sinks property
    std::vector<std::string> dataSinks;
    for (const auto& sink : savedSinks) {
        if (sink) {
            dataSinks.push_back(std::string(sink->name()));
        }
    }
    if (!dataSinks.empty()) {
        chartParameters["data_sinks"] = dataSinks;
    }

    auto [blockPtr, chartPtr] = emplaceChartBlock(newChartType, plot.name, chartParameters);
    if (!blockPtr || !chartPtr) {
        // Transmutation failed - unknown chart type
        return false;
    }

    // Restore uiConstraints
    blockPtr->uiConstraints() = savedUiConstraints;

    // Note: Signal sinks are transferred via the data_sinks property which triggers
    // settingsChanged -> syncSinksFromNames. We don't manually add them here to avoid
    // duplicates which can cause deadlocks when acquiring data locks.

    // Update plot references
    plot.chartTypeName = std::string(newChartType);
    plot.chartBlock    = blockPtr;
    plot.chart         = chartPtr;

    return true;
}

bool Dashboard::transmuteUIWindow(UIWindow& uiWindow, std::string_view newChartType) {
    if (!uiWindow.block) {
        return false;
    }

    // Get current chart type name from block's fully-qualified type name
    std::string fullTypeName = std::string(uiWindow.block->typeName());
    std::string currentTypeName;
    if (auto pos = fullTypeName.rfind("::"); pos != std::string::npos) {
        currentTypeName = fullTypeName.substr(pos + 2);
    } else {
        currentTypeName = fullTypeName;
    }

    // Skip if same type
    if (currentTypeName == newChartType) {
        return true;
    }

    // Save current state
    std::vector<std::string> savedSinkNames    = uiWindow.sinkNames();
    gr::property_map         savedUiConstraints = uiWindow.block->uiConstraints();
    std::string              oldUniqueName     = std::string(uiWindow.block->uniqueName());
    std::string              windowName        = uiWindow.window ? uiWindow.window->name : oldUniqueName;

    // Remove old block from UI graph
    m_uiGraph.removeBlockByName(oldUniqueName);

    // Create new chart block with preserved sink names
    gr::property_map chartParameters;
    if (!savedSinkNames.empty()) {
        chartParameters["data_sinks"] = savedSinkNames;
    }

    auto [blockPtr, chartPtr] = emplaceChartBlock(newChartType, windowName, chartParameters);
    if (!blockPtr) {
        // Transmutation failed - unknown chart type
        return false;
    }

    // Restore uiConstraints
    blockPtr->uiConstraints() = savedUiConstraints;

    // Find the shared_ptr for the new block in uiGraph
    std::shared_ptr<gr::BlockModel> newBlockShared;
    for (auto& blk : m_uiGraph.blocks()) {
        if (blk.get() == blockPtr) {
            newBlockShared = blk;
            break;
        }
    }

    // Update UIWindow block reference
    uiWindow.block = newBlockShared;

    return true;
}

void Dashboard::removeSinkFromPlots(std::string_view sinkName) {
    // Remove from UIWindows via settings interface
    for (auto& uiWindow : m_uiWindows) {
        if (uiWindow.isChart()) {
            auto names = uiWindow.sinkNames();
            std::erase(names, std::string(sinkName));
            uiWindow.setSinkNames(names);
        }
    }
    // Remove UIWindows with no sinks
    std::erase_if(m_uiWindows, [](const UIWindow& w) {
        return w.isChart() && w.sinkNames().empty();
    });

    // Legacy: Also update m_plots (for backward compatibility during transition)
    for (auto& plot : m_plots) {
        std::erase(plot.sourceNames, std::string(sinkName));
    }
    std::erase_if(m_plots, [](const Plot& p) { return p.sourceNames.empty(); });
}

void Dashboard::loadPlotSourcesFor(Plot& plot) {
    // Clear existing sinks from chart
    if (plot.hasChart()) {
        plot.clearChartSinks();
    }

    for (const auto& name : plot.sourceNames) {
        // Find sink in SinkRegistry for proper shared ownership
        auto sinkSharedPtr = opendigitizer::charts::SinkRegistry::instance().findSink([&name](const auto& sink) { return sink.signalName() == name || sink.name() == name; });

        if (sinkSharedPtr) {
            // Add to chart with shared ownership
            if (plot.hasChart()) {
                plot.chart->addSignalSink(sinkSharedPtr);
            }
            continue;
        }

        // Sink not found in SinkRegistry
        auto msg = std::format("Unable to find plot source -- sink: '{}'", name);
        components::Notification::warning(msg);
    }
}

void Dashboard::loadPlotSources() {
    for (auto& plot : m_plots) {
        loadPlotSourcesFor(plot);
    }
}

void Dashboard::loadUIWindowSources() {
    std::println("[Dashboard::loadUIWindowSources] called with {} UIWindows, SinkRegistry has {} sinks",
                 m_uiWindows.size(), opendigitizer::charts::SinkRegistry::instance().sinkCount());

    for (auto& uiWindow : m_uiWindows) {
        if (!uiWindow.isChart() || !uiWindow.block) {
            continue;
        }

        // Re-set the data_sinks property to trigger settingsChanged() which calls syncSinksFromNames()
        auto sinkNames = uiWindow.sinkNames();
        std::println("[Dashboard::loadUIWindowSources] UIWindow '{}' has {} sinkNames",
                     uiWindow.block->uniqueName(), sinkNames.size());
        if (!sinkNames.empty()) {
            uiWindow.setSinkNames(sinkNames);
        }
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
        auto& s = *m_services.emplace(m_restClient, flowgraphUri, flowgraphUri);
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
    message.endpoint = gr::scheduler::property::kEmplaceBlock;

    // We can add remote signals only to the root block. And the root block
    // has to be a scheduler
    message.serviceName = graphModel().rootBlock.ownerSchedulerUniqueName();
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
    restClient->request(command);
}

void Dashboard::Service::emplaceBlock(std::string type, std::string params) {
    gr::Message message;
    message.cmd      = gr::message::Command::Set;
    message.endpoint = gr::scheduler::property::kEmplaceBlock;
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
    restClient->request(command);
}

UiGraphModel& Dashboard::graphModel() { return m_graphModel; }

std::pair<gr::BlockModel*, opendigitizer::charts::Chart*> Dashboard::emplaceChartBlock(std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters) {
    using namespace opendigitizer::charts;

    // Handle both short names ("XYChart") and fully-qualified names ("opendigitizer::charts::XYChart")
    auto matchesType = [](std::string_view fullName, std::string_view shortName) {
        if (fullName == shortName) {
            return true;
        }
        const std::string suffix = std::string("::") + std::string(shortName);
        return fullName.size() > suffix.size() && fullName.substr(fullName.size() - suffix.size()) == suffix;
    };

    // Build initParams: start with chart name, then merge any additional parameters
    gr::property_map initParams;
    if (!chartName.empty()) {
        initParams["chart_name"] = chartName;
    }
    // Merge chart parameters (from .grc parameters: section)
    for (const auto& [key, value] : chartParameters) {
        initParams[key] = value;
    }

    if (matchesType(chartTypeName, "XYChart")) {
        auto& chart = m_uiGraph.emplaceBlock<XYChart>(initParams);
        // Get BlockModel* from the last added block (emplaceBlock adds to back)
        gr::BlockModel* blockModel = m_uiGraph.blocks().back().get();
        return {blockModel, static_cast<Chart*>(&chart)};
    }
    if (matchesType(chartTypeName, "YYChart")) {
        auto&           chart      = m_uiGraph.emplaceBlock<YYChart>(initParams);
        gr::BlockModel* blockModel = m_uiGraph.blocks().back().get();
        return {blockModel, static_cast<Chart*>(&chart)};
    }

    return {nullptr, nullptr};
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
    restClient->request(command);
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
    s->restClient->request(command);
}

void DashboardDescription::loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, const std::function<void(std::shared_ptr<const DashboardDescription>&&)>& cb) {
    fetch(
        client, storageInfo, name, {What::Header},
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
