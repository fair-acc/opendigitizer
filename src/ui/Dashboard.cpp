#include "Dashboard.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <print>
#include <ranges>

#include <format>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>

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
                    std::size_t size = 0;
                    std::from_chars(s, s + p, size);
                    s += p + 1; // the +1 is for the ';'

                    reply[i].resize(size);
                    std::copy_n(s, size, reply[i].data());
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
    auto it = std::ranges::find_if(knownDashboardStorage(), [=](const auto& s) {
        auto p = s.lock();
        return p && p->path == path;
    });
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

Dashboard::Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc) : restClient(std::move(client)), description(desc) {
    description->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

    const auto style = Digitizer::Settings::instance().darkMode ? LookAndFeel::Style::Dark : LookAndFeel::Style::Light;
    switch (style) {
    case LookAndFeel::Style::Dark: ImGui::StyleColorsDark(); break;
    case LookAndFeel::Style::Light: ImGui::StyleColorsLight(); break;
    }
    LookAndFeel::mutableInstance().style         = style;
    ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

    graphModel.sendMessage_ = [this](gr::Message message, std::source_location location) { scheduler.sendMessage(std::move(message), std::move(location)); };

    // Create the GlobalSignalLegend in uiGraph (Toolbar block showing all sinks)
    uiGraph.emplaceBlock("DigitizerUi::GlobalSignalLegend", {});
}

Dashboard::~Dashboard() {}

std::unique_ptr<Dashboard> Dashboard::create(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc) { return std::make_unique<Dashboard>(PrivateTag{}, client, desc); }

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { description = desc; }

void Dashboard::load() {
    if (!description->storageInfo->isInMemoryDashboardStorage()) {
        isInitialised.store(false, std::memory_order_release);
        isInUse = true;

        fetch(
            restClient, description->storageInfo, description->filename, {What::Flowgraph}, //
            [this](std::array<std::string, 1>&& data) {
                //
                loadAndThen(std::move(data[0]), [this](gr::Graph&& graph) { scheduler.emplaceGraph(std::move(graph)); });
                isInUse = false;
            },
            [this]() {
                auto error = std::format("Invalid flowgraph for dashboard {}/{}", description->storageInfo->path, description->filename);
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
        const auto yaml = gr::pmt::yaml::deserialize(grcData);
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

        if (const auto dashboardUri = opencmw::URI<>(std::string(description->storageInfo->path)); dashboardUri.hostName().has_value()) {
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
        if (const auto* dashboard = rootMap.at("dashboard").get_if<gr::property_map>()) {
            doLoad(*dashboard);
        } else {
            throw gr::exception("dashboard field is not a property_map");
        }
        isInitialised.store(true, std::memory_order_release);
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
    auto path = std::filesystem::path(description->storageInfo->path) / description->filename;

    const auto readField = []<typename T>(const gr::property_map& m, std::string_view key, bool required = true) -> std::optional<T> {
        auto it = m.find(std::pmr::string(key));
        if (it == m.end()) {
            if (!required) {
                return std::nullopt;
            }
            throw gr::exception(std::format("Missing required key '{}'", key));
        }

        if constexpr (std::same_as<T, std::string>) {
            if (!it->second.is_string()) {
                throw gr::exception(std::format("Key '{}' must be string", key));
            }
            return it->second.value_or(std::string{});
        } else {
            if (const auto* p = it->second.get_if<T>()) {
                return *p;
            }

            std::string actualType = "<unknown>";
            pmt::ValueVisitor([&actualType]<typename T0>(const T0& /*arg*/) { actualType = gr::meta::type_name<std::decay_t<T0>>(); }).visit(it->second);
            throw gr::exception(std::format("Key '{}' has invalid type: expected '{}', got '{}'", key, gr::meta::type_name<T>(), actualType));
        }
    };

    if (dashboard.contains("layout") && dashboard.at("layout").is_string()) {
        layout = magic_enum::enum_cast<DockingLayoutType>(dashboard.at("layout").value_or(std::string()), magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
    }

    auto sources = *readField.operator()<Tensor<pmt::Value>>(dashboard, "sources");

    for (const auto& src : sources) {
        if (!src.holds<property_map>()) {
            throw gr::exception("source is not a property_map");
        }
        const property_map srcMap = src.value_or(property_map{});

        const auto block = *readField.operator()<std::string>(srcMap, "block");
        const auto name  = *readField.operator()<std::string>(srcMap, "name");

        auto sinkPtr = opendigitizer::charts::SinkRegistry::instance().findSink([&](const auto& s) { return s.name() == block; });
        if (!sinkPtr) {
            auto msg = std::format("Unable to find the plot source -- sink: '{}'", block);
            components::Notification::warning(msg);
            continue;
        }

        // Signal name is set via block properties during initialization
        // SignalSink interface is read-only for name
    }

    const auto plots = *readField.operator()<Tensor<pmt::Value>>(dashboard, "plots");

    for (const auto& plotPmt : plots) {
        if (!plotPmt.holds<property_map>()) {
            throw gr::exception("plot is not a property_map");
        }
        const property_map plotMap = plotPmt.value_or(property_map{});

        const auto name        = *readField.operator()<std::string>(plotMap, "name");
        const auto plotSources = *readField.operator()<Tensor<pmt::Value>>(plotMap, "sources");
        const auto rect        = *readField.operator()<Tensor<pmt::Value>>(plotMap, "rect");
        if (rect.size() != 4) {
            throw gr::exception("invalid plot definition rect.size() != 4");
        }

        std::string chartTypeName = "XYChart";
        if (const auto type = readField.operator()<std::string>(plotMap, "type", false); type) {
            chartTypeName = *type;
        }

        // Parse chart parameters from .grc (e.g., show_legend, show_grid, etc.)
        gr::property_map chartParameters;
        if (const auto parameters = readField.operator()<property_map>(plotMap, "parameters", false); parameters) {
            chartParameters = *parameters;
        }

        // Transfer 'sources:' to 'data_sinks' (see grc_compat namespace in Dashboard.hpp)
        gr::Tensor<gr::pmt::Value> dataSinksTensor(gr::extents_from, {plotSources.size()});
        for (std::size_t i = 0; i < plotSources.size(); ++i) {
            if (!plotSources[i].is_string()) {
                throw gr::exception("plot.sources elements must be strings");
            }
            dataSinksTensor[i] = plotSources[i].value_or(std::string());
        }
        chartParameters[std::pmr::string("data_sinks")] = std::move(dataSinksTensor);

        // Parse axes config (will be set on block's uiConstraints after creation)
        gr::Tensor<gr::pmt::Value> axesConfig;
        if (const auto axes = readField.operator()<gr::Tensor<gr::pmt::Value>>(plotMap, "axes", false); axes) {
            axesConfig = *axes;
        }

        // Create chart block in UI Graph with parameters
        auto* blockPtr = emplaceChartBlock(chartTypeName, name, chartParameters);

        if (!blockPtr) {
            components::Notification::warning(std::format("Failed to create chart block of type '{}'", chartTypeName));
            continue;
        }

        // Set uiConstraints on the block (axes config and window layout for chart to read in draw())
        blockPtr->uiConstraints()["axes"] = axesConfig;
        auto rectValue                    = [](const gr::pmt::Value& v) -> std::uint64_t {
            if (auto converted = gr::pmt::convert_safely<std::uint64_t>(v); converted) {
                return *converted;
            }
            return 0ULL;
        };
        blockPtr->uiConstraints()["window"] = gr::property_map{{"x", rectValue(rect[0])}, {"y", rectValue(rect[1])}, {"width", rectValue(rect[2])}, {"height", rectValue(rect[3])}};

        // Find the shared_ptr for this block in uiGraph
        std::shared_ptr<gr::BlockModel> blockShared;
        for (auto& blk : uiGraph.blocks()) {
            if (blk.get() == blockPtr) {
                blockShared = blk;
                break;
            }
        }

        // Create UIWindow for this chart (sink management done via settings interface)
        UIWindow uiWindow(blockShared, name);
        if (uiWindow.window) {
            uiWindow.window->x      = rectValue(rect[0]);
            uiWindow.window->y      = rectValue(rect[1]);
            uiWindow.window->width  = rectValue(rect[2]);
            uiWindow.window->height = rectValue(rect[3]);
        }

        uiWindows.push_back(std::move(uiWindow));
    }

    // Load sinks for all UIWindows (sinks should be registered in SinkRegistry by now)
    loadUIWindowSources();
}

void Dashboard::save() {
    using namespace gr;

    if (description->storageInfo->isInMemoryDashboardStorage()) {
        return;
    }

    property_map headerYaml;
    headerYaml["favorite"] = description->isFavorite;
    std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(description->lastUsed.value()));
    char                        lastUsed[11];
    std::format_to(lastUsed, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
    headerYaml["lastUsed"] = std::string(lastUsed);

    property_map dashboardYaml = gr::detail::saveGraphToMap(*pluginLoader, scheduler->graph());

    gr::Tensor<gr::pmt::Value> sources;
    // Use SinkRegistry with SignalSink interface for serialization
    opendigitizer::charts::SinkRegistry::instance().forEach([&](const opendigitizer::charts::SignalSink& sink) {
        // TODO: Port saving and loading flowgraph layouts
        property_map map;
        map["name"]  = std::string(sink.name());
        map["color"] = sink.color();

        sources.emplace_back(std::move(map));
    });
    dashboardYaml["sources"] = sources;

    gr::Tensor<gr::pmt::Value> plots;
    // Iterate UIWindows for chart serialization (new API)
    for (const auto& w : uiWindows) {
        if (!w.isChart() || !w.block) {
            continue;
        }

        property_map plotMap;
        plotMap["name"] = w.window ? w.window->name : std::string(w.block->uniqueName());
        // Extract short chart type name from fully-qualified name (e.g., "opendigitizer::charts::XYChart" -> "XYChart")
        std::string fullTypeName = std::string(w.block->typeName());
        if (auto pos = fullTypeName.rfind("::"); pos != std::string::npos) {
            plotMap["type"] = fullTypeName.substr(pos + 2);
        } else {
            plotMap["type"] = fullTypeName;
        }

        // Serialize axes from uiConstraints
        gr::Tensor<gr::pmt::Value> plotAxes;
        const auto&                constraints = w.block->uiConstraints();
        if (constraints.contains("axes")) {
            plotAxes = constraints.at("axes").value_or(gr::Tensor<gr::pmt::Value>{});
        }
        plotMap["axes"] = plotAxes;

        // Serialize signal sinks from data_sinks property
        gr::Tensor<gr::pmt::Value> plotSinkBlockNames;
        for (const auto& sinkName : grc_compat::getBlockSinkNames(w.block.get())) {
            plotSinkBlockNames.emplace_back(sinkName);
        }
        plotMap["sources"] = plotSinkBlockNames;

        // Serialize window rect from uiConstraints or DockSpace::Window
        gr::Tensor<gr::pmt::Value> rectTensor(gr::extents_from, {std::size_t(4)});
        if (constraints.contains("window")) {
            if (const auto* windowMap = constraints.at("window").get_if<property_map>()) {
                auto toUInt64 = [](const gr::pmt::Value& v) -> std::uint64_t {
                    if (auto converted = gr::pmt::convert_safely<std::uint64_t>(v); converted) {
                        return *converted;
                    }
                    return 0ULL;
                };
                rectTensor[0] = toUInt64(windowMap->at("x"));
                rectTensor[1] = toUInt64(windowMap->at("y"));
                rectTensor[2] = toUInt64(windowMap->at("width"));
                rectTensor[3] = toUInt64(windowMap->at("height"));
            }
        } else if (w.window) {
            rectTensor[0] = static_cast<std::uint64_t>(w.window->x);
            rectTensor[1] = static_cast<std::uint64_t>(w.window->y);
            rectTensor[2] = static_cast<std::uint64_t>(w.window->width);
            rectTensor[3] = static_cast<std::uint64_t>(w.window->height);
        } else {
            rectTensor[0] = std::uint64_t{0};
            rectTensor[1] = std::uint64_t{0};
            rectTensor[2] = std::uint64_t{1};
            rectTensor[3] = std::uint64_t{1};
        }
        plotMap["rect"] = std::move(rectTensor);

        plots.emplace_back(plotMap);
    }
    dashboardYaml["plots"] = plots;

    if (description->storageInfo->path.starts_with("http://") || description->storageInfo->path.starts_with("https://")) {
        auto path = std::filesystem::path(description->storageInfo->path) / description->filename;

        opencmw::client::Command hcommand;
        hcommand.command          = opencmw::mdp::Command::Set;
        std::string headerYamlStr = pmt::yaml::serialize(headerYaml);
        hcommand.data.put(std::string_view(headerYamlStr.c_str(), headerYamlStr.size()));
        hcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "header").build();
        restClient->request(hcommand);

        opencmw::client::Command dcommand;
        dcommand.command             = opencmw::mdp::Command::Set;
        std::string dashboardYamlStr = pmt::yaml::serialize(dashboardYaml);
        dcommand.data.put(std::string_view(dashboardYamlStr.c_str(), dashboardYamlStr.size()));
        dcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "dashboard").build();
        restClient->request(dcommand);

        opencmw::client::Command fcommand;
        fcommand.command = opencmw::mdp::Command::Set;
        std::stringstream stream;
        fcommand.data.put(stream.str());
        fcommand.topic = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "flowgraph").build();
        restClient->request(fcommand);
    } else {
#ifndef EMSCRIPTEN
        auto path = std::filesystem::path(description->storageInfo->path);

        std::ofstream stream(path / (description->name + DashboardDescription::fileExtension), std::ios::out | std::ios::trunc);
        if (!stream.is_open()) {
            auto msg = std::format("can't open file for writing");
            components::Notification::warning(msg);
            return;
        }

        uint32_t      headerStart      = 32;
        std::string   headerYamlStr    = pmt::yaml::serialize(headerYaml);
        std::string   dashboardYamlStr = pmt::yaml::serialize(dashboardYaml);
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
#endif
    }
}

DigitizerUi::Dashboard::UIWindow& Dashboard::newUIBlock(int x, int y, int w, int h, std::string_view chartType) {
    std::string chartTypeName = chartType.empty() ? "XYChart" : std::string(chartType);

    // Generate a unique name for the chart
    static int  chartCounter = 1;
    std::string chartName    = std::format("Chart {}", chartCounter++);

    // Create chart block in UI Graph
    auto* blockPtr = emplaceChartBlock(chartTypeName, chartName);

    if (!blockPtr) {
        // Return a reference to an empty UIWindow on failure (shouldn't happen with known types)
        static UIWindow emptyWindow;
        return emptyWindow;
    }

    // Store window layout and default axes in uiConstraints
    blockPtr->uiConstraints()["window"] = gr::property_map{
        {"x", static_cast<std::uint64_t>(x)},
        {"y", static_cast<std::uint64_t>(y)},
        {"width", static_cast<std::uint64_t>(w)},
        {"height", static_cast<std::uint64_t>(h)},
    };

    // Find the shared_ptr for this block in uiGraph
    std::shared_ptr<gr::BlockModel> blockShared;
    for (auto& blk : uiGraph.blocks()) {
        if (blk.get() == blockPtr) {
            blockShared = blk;
            break;
        }
    }

    // Create UIWindow for this chart
    UIWindow win(blockShared, chartName);
    if (win.window) {
        win.window->setGeometry({static_cast<float>(x), static_cast<float>(y)}, {static_cast<float>(w), static_cast<float>(h)});
    }
    uiWindows.push_back(std::move(win));

    return uiWindows.back();
}

void Dashboard::deleteChart(UIWindow* win) {
    if (!win || !win->block) {
        return;
    }
    // Remove block from UI graph
    uiGraph.removeBlockByName(win->block->uniqueName());
    // Remove the UIWindow
    std::erase_if(uiWindows, [win](const UIWindow& w) { return &w == win; });
}

Dashboard::UIWindow* Dashboard::copyChart(std::string_view sourceChartId) {
    // Find source block
    gr::BlockModel* sourceBlock = nullptr;
    for (auto& w : uiWindows) {
        if (w.block && w.block->uniqueName() == sourceChartId) {
            sourceBlock = w.block.get();
            break;
        }
    }
    if (!sourceBlock) {
        return nullptr;
    }

    // Get source settings
    gr::property_map settings = sourceBlock->settings().getStored().value_or(gr::property_map{});

    // Generate unique name by appending suffix
    std::string baseName = sourceBlock->name().empty() ? std::string(sourceChartId) : std::string(sourceBlock->name());
    std::string newName  = baseName + "_copy";
    int         suffix   = 2;
    while (findUIWindowByName(newName) != nullptr) {
        newName = baseName + "_" + std::to_string(suffix++);
    }

    // Get chart type name
    std::string typeName = std::string(sourceBlock->typeName());

    // Create new block with same type and copied settings
    gr::property_map chartParameters;
    // Copy data_sinks setting if present
    auto sinkNames = grc_compat::getBlockSinkNames(sourceBlock);
    if (!sinkNames.empty()) {
        gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {sinkNames.size()});
        for (std::size_t i = 0; i < sinkNames.size(); ++i) {
            sinks[i] = sinkNames[i];
        }
        chartParameters[std::pmr::string("data_sinks")] = std::move(sinks);
    }

    auto* newBlock = emplaceChartBlock(typeName, newName, chartParameters);
    if (!newBlock) {
        return nullptr;
    }

    // Copy uiConstraints (axes config) but not window position (let DockSpace place it like a new chart)
    newBlock->uiConstraints() = sourceBlock->uiConstraints();
    newBlock->uiConstraints().erase("window");

    // Find the shared_ptr for the new block in the uiGraph
    std::shared_ptr<gr::BlockModel> newBlockPtr;
    for (auto& blk : uiGraph.blocks()) {
        if (blk.get() == newBlock) {
            newBlockPtr = blk;
            break;
        }
    }
    if (!newBlockPtr) {
        return nullptr;
    }

    // Create UIWindow for the new chart
    return &getOrCreateUIWindow(newBlockPtr);
}

bool Dashboard::transmuteUIWindow(UIWindow& win, std::string_view newChartType) {
    if (!win.block) {
        return false;
    }

    // Get current chart type name from block's fully-qualified type name
    std::string fullTypeName = std::string(win.block->typeName());
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
    std::vector<std::string> savedSinkNames     = grc_compat::getBlockSinkNames(win.block.get());
    gr::property_map         savedUiConstraints = win.block->uiConstraints();
    std::string              oldUniqueName      = std::string(win.block->uniqueName());
    std::string              windowName         = win.window ? win.window->name : oldUniqueName;

    // Remove old block from UI graph
    uiGraph.removeBlockByName(oldUniqueName);

    // Create new chart block with preserved sink names
    gr::property_map chartParameters;
    if (!savedSinkNames.empty()) {
        gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {savedSinkNames.size()});
        for (std::size_t i = 0; i < savedSinkNames.size(); ++i) {
            sinks[i] = savedSinkNames[i];
        }
        chartParameters[std::pmr::string("data_sinks")] = std::move(sinks);
    }

    auto* blockPtr = emplaceChartBlock(newChartType, windowName, chartParameters);
    if (!blockPtr) {
        // Transmutation failed - unknown chart type
        return false;
    }

    // Restore uiConstraints
    blockPtr->uiConstraints() = savedUiConstraints;

    // Find the shared_ptr for the new block in uiGraph
    std::shared_ptr<gr::BlockModel> newBlockShared;
    for (auto& blk : uiGraph.blocks()) {
        if (blk.get() == blockPtr) {
            newBlockShared = blk;
            break;
        }
    }

    if (!newBlockShared) {
        return false;
    }

    // Update UIWindow block reference
    win.block = newBlockShared;

    return true;
}

void Dashboard::removeSinkFromPlots(std::string_view sinkName) {
    for (auto& w : uiWindows) {
        if (w.isChart() && w.block) {
            auto names = grc_compat::getBlockSinkNames(w.block.get());
            std::erase(names, std::string(sinkName));
            grc_compat::setBlockSinkNames(w.block.get(), names);
        }
    }
    std::erase_if(uiWindows, [](const UIWindow& w) { return w.isChart() && w.block && grc_compat::getBlockSinkNames(w.block.get()).empty(); });
}

void Dashboard::loadUIWindowSources() {
    std::println("[Dashboard::loadUIWindowSources] {} UIWindows, {} sinks in registry", uiWindows.size(), opendigitizer::charts::SinkRegistry::instance().sinkCount());

    for (auto& w : uiWindows) {
        if (!w.isChart() || !w.block) {
            continue;
        }
        // Re-set data_sinks to trigger settingsChanged() -> syncSinksFromNames()
        auto names = grc_compat::getBlockSinkNames(w.block.get());
        if (!names.empty()) {
            grc_compat::setBlockSinkNames(w.block.get(), names);
        }
    }
}

void Dashboard::registerRemoteService(std::string_view blockName, std::optional<opencmw::URI<>> uri) {
    if (!uri) {
        return;
    }

    const auto flowgraphUri = opencmw::URI<>::UriFactory(*uri).path("/flowgraph").setQuery({}).build().str();
    std::print("block {} adds subscription to remote flowgraph service: {} -> {}\n", blockName, uri->str(), flowgraphUri);
    flowgraphUriByRemoteSource.insert({std::string{blockName}, flowgraphUri});

    const auto it = std::ranges::find_if(services, [&](const auto& s) { return s.uri == flowgraphUri; });
    if (it == services.end()) {
        auto msg = std::format("Registering to remote flow graph for '{}' at {}", blockName, flowgraphUri);
        components::Notification::warning(msg);
        auto& s = *services.emplace(restClient, flowgraphUri, flowgraphUri);
        s.reload();
    }
    removeUnusedRemoteServices();
}

void Dashboard::unregisterRemoteService(std::string_view blockName) {
    flowgraphUriByRemoteSource.erase(std::string{blockName});
    removeUnusedRemoteServices();
}

void Dashboard::removeUnusedRemoteServices() {
    std::erase_if(services, [&](const auto& s) { return std::ranges::none_of(flowgraphUriByRemoteSource | std::views::values, [&s](const auto& uri) { return uri == s.uri; }); });
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
    message.serviceName = graphModel.rootBlock.ownerSchedulerUniqueName();
    gr::property_map properties{
        {"remote_uri", uriStr},                 //
        {"signal_name", signalData.signalName}, //
        {"signal_unit", signalData.unit}        //
    }; //
    message.data = gr::property_map{                             //
        {"type", std::move(blockType) + std::move(blockParams)}, //
        {"properties", std::move(properties)}};

    graphModel.sendMessage(std::move(message));
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
            {"type", std::move(type)},        //
            {"parameters", std::move(params)} //
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

gr::BlockModel* Dashboard::emplaceChartBlock(std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters) {
    using namespace opendigitizer::charts;

    // Build initParams: start with chart name, then merge any additional parameters
    gr::property_map initParams;
    if (!chartName.empty()) {
        initParams["chart_name"] = chartName;
    }
    for (const auto& [key, value] : chartParameters) {
        initParams[key] = value;
    }

    // Resolve chart type name - try exact match first, then search registered types
    std::string resolvedTypeName;
    if (pluginLoader->isBlockAvailable(chartTypeName)) {
        resolvedTypeName = std::string(chartTypeName);
    } else {
        // Search for a matching chart type (case-insensitive partial match)
        std::string lowerRequestedType(chartTypeName);
        std::transform(lowerRequestedType.begin(), lowerRequestedType.end(), lowerRequestedType.begin(), [](unsigned char c) { return std::tolower(c); });

        for (const auto& registeredType : registeredChartTypes()) {
            std::string lowerRegistered = registeredType;
            std::transform(lowerRegistered.begin(), lowerRegistered.end(), lowerRegistered.begin(), [](unsigned char c) { return std::tolower(c); });
            // Match if registered type ends with the requested type (e.g., "XYChart" matches "opendigitizer::charts::XYChart")
            if (lowerRegistered.ends_with(lowerRequestedType) || lowerRequestedType.ends_with(lowerRegistered.substr(lowerRegistered.rfind("::") + 2))) {
                resolvedTypeName = registeredType;
                break;
            }
        }
    }

    // Fall back to default chart type if not found
    if (resolvedTypeName.empty()) {
        resolvedTypeName = std::string(kDefaultChartType);
    }

    // Create block via registry
    auto& blockModelRef = uiGraph.emplaceBlock(resolvedTypeName, initParams);
    if (!blockModelRef) {
        return nullptr;
    }

    return blockModelRef.get();
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
            const auto yaml = gr::pmt::yaml::deserialize(desc[0]);
            if (!yaml) {
                throw gr::exception(std::format("Could not parse yaml for DashboardDescription: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
            }
            const gr::property_map& rootMap    = yaml.value();
            bool                    isFavorite = rootMap.contains("favorite") && rootMap.at("favorite").value_or(false);

            auto getDate = [](const std::string& str) -> decltype(DashboardDescription::lastUsed) {
                if (str.size() < 10) {
                    return {};
                }
                int      year  = 0;
                unsigned month = 0;
                unsigned day   = 0;
                std::from_chars(str.data(), str.data() + 4, year);
                std::from_chars(str.data() + 5, str.data() + 7, month);
                std::from_chars(str.data() + 8, str.data() + 10, day);

                std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
                return std::chrono::sys_days(date);
            };

            auto lastUsed = rootMap.contains("lastUsed") && rootMap.at("lastUsed").is_string() ? getDate(rootMap.at("lastUsed").value_or(std::string())) : std::nullopt;

            cb(std::make_shared<DashboardDescription>(PrivateTag{}, std::filesystem::path(name).stem().native(), storageInfo, name, isFavorite, lastUsed));
        },
        [cb]() { cb({}); });
}

std::shared_ptr<const DashboardDescription> DashboardDescription::createEmpty(const std::string& name) { return std::make_shared<DashboardDescription>(PrivateTag{}, name, DashboardStorageInfo::memoryDashboardStorage(), std::string{}, false, std::nullopt); }
} // namespace DigitizerUi
