#include "Dashboard.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <print>
#include <ranges>
#include <set>

#include <format>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

#if defined(__EMSCRIPTEN__) && !defined(OD_WASM_STATIC_BLOCKLIBS)
#include "PluginWasmNames.hpp"
#endif

#include "common/Events.hpp"

#include <implot.h>

#include <opencmw.hpp>

#include <MdpMessage.hpp>
#include <RestClient.hpp>

#include "GraphModel.hpp"
#include "MapUtils.hpp"

#include "blocks/RemoteSource.hpp"
#include "components/SignalSelector.hpp"
#include "utils/Browser.hpp"

#include "charts/Charts.hpp"
#include "charts/SinkRegistry.hpp"

using namespace std::string_literals;

namespace gr::od {

namespace {
enum class What { Header, Flowgraph };

void fetch(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, What what, std::function<void(std::string&&)>&& cb, std::function<void()>&& errCb) {
    const StorageKind kind = storageInfo->kind();
    if (kind == StorageKind::Remote) {
        if (!client) {
            gr::log::error("cannot fetch '{}' from {}: no REST client", name, storageInfo->path);
            errCb();
            return;
        }
        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Get;
        auto path       = std::filesystem::path(storageInfo->path) / name;
        command.topic   = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", what == What::Header ? "header" : "flowgraph").build();

        command.callback = [callback = std::move(cb), errCallback = std::move(errCb)](const opencmw::mdp::Message& rep) mutable {
            std::string            reply; // <size>;<content>
            const std::string_view data(reinterpret_cast<const char*>(rep.data.data()), rep.data.size());
            if (const auto separator = data.find(';'); separator != std::string_view::npos) {
                std::size_t size = 0;
                if (std::from_chars(data.data(), data.data() + separator, size).ec == std::errc{} && size <= data.size() - separator - 1) {
                    reply = data.substr(separator + 1, size);
                }
            }

            if (reply.empty()) {
                EventLoop::instance().executeLater(std::move(errCallback));
            } else {
                EventLoop::instance().executeLater([callback, reply]() mutable { callback(std::move(reply)); });
            }
        };

        client->request(command);
        return;
    }
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
    if (kind == StorageKind::Example) {
        if (what == What::Header) {
            cb("favorite: false\nlastUsed: 07/04/2023");
        } else {
            auto file = cmrc::sample_dashboards::get_filesystem().open(std::format("assets/sampleDashboards/{}.grc", name));
            cb(std::string(file.begin(), file.end()));
        }
        return;
    }
#endif
#ifndef __EMSCRIPTEN__
    auto          path = std::filesystem::path(storageInfo->path) / name;
    std::ifstream stream(path, std::ios::in);
    if (stream.is_open()) {
        const auto corrupted = [&] {
            components::Notification::warning(std::format("Cannot load dashboard from '{}'. File is corrupted.", path.native()));
            errCb();
        };
        stream.seekg(0, std::ios::end);
        const auto filesize = stream.tellg();
        if (filesize < 32) {
            corrupted();
            return;
        }
        stream.seekg(what == What::Header ? 0 : 16);

        std::uint32_t start, size;
        stream.read(reinterpret_cast<char*>(&start), 4);
        stream.read(reinterpret_cast<char*>(&size), 4);
        if (filesize < start + size) {
            corrupted();
            return;
        }
        stream.seekg(start);
        std::string content(size, '\0');
        stream.read(content.data(), size);
        cb(std::move(content));
        return;
    }
#endif
    errCb();
}

std::vector<std::string> readStringList(const gr::property_map& map, std::string_view key) {
    std::vector<std::string> result;
    const auto               value = map.find_value(key);
    if (!value) {
        return result;
    }
    const auto view = value->get_if<gr::TensorView<gr::pmt::Value>>();
    if (!view) {
        return result;
    }
    for (const auto& element : *view) {
        if (element.is_string()) {
            result.push_back(element.value_or(std::string{}));
        }
    }
    return result;
}

DashboardDescription::StringMap readStringMap(const gr::property_map& map, std::string_view key) {
    DashboardDescription::StringMap result;
    if (const auto nested = map.get_if<gr::property_map>(key)) {
        for (const auto& [entryKey, entryValue] : *nested) {
            if (entryValue.is_string()) {
                result.emplace(entryKey, entryValue.value_or(std::string{}));
            }
        }
    }
    return result;
}

} // namespace

DashboardStorageInfo::~DashboardStorageInfo() noexcept {
    std::erase_if(knownDashboardStorage(), [](const auto& s) { return s.expired(); });
}

std::vector<std::weak_ptr<DashboardStorageInfo>>& DashboardStorageInfo::knownDashboardStorage() {
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

    auto dashboardStorageInfo = std::make_shared<DashboardStorageInfo>(std::string(path));
    knownDashboardStorage().push_back(dashboardStorageInfo);
    return dashboardStorageInfo;
}

// a static instance, a shared_ptr only because DashboardSources are; it is never saved
std::shared_ptr<DashboardStorageInfo> DashboardStorageInfo::memoryDashboardStorage() {
    static auto storageInfo = std::make_shared<DashboardStorageInfo>("Unsaved"s);
    return storageInfo;
}

std::unordered_set<std::string_view> Dashboard::getAllWindowNamesInUse() const {
    std::unordered_set<std::string_view> output;
    for (const auto& window : uiWindows) {
        [[maybe_unused]] auto [_, wasEmplaced] = output.emplace(std::string_view{window.window->name});
        assert(wasEmplaced);
    }
    for (const auto& [id, controlWindow] : propertyControlWindows) {
        [[maybe_unused]] auto [_, wasEmplaced] = output.emplace(std::string_view{controlWindow.window->name});
        assert(wasEmplaced);
    }
    return output;
}

Dashboard::Dashboard(std::shared_ptr<opencmw::client::RestClient> client, std::shared_ptr<DashboardDescription> desc) : restClient(std::move(client)), description(std::move(desc)) { description->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()); }

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { description = desc; }

void Dashboard::loadPlugins(std::function<void()> done) {
#if defined(__EMSCRIPTEN__) && !defined(OD_WASM_STATIC_BLOCKLIBS)
    // bare filenames next to index.html: filesystem::path must not see full URLs (it collapses "http://" to "http:/").
    // Sequential: a parallel dlopen of ~60 MB freezes the main thread and can stall the HTML spinner via run-dependencies.
    std::vector<std::string> names;
    names.reserve(kPluginWasmNames.size());
    for (const auto name : kPluginWasmNames) {
        names.emplace_back(name);
    }
    gr::log::debug("loading {} plugin side-modules sequentially", names.size());
    pluginLoader->loadPluginsAsync(
        names,
        [done = std::move(done)](std::expected<void, gr::Error> result) {
            if (!result) {
                gr::log::error("loading a plugin side-module failed: {}", result.error().message);
            } else {
                gr::log::debug("all plugin side-modules loaded");
            }
            if (done) {
                done();
            }
        },
        /*sequential=*/true);
#else
    if (done) {
        done();
    }
#endif
}

void Dashboard::load() {
    if (!description->storageInfo->isInMemoryDashboardStorage()) {
        isInitialised.store(false, std::memory_order_release);
        isInUse = true;

        auto doFetch = [this]() {
            fetch(
                restClient, description->storageInfo, description->filename, What::Flowgraph, //
                [this](std::string&& data) {
                    loadAndThen(std::move(data), [this](gr::Graph&& graph) { session.emplaceGraph(std::move(graph)); });
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
        };

        loadPlugins([doFetch = std::move(doFetch)]() { doFetch(); });
    }
}

void Dashboard::loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler) {
    const auto publishLoadResult = [this](bool loaded, std::string_view error) {
        if (browserTestHooks()) {
            publishForBrowserTests("odDashboard", "current", std::format(R"({{"name":{},"loaded":{},"error":{}}})", jsonString(description->name), loaded, jsonString(error)));
        }
    };
    const auto fail = [this, &publishLoadResult](std::string_view message) {
        publishLoadResult(false, message);
        components::Notification::error(std::format("Error: {}", message));
        if (requestClose) {
            requestClose(this);
        }
    };
    try {
        const auto yaml = gr::pmt::yaml::deserialize(grcData);
        if (!yaml) {
            fail(std::format("Could not parse yaml: {}:{}\n{}", yaml.error().message, yaml.error().line, grcData));
            return;
        }
        const gr::property_map& rootMap = yaml.value();

        gr::Graph grGraph(*pluginLoader);
        if (const auto loaded = gr::detail::loadGraphFromMap(*pluginLoader, grGraph, rootMap); !loaded) {
            fail(loaded.error().message);
            return;
        }

        const std::string_view dashboardStorageURI = description->storageInfo->path;
        const bool             isRemoteStorage     = description->storageInfo->kind() == StorageKind::Remote;
        const std::string      sourceHost          = [&]() -> std::string {
            if (!isRemoteStorage) {
                return "https://localhost:8443";
            }
            const opencmw::URI<> dashboardUri{std::string(dashboardStorageURI)};
            return dashboardUri.factory().hostName(dashboardUri.hostName().value_or("localhost")).port(dashboardUri.port().value_or(8080)).scheme(dashboardUri.scheme().value_or("https")).build().str();
        }();
        gr::graph::forEachBlock<gr::block::Category::NormalBlock>(grGraph, [&sourceHost](auto& block) {
            if (block->typeName().starts_with(baseTypeName<RemoteStreamSource<float>>()) || block->typeName().starts_with(baseTypeName<RemoteDataSetSource<float>>())) {
                auto* sourceBlock = static_cast<RemoteSourceBase*>(block->raw());
                sourceBlock->host = sourceHost;
            }
        });

        const gr::pmt::Value dashboardValue = rootMap.find_value("dashboard").value_or(gr::pmt::Value{});
        const auto           dashboard      = dashboardValue.get_if<gr::property_map>();
        if (dashboard) {
            if (const auto loaded = doLoad(*dashboard, grGraph); !loaded) {
                fail(loaded.error().message);
                return;
            }
        }
        assignScheduler(std::move(grGraph));
        if (!dashboard) {
            fail(std::format("dashboard field is not a property_map, it is {}, in the map {}", rootMap.find_value("dashboard"), rootMap));
            return;
        }
        loadUIWindowSources();
        isInitialised.store(true, std::memory_order_release);
        publishLoadResult(true, {});
    } catch (const std::exception& e) {
        fail(e.what());
    } catch (...) {
        fail("Unkonwn exception");
    }
}

std::expected<void, gr::Error> Dashboard::doLoad(const gr::property_map& dashboard, gr::Graph& graph) {
    using namespace gr;

    editable                   = dashboard.value_or<bool>("editable", true);
    schedulerUi                = dashboard.value_or<bool>("scheduler_ui", false);
    layoutType                 = magic_enum::enum_cast<DockingLayoutType>(dashboard.value_or<std::string>("layout", {}), magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
    const bool hasWindowLayout = dashboard.contains("windowLayout");
    if (hasWindowLayout) {
        if (const auto windowLayoutOpt = dashboard.find_value("windowLayout").value_or(gr::pmt::Value{}).get_if<gr::property_map>()) {
            windowLayout = *windowLayoutOpt;
        }
    }

    const auto plots = getOptionalProperty<Tensor<pmt::Value>>(dashboard, "plots");
    if (!plots) {
        return std::unexpected(plots.error());
    }

    for (const auto& plotPmt : *plots) {
        if (!plotPmt.holds<property_map>()) {
            return std::unexpected(gr::Error("plot is not a property_map"));
        }
        const property_map plotMap = *plotPmt.get_if<property_map>();

        const auto name        = getOptionalProperty<std::string>(plotMap, "name");
        const auto plotSources = getOptionalProperty<Tensor<pmt::Value>>(plotMap, "sources");
        if (!name || !plotSources) {
            return std::unexpected(name ? plotSources.error() : name.error());
        }
        const auto optional = [&plotMap]<typename T>(std::string_view key) -> std::expected<std::optional<T>, gr::Error> {
            if (!plotMap.contains(key)) {
                return std::nullopt;
            }
            return getOptionalProperty<T>(plotMap, key);
        };
        auto rect = optional.operator()<Tensor<std::int64_t>>("rect");
        if (!rect) {
            return std::unexpected(rect.error());
        }
        if (!*rect && !hasWindowLayout && layoutType == DockingLayoutType::Free) {
            return std::unexpected(gr::Error("Missing one of two possible required keys for describing dashboard window UI:"
                                             " [\"dashboard\"][\"windowLayout\"] or per-plot [\"rect\"] field"));
        }
        if (*rect && hasWindowLayout) {
            gr::log::warning("a plot rect and the dashboard windowLayout are both specified, the rect is ignored");
            rect->reset();
        }
        if (*rect && (*rect)->size() != 4) {
            return std::unexpected(gr::Error("invalid plot definition rect.size() != 4"));
        }

        const auto type       = optional.operator()<std::string>("type");
        const auto parameters = optional.operator()<property_map>("parameters");
        const auto axes       = optional.operator()<gr::Tensor<gr::pmt::Value>>("axes");
        if (!type || !parameters || !axes) {
            return std::unexpected(!type ? type.error() : !parameters ? parameters.error() : axes.error());
        }
        const std::string          chartTypeName   = type->value_or("XYChart");
        gr::property_map           chartParameters = parameters->value_or(gr::property_map{});
        gr::Tensor<gr::pmt::Value> axesConfig      = axes->value_or(gr::Tensor<gr::pmt::Value>{});

        // 'sources:' become 'data_sinks' (see grc_compat in Dashboard.hpp)
        gr::Tensor<gr::pmt::Value> dataSinksTensor(gr::extents_from, {plotSources->size()});
        for (std::size_t i = 0; i < plotSources->size(); ++i) {
            if (!(*plotSources)[i].is_string()) {
                return std::unexpected(gr::Error("plot.sources elements must be strings"));
            }
            dataSinksTensor[i] = (*plotSources)[i].value_or(std::string());
        }
        chartParameters[std::pmr::string("data_sinks")] = std::move(dataSinksTensor);

        const std::string requestedBlockName = chartParameters.value_or<std::string>("name", resolveChartTypeName(chartTypeName));
        chartParameters["name"]              = session.graphModel.nameRegistry.ensureNameIsUnique(requestedBlockName);

        auto blockPtr = emplaceChartBlock(graph, chartTypeName, *name, chartParameters);
        if (!blockPtr) {
            components::Notification::warning(std::format("Failed to create chart block of type '{}'", chartTypeName));
            continue;
        }

        blockPtr->uiConstraints()["axes"] = axesConfig;

        constexpr auto rectValue = [](const gr::pmt::Value& v) -> std::uint64_t {
            auto value = v.value_or<std::int64_t>(0);
            return value < 0 ? 0ULL : static_cast<std::uint64_t>(value);
        };

        UIWindow uiWindow{.window = std::make_shared<DockSpace::Window>(*name), .block = std::move(blockPtr)};

        if (*rect) {
            const auto& r                       = **rect;
            uiWindow.window->freeLayoutPosition = {
                .x      = static_cast<std::size_t>(rectValue(r[0])),
                .y      = static_cast<std::size_t>(rectValue(r[1])),
                .width  = static_cast<std::size_t>(rectValue(r[2])),
                .height = static_cast<std::size_t>(rectValue(r[3])),
            };
        }

        uiWindows.push_back(std::move(uiWindow));
    }

    if (dashboard.contains("propertyControlWindows")) {
        if (const auto controlWindowsByWindowName = dashboard.find_value("propertyControlWindows").value_or(gr::pmt::Value{}).get_if<gr::property_map>()) {
            for (const auto& [controlWindowName, controlWindowPropertiesValue] : *controlWindowsByWindowName) {
                if (const auto controlWindowProperties = controlWindowPropertiesValue.get_if<gr::property_map>()) {
                    const gr::pmt::Value idPmt    = controlWindowProperties->find_value("id").value_or(gr::pmt::Value{});
                    const gr::pmt::Value labelPmt = controlWindowProperties->find_value("label").value_or(gr::pmt::Value{});
                    const auto*          id       = idPmt.get_if<gr::Size_t>();
                    const auto           label    = labelPmt.get_if<std::string_view>();
                    if (id && label) {
                        propertyControlWindows.try_emplace(*id, PropertyControlWindow{.window = std::make_shared<DockSpace::Window>(std::string(controlWindowName)), .label = std::string(*label)});
                    }
                }
            }
        }
    }
    return {};
}

void Dashboard::saveStore(const gr::property_map& headerYaml, const gr::property_map& graphYaml) {
    using namespace gr;
    const auto headerYamlStr = pmt::yaml::serialize(headerYaml);
    const auto graphYamlStr  = pmt::yaml::serialize(graphYaml);
    if (description->storageInfo->kind() == StorageKind::Remote) {
        if (!restClient) {
            components::Notification::error(std::format("cannot save to {}: no REST client", description->storageInfo->path));
            return;
        }
        auto path = std::filesystem::path(description->storageInfo->path) / description->filename;

        opencmw::client::Command hcommand;
        hcommand.command = opencmw::mdp::Command::Set;
        hcommand.data.put(std::string_view(headerYamlStr.c_str(), headerYamlStr.size()));
        hcommand.topic    = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "header").build();
        hcommand.callback = [](const opencmw::mdp::Message&) {};
        restClient->request(hcommand);

        opencmw::client::Command fcommand;
        fcommand.command = opencmw::mdp::Command::Set;
        fcommand.data.put(std::string_view(graphYamlStr));
        fcommand.topic    = opencmw::URI<opencmw::STRICT>::UriFactory().path(path.native()).addQueryParameter("what", "flowgraph").build();
        fcommand.callback = [](const opencmw::mdp::Message&) {};
        restClient->request(fcommand);
    } else {
#ifndef __EMSCRIPTEN__
        auto path = std::filesystem::path(description->storageInfo->path);

        std::ofstream stream(path / description->filename, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open()) {
            auto msg = std::format("can't open file for writing");
            components::Notification::warning(msg);
            return;
        }

        constexpr std::uint32_t kHeaderStart = 32;
        const auto              headerSize   = static_cast<std::uint32_t>(headerYamlStr.size());
        const auto              graphStart   = kHeaderStart + headerSize + 1;
        const auto              graphSize    = static_cast<std::uint32_t>(graphYamlStr.size());
        // the reader expects the flowgraph offset and size at byte 16
        const std::array<std::uint32_t, 8> offsets{kHeaderStart, headerSize, 0, 0, graphStart, graphSize, 0, 0};
        stream.write(reinterpret_cast<const char*>(offsets.data()), sizeof(offsets));
        stream << headerYamlStr << '\n' << graphYamlStr << '\n';
#endif
    }
}

void Dashboard::save(DockingLayoutType liveLayoutType, const gr::property_map& liveWindowLayout) {
    if (description->storageInfo->isInMemoryDashboardStorage() || !session) {
        return;
    }
    layoutType                         = liveLayoutType;
    windowLayout                       = liveWindowLayout;
    const auto [headerYaml, graphYaml] = serialise();
    saveStore(headerYaml, graphYaml);
}

std::pair<gr::property_map, gr::property_map> Dashboard::serialise() {
    using namespace gr;

    property_map headerYaml;
    headerYaml["favorite"] = description->isFavorite;
    std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(description->lastUsed.value()));
    headerYaml["lastUsed"] = std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));

    gr::Tensor<gr::pmt::Value> tagsTensor;
    for (const std::string& tag : description->tags) {
        tagsTensor.emplace_back(tag);
    }
    headerYaml["tags"] = std::move(tagsTensor);

    property_map keyValueTagsMap;
    for (const auto& [key, value] : description->keyValueTags) {
        keyValueTagsMap[key] = value;
    }
    headerYaml["keyValueTags"] = std::move(keyValueTagsMap);

    auto graphYaml = gr::detail::saveGraphToMap(*pluginLoader, session.graph());
    session.graphModel.saveBlockPositions(graphYaml);
    if (const auto savedBlocks = graphYaml.get_if<gr::TensorView<gr::pmt::Value>>("blocks")) {
        std::set<std::string, std::less<>> chartUniqueNames;
        for (const auto& block : session.graph().blocks()) {
            if (isChartTypeName(block->typeName())) {
                chartUniqueNames.emplace(block->uniqueName());
            }
        }
        gr::Tensor<gr::pmt::Value> blocksWithoutCharts;
        for (const gr::pmt::Value& block : savedBlocks->owned()) {
            if (!chartUniqueNames.contains(block.value_or(gr::property_map{}).value_or<std::string>("unique_name", {}))) {
                blocksWithoutCharts.emplace_back(block);
            }
        }
        graphYaml["blocks"] = std::move(blocksWithoutCharts);
    }
    property_map dashboardYaml;
    dashboardYaml["editable"] = editable;

    gr::Tensor<gr::pmt::Value> sources;
    for (const UiGraphBlock* sink : session.graphModel.recursiveGatherPlotSinks()) {
        property_map map;
        map["block"] = sink->blockName;
        map["name"]  = sink->blockName;
        if (const auto color = sink->blockSettings.find_value("color", std::pmr::get_default_resource())) {
            map["color"] = *color;
        }
        sources.emplace_back(std::move(map));
    }
    dashboardYaml["sources"] = sources;

    if (schedulerUi) {
        dashboardYaml["scheduler_ui"] = true;
    }
    dashboardYaml["layout"]       = std::string(gr::meta::enumName(layoutType).value_or(""));
    dashboardYaml["windowLayout"] = windowLayout;

    dashboardYaml["propertyControlWindows"] = [this] {
        gr::property_map out;
        for (const auto& [id, controlWindow] : propertyControlWindows) {
            gr::property_map windowProperties = {{"id", static_cast<gr::Size_t>(id)}, {"label", controlWindow.label}};
            out.try_emplace(std::pmr::string{controlWindow.window->name}, std::move(windowProperties));
        }
        return out;
    }();

    gr::Tensor<gr::pmt::Value> plots;
    for (const auto& w : uiWindows) {
        if (!w.block) {
            continue;
        }

        property_map plotMap;
        plotMap["name"]          = w.window ? w.window->name : registeredUniqueName(w.block->uniqueName());
        std::string fullTypeName = std::string(w.block->typeName());
        if (auto pos = fullTypeName.rfind("::"); pos != std::string::npos) {
            plotMap["type"] = fullTypeName.substr(pos + 2);
        } else {
            plotMap["type"] = fullTypeName;
        }

        gr::Tensor<gr::pmt::Value> plotAxes;
        const auto&                constraints = w.block->uiConstraints();
        if (constraints.contains("axes")) {
            if (const auto axesPmt = constraints.find_value("axes")) {
                plotAxes = axesPmt->value_or(gr::Tensor<gr::pmt::Value>{});
            }
        }
        plotMap["axes"] = plotAxes;

        gr::Tensor<gr::pmt::Value> plotSinkBlockNames;
        for (const auto& sinkName : grc_compat::getBlockSinkNames(w.block.get())) {
            plotSinkBlockNames.emplace_back(sinkName);
        }
        plotMap["sources"] = plotSinkBlockNames;
        plots.emplace_back(plotMap);
    }
    dashboardYaml["plots"] = plots;

    graphYaml["dashboard"] = std::move(dashboardYaml);
    return {std::move(headerYaml), std::move(graphYaml)};
}

namespace {

[[nodiscard]] std::string chartNameOf(const gr::BlockModel& block) {
    const std::string name = block.settings().get("chart_name").value_or(gr::pmt::Value{}).value_or(std::string());
    return name.empty() ? registeredUniqueName(block.uniqueName()) : name;
}

} // namespace

void Dashboard::newUIBlock(std::string_view chartType, const gr::property_map& chartInitialParameters) {
    static int  chartCounter = 1;
    std::string chartName;
    do {
        chartName = std::format("Chart {}", chartCounter++);
    } while (getAllWindowNamesInUse().contains(chartName) || pendingCharts.contains(chartName));

    gr::property_map properties = chartInitialParameters;
    properties["chart_name"]    = chartName;

    const std::string chartTypeName = resolveChartTypeName(chartType.empty() ? "XYChart" : chartType);
    properties["name"]              = session.graphModel.nameRegistry.ensureNameIsUnique(chartTypeName);

    pendingCharts.try_emplace(chartName);
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", chartTypeName}, {"properties", std::move(properties)}});
}

void Dashboard::deleteChart(UIWindow* win) {
    if (!win || !win->block) {
        return;
    }
    session.sendToScheduler(gr::scheduler::property::kRemoveBlock, {{"uniqueName", std::string(win->block->uniqueName())}});
    std::erase_if(uiWindows, [win](const UIWindow& w) { return &w == win; });
}

void Dashboard::copyChart(std::string_view sourceChartId) {
    const UIWindow* source = findUIWindowByName(sourceChartId);
    if (!source || !source->block) {
        return;
    }
    const gr::BlockModel& sourceBlock = *source->block;
    const std::string     baseName    = sourceBlock.name().empty() ? std::string(sourceChartId) : std::string(sourceBlock.name());
    std::string           newName     = baseName + "_copy";
    for (int suffix = 2; getAllWindowNamesInUse().contains(newName) || pendingCharts.contains(newName); ++suffix) {
        newName = std::format("{}_{}", baseName, suffix);
    }

    gr::property_map properties{{"chart_name", newName}, {"name", session.graphModel.nameRegistry.ensureNameIsUnique(sourceBlock.name())}};
    if (const auto sinkNames = grc_compat::getBlockSinkNames(&sourceBlock); !sinkNames.empty()) {
        properties["data_sinks"] = grc_compat::sinkNamesTensor(sinkNames);
    }
    gr::property_map constraints = sourceBlock.uiConstraints(); // without the window position
    constraints.erase("window");
    pendingCharts.insert_or_assign(newName, std::move(constraints));
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", std::string(registeredId(sourceBlock.typeName()))}, {"properties", std::move(properties)}});
}

bool Dashboard::transmuteUIWindow(UIWindow& win, std::string_view newChartType) {
    if (!win.block) {
        return false;
    }
    const std::string newTypeName = resolveChartTypeName(newChartType);
    if (newTypeName == registeredId(win.block->typeName())) {
        return true;
    }
    const std::string windowName = win.window ? win.window->name : registeredUniqueName(win.block->uniqueName());
    gr::property_map  properties{{"chart_name", windowName}, {"name", std::string(win.block->name())}};
    if (const auto sinkNames = grc_compat::getBlockSinkNames(win.block.get()); !sinkNames.empty()) {
        properties["data_sinks"] = grc_compat::sinkNamesTensor(sinkNames);
    }
    pendingCharts.insert_or_assign(windowName, win.block->uiConstraints());
    session.sendToScheduler(gr::scheduler::property::kReplaceBlock, {{"uniqueName", std::string(win.block->uniqueName())}, {"type", newTypeName}, {"properties", std::move(properties)}});
    return true;
}

void Dashboard::bindChartWindows() {
    const std::uint64_t topology = session.graphModel.topologyGeneration;
    if (!session || session.isExchangingGraph() || topology == boundChartsTopology) {
        return;
    }
    boundChartsTopology = topology;

    std::vector<std::shared_ptr<gr::BlockModel>> charts;
    std::ranges::copy_if(session.graph().blocks(), std::back_inserter(charts), [](const auto& block) { return isChartTypeName(block->typeName()); });
    for (const auto& block : charts) {
        if (findUIWindow(block) != nullptr) {
            continue;
        }
        const std::string name   = chartNameOf(*block);
        const auto        window = std::ranges::find_if(uiWindows, [&name](const UIWindow& w) { return w.window && w.window->name == name; });
        if (window != uiWindows.end()) {
            window->block = block;
        } else {
            uiWindows.push_back({.window = std::make_shared<DockSpace::Window>(name), .block = block});
        }
        if (auto pending = pendingCharts.extract(name); pending && !pending.mapped().empty()) {
            block->uiConstraints() = std::move(pending.mapped());
        }
        if (const auto sinkNames = grc_compat::getBlockSinkNames(block.get()); !sinkNames.empty()) {
            grc_compat::setBlockSinkNames(block.get(), sinkNames);
        }
    }
    std::erase_if(uiWindows, [&](const UIWindow& w) { return !std::ranges::contains(charts, w.block) && !(w.window && pendingCharts.contains(w.window->name)); });
}

void Dashboard::loadUIWindowSources() {

    for (auto& w : uiWindows) {
        if (!w.block) {
            continue;
        }
        // set again to trigger settingsChanged() and syncSinksFromNames()
        auto names = grc_compat::getBlockSinkNames(w.block.get());
        if (!names.empty()) {
            grc_compat::setBlockSinkNames(w.block.get(), names);
        }
    }
}

void Dashboard::addRemoteSignal(const SignalData& signalData) {
    const auto& uriStr    = signalData.uri();
    auto        blockType = [&] {
        opencmw::URI<opencmw::RELAXED> uri{uriStr};
        const auto                     params  = uri.queryParamMap();
        const auto                     acqMode = params.find("acquisitionModeFilter");
        if (acqMode != params.end() && acqMode->second && acqMode->second != "streaming") {
            return std::string(withoutTemplateArguments(registeredId<RemoteDataSetSource<float>>()));
        }
        return std::string(withoutTemplateArguments(registeredId<RemoteStreamSource<float>>()));
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

    const std::string fullBlockType = std::move(blockType) + std::move(blockParams);
    gr::property_map  properties{{"remote_uri", uriStr}, {"signal_name", signalData.signalName}, {"signal_unit", signalData.unit}, {"name", session.graphModel.nameRegistry.ensureNameIsUnique(fullBlockType)}};
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", fullBlockType}, {"properties", std::move(properties)}});
}

std::string Dashboard::resolveChartTypeName(std::string_view chartTypeName) const {
    using namespace charts;
    if (pluginLoader->isBlockAvailable(chartTypeName)) {
        return std::string(chartTypeName);
    }
    const auto lowerCase = [](std::string text) {
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string requested = lowerCase(std::string(chartTypeName));
    for (const auto& registeredType : registeredChartTypes()) {
        const std::string registered = lowerCase(registeredType);
        if (registered.ends_with(requested) || requested.ends_with(registered.substr(registered.find_last_of(':') + 1))) {
            return registeredType;
        }
    }
    return std::string(registeredId<XYChart>());
}

std::shared_ptr<gr::BlockModel> Dashboard::emplaceChartBlock(gr::Graph& graph, std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters) {
    gr::property_map initParams = chartParameters;
    if (!chartName.empty()) {
        initParams.try_emplace("chart_name", chartName);
    }
    auto blockModel = graph.emplaceBlock(resolveChartTypeName(chartTypeName), initParams);
    return blockModel ? std::move(*blockModel) : nullptr;
}

void DashboardDescription::loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& name, const std::function<void(std::shared_ptr<DashboardDescription>&&)>& cb) {
    fetch(
        client, storageInfo, name, What::Header,
        [cb, name, storageInfo](std::string&& desc) {
            const auto yaml = gr::pmt::yaml::deserialize(desc);
            if (!yaml) {
                components::Notification::warning(std::format("Could not parse yaml for DashboardDescription: {}:{}\n{}", yaml.error().message, yaml.error().line, desc));
                cb({});
                return;
            }
            const gr::property_map& rootMap     = yaml.value();
            const auto              valueForKey = [&rootMap](std::string_view key) { return rootMap.find_value(key).value_or(gr::pmt::Value{}); };
            bool                    isFavorite  = rootMap.contains("favorite") && valueForKey("favorite").value_or(false);

            auto getDate = [](const std::string& str) -> decltype(DashboardDescription::lastUsed) {
                if (str.size() != 10) {
                    return {};
                }
                const bool legacyFormat = str[2] == '/' && str[5] == '/';
                if (!legacyFormat && (str[4] != '-' || str[7] != '-')) {
                    return {};
                }
                const auto parse = [&str](std::size_t offset, std::size_t size, auto& value) {
                    const auto* end         = str.data() + offset + size;
                    const auto [parsed, ec] = std::from_chars(str.data() + offset, end, value);
                    return ec == std::errc{} && parsed == end;
                };
                int      year  = 0;
                unsigned month = 0;
                unsigned day   = 0;
                if (!parse(legacyFormat ? 6UZ : 0UZ, 4UZ, year) || !parse(legacyFormat ? 3UZ : 5UZ, 2UZ, month) || !parse(legacyFormat ? 0UZ : 8UZ, 2UZ, day)) {
                    return {};
                }

                std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
                if (!date.ok()) {
                    return {};
                }
                return std::chrono::sys_days(date);
            };

            auto lastUsed = rootMap.contains("lastUsed") && valueForKey("lastUsed").is_string() ? getDate(valueForKey("lastUsed").value_or(std::string())) : std::nullopt;

            cb(std::make_shared<DashboardDescription>(DashboardDescription{.name = std::filesystem::path(name).stem().native(), .storageInfo = storageInfo, .filename = name, .isFavorite = isFavorite, .lastUsed = lastUsed, .tags = readStringList(rootMap, "tags"), .keyValueTags = readStringMap(rootMap, "keyValueTags")}));
        },
        [cb]() { cb({}); });
}

void DashboardDescription::loadFlowgraphAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, std::function<void(std::string&&)>&& cb, std::function<void()>&& errCb) { fetch(std::move(client), storageInfo, filename, What::Flowgraph, std::move(cb), std::move(errCb)); }

std::shared_ptr<DashboardDescription> DashboardDescription::createEmpty(const std::string& name) {
    auto description         = std::make_shared<DashboardDescription>();
    description->name        = name;
    description->storageInfo = DashboardStorageInfo::memoryDashboardStorage();
    return description;
}
} // namespace gr::od
