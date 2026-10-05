#include "Dashboard.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <print>
#include <ranges>

#include <format>
#include <gnuradio-4.0/Logger.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>

#if defined(__EMSCRIPTEN__) && !defined(OD_WASM_STATIC_BLOCKLIBS)
#include "PluginWasmNames.hpp"
#endif

#include "common/Events.hpp"

#include <implot.h>

#include <opencmw.hpp>

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <daq_api.hpp>

#include "GraphModel.hpp"

#include "blocks/RemoteSource.hpp"
#include "components/SignalSelector.hpp"

#include "charts/Charts.hpp"
#include "charts/SinkRegistry.hpp"

using namespace std::string_literals;

struct FlowgraphMessage {
    std::string flowgraph;
    std::string layout;
};

#if defined(__EMSCRIPTEN__) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc2y-extensions" // refl-cpp uses __COUNTER__ issue
#endif

ENABLE_REFLECTION_FOR(FlowgraphMessage, flowgraph, layout)

#if defined(__EMSCRIPTEN__) && defined(__clang__)
#pragma clang diagnostic pop
#endif

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
        if (!client) {
            gr::log::error("cannot fetch '{}' from {}: no REST client", name, storageInfo->path);
            errCb();
            return;
        }
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

std::size_t& lastUsedWindowId() {
    static std::size_t lastUsedWindowId = 0;
    return lastUsedWindowId;
}

std::string Dashboard::generateUniqueNameForPropertyControlWindow(UiGraphBlock* block, std::string_view propertyName) {
    auto        currentlyInUse = getAllWindowNamesInUse();
    const char* blockName      = block ? block->blockName.c_str() : "UNKNOWN";
    const auto  prefix         = std::format("Property control for {} of block {}", propertyName, blockName);
    // imgui restores a window's position by name: a new property window needs a new name
    std::string out;
    do {
        ++lastUsedWindowId();
        out = std::format("{}{}", prefix, lastUsedWindowId());
    } while (currentlyInUse.contains(out));
    return out;
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

Dashboard::UIWindow::UIWindow(DeserializeTag, std::shared_ptr<gr::BlockModel> blk, std::string_view name) //
    : window(std::make_shared<DockSpace::Window>(std::string{name})), block(std::move(blk)) {}

Dashboard::PropertyControlWindow::PropertyControlWindow(DeserializeTag, std::string_view labelView, std::string_view windowName)
    : window(std::make_shared<DockSpace::Window>(std::string{windowName})), //
      label{labelView}                                                      //
{}

Dashboard::PropertyControlWindow::PropertyControlWindow(Dashboard& dashboard, UiGraphModel* graphModel, //
    std::string_view propertyName, std::string_view labelView, std::string_view blockName)              //
    : PropertyControlWindow(dashboard, graphModel->recursiveFindBlockByName(blockName).block, propertyName, labelView) {}

Dashboard::PropertyControlWindow::PropertyControlWindow(Dashboard& dashboard, UiGraphBlock* block, std::string_view propertyName, std::string_view labelView) //
    : window(std::make_shared<DockSpace::Window>(dashboard.generateUniqueNameForPropertyControlWindow(block, propertyName))),                                 //
      label{labelView}                                                                                                                                        //
{
    assert(block && "trying to control property of nonexistent block");
}

Dashboard::Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc) : restClient(std::move(client)), description(desc) { description->lastUsed = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()); }

Dashboard::~Dashboard() {}

std::unique_ptr<Dashboard> Dashboard::create(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc) { return std::make_unique<Dashboard>(PrivateTag{}, client, desc); }

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription>& desc) { description = desc; }

void Dashboard::loadPlugins(std::function<void()> done) {
#if defined(__EMSCRIPTEN__) && !defined(OD_WASM_STATIC_BLOCKLIBS)
    // Bare filenames next to index.html — filesystem::path must not see full URLs
    // (collapses "http://" → "http:/"). Sequential: parallel dlopen of ~60MB freezes
    // the main thread and can stall the HTML spinner via run-dependencies.
    std::vector<std::string> names;
    names.reserve(Digitizer::kPluginWasmNames.size());
    for (const auto name : Digitizer::kPluginWasmNames) {
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
                restClient, description->storageInfo, description->filename, {What::Flowgraph}, //
                [this](std::array<std::string, 1>&& data) {
                    loadAndThen(std::move(data[0]), [this](gr::Graph&& graph) { session.emplaceGraph(std::move(graph)); });
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
    try {
        const auto yaml = gr::pmt::yaml::deserialize(grcData);
        if (!yaml) {
            throw gr::exception(std::format("Could not parse yaml: {}:{}\n{}", yaml.error().message, yaml.error().line, grcData));
        }
        const gr::property_map& rootMap = yaml.value();

        gr::Graph grGraph = [this, &rootMap]() -> gr::Graph {
            try {
                gr::Graph  resultGraph(*pluginLoader);
                const auto loadResult = gr::detail::loadGraphFromMap(*pluginLoader, resultGraph, rootMap);
                if (!loadResult.has_value()) {
                    throw gr::exception(loadResult.error().message, loadResult.error().sourceLocation);
                }
                return resultGraph;
            } catch (const gr::exception& e) {
                throw;
            } catch (const std::string& e) {
                throw gr::exception(e);
            } catch (...) {
                throw;
            }
        }();

        const std::string_view dashboardStorageURI = description->storageInfo->path;
        const bool             isRemoteStorage     = dashboardStorageURI.starts_with("http://") || dashboardStorageURI.starts_with("https://");
        const std::string      sourceHost          = [&]() -> std::string {
            if (!isRemoteStorage) {
                return "https://localhost:8443";
            }
            const opencmw::URI<> dashboardUri{std::string(dashboardStorageURI)};
            return dashboardUri.factory().hostName(dashboardUri.hostName().value_or("localhost")).port(dashboardUri.port().value_or(8080)).scheme(dashboardUri.scheme().value_or("https")).build().str();
        }();
        gr::graph::forEachBlock<gr::block::Category::NormalBlock>(grGraph, [&sourceHost](auto& block) {
            if (block->typeName().starts_with("opendigitizer::RemoteStreamSource") || block->typeName().starts_with("opendigitizer::RemoteDataSetSource")) {
                auto* sourceBlock = static_cast<opendigitizer::RemoteSourceBase*>(block->raw());
                sourceBlock->host = sourceHost;
            }
        });

        const gr::pmt::Value dashboardValue = rootMap.find_value("dashboard").value_or(gr::pmt::Value{});
        const auto           dashboard      = dashboardValue.get_if<gr::property_map>();
        if (dashboard) {
            doLoad(*dashboard, grGraph);
        }
        assignScheduler(std::move(grGraph));
        if (!dashboard) {
            throw gr::exception(std::format("dashboard field is not a property_map, it is {}, in the map {}", //
                rootMap.find_value("dashboard"),                                                              //
                rootMap));
        }
        loadUIWindowSources();
        isInitialised.store(true, std::memory_order_release);
    } catch (const gr::exception& e) {
        components::Notification::error(std::format("Error: {}", e.what()));
        if (requestClose) {
            requestClose(this);
        }
    } catch (const std::exception& e) {
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

void Dashboard::doLoad(const gr::property_map& dashboard, gr::Graph& graph) {
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
        } else if constexpr (gr::TensorLike<T>) {
            const gr::pmt::Value val = it->second;
            if (const auto tv = val.get_if<gr::TensorView<typename T::value_type>>()) {
                return tv->owned();
            }
            throw gr::exception(std::format("Key '{}' must be tensor-like", key));
            return {};
        } else {
            const auto result = it->second.get_if<T>();
            if (result) {
                return *result;
            }

            std::string actualType = "<unknown>";
            pmt::ValueVisitor([&actualType]<typename T0>(const T0& /*arg*/) { actualType = gr::meta::type_name<std::decay_t<T0>>(); }).visit(it->second);
            throw gr::exception(std::format("Key '{}' has invalid type: expected '{}', got '{}'", key, gr::meta::type_name<T>(), actualType));
        }
    };

    schedulerUi                = dashboard.value_or<bool>("scheduler_ui", false);
    layoutType                 = magic_enum::enum_cast<DockingLayoutType>(dashboard.value_or<std::string>("layout", {}), magic_enum::case_insensitive).value_or(DockingLayoutType::Grid);
    const bool hasWindowLayout = dashboard.contains("windowLayout");
    if (hasWindowLayout) {
        if (const auto windowLayoutOpt = dashboard.find_value("windowLayout").value_or(gr::pmt::Value{}).get_if<gr::property_map>()) {
            windowLayout = *windowLayoutOpt;
        }
    }

    const auto plots = *readField.operator()<Tensor<pmt::Value>>(dashboard, "plots");

    for (const auto& plotPmt : plots) {
        if (!plotPmt.holds<property_map>()) {
            throw gr::exception("plot is not a property_map");
        }
        const property_map plotMap = *plotPmt.get_if<property_map>();

        const auto name        = *readField.operator()<std::string>(plotMap, "name");
        const auto plotSources = *readField.operator()<Tensor<pmt::Value>>(plotMap, "sources");
        auto       rect        = readField.operator()<Tensor<std::int64_t>>(plotMap, "rect", false);
        if (!rect && !hasWindowLayout && layoutType == DockingLayoutType::Free) {
            throw gr::exception("Missing one of two possible required keys for describing dashboard window UI:"
                                " [\"dashboard\"][\"windowLayout\"] or per-plot [\"rect\"] field");
        }
        if (rect && hasWindowLayout) {
            gr::log::warning("a plot rect and the dashboard windowLayout are both specified, the rect is ignored");
            rect = std::nullopt;
        }
        if (rect && rect->size() != 4) {
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

        auto blockPtr = emplaceChartBlock(graph, chartTypeName, name, chartParameters);
        if (!blockPtr) {
            components::Notification::warning(std::format("Failed to create chart block of type '{}'", chartTypeName));
            continue;
        }

        // Set uiConstraints on the block (axes config and window layout for chart to read in draw())
        blockPtr->uiConstraints()["axes"] = axesConfig;

        constexpr auto rectValue = [](const gr::pmt::Value& v) -> std::uint64_t {
            auto value = v.value_or<std::int64_t>(0);
            return value < 0 ? 0ULL : static_cast<std::uint64_t>(value);
        };

        UIWindow uiWindow(DeserializeTag{}, std::move(blockPtr), name);

        if (rect) {
            uiWindow.window->freeLayoutPosition = {
                .x      = static_cast<std::size_t>(rectValue((*rect)[0])),
                .y      = static_cast<std::size_t>(rectValue((*rect)[1])),
                .width  = static_cast<std::size_t>(rectValue((*rect)[2])),
                .height = static_cast<std::size_t>(rectValue((*rect)[3])),
            };
        }

        uiWindows.push_back(std::move(uiWindow));
    }

    if (dashboard.contains("exportedProperties")) {
        if (const auto expOpt = dashboard.find_value("exportedProperties").value_or(gr::pmt::Value{}).get_if<gr::property_map>()) {
            this->exportedProperties = *expOpt;
        }
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
                        propertyControlWindows.try_emplace(*id, DeserializeTag{}, std::string(*label), controlWindowName);
                    }
                }
            }
        }
    }
}

void Dashboard::saveStore(const gr::property_map& headerYaml, const gr::property_map& graphYaml) {
    using namespace gr;
    const auto headerYamlStr = pmt::yaml::serialize(headerYaml);
    const auto graphYamlStr  = pmt::yaml::serialize(graphYaml);
    if (description->storageInfo->path.starts_with("http://") || description->storageInfo->path.starts_with("https://")) {
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
#ifndef EMSCRIPTEN
        auto path = std::filesystem::path(description->storageInfo->path);

        std::ofstream stream(path / description->filename, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open()) {
            auto msg = std::format("can't open file for writing");
            components::Notification::warning(msg);
            return;
        }

        constexpr std::uint32_t headerStart = 32;
        const auto              headerSize  = static_cast<std::uint32_t>(headerYamlStr.size());
        const auto              graphStart  = headerStart + headerSize + 1;
        const auto              graphSize   = static_cast<std::uint32_t>(graphYamlStr.size());
        // The reader expects the flowgraph offset and size at byte 16.
        const std::array<std::uint32_t, 8> offsets{headerStart, headerSize, 0, 0, graphStart, graphSize, 0, 0};
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
        gr::Tensor<gr::pmt::Value> blocksWithoutCharts;
        for (const gr::pmt::Value& block : savedBlocks->owned()) {
            if (!isChartTypeName(block.value_or(gr::property_map{}).value_or<std::string>("id", {}))) {
                blocksWithoutCharts.emplace_back(block);
            }
        }
        graphYaml["blocks"] = std::move(blocksWithoutCharts);
    }
    property_map dashboardYaml;

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
    dashboardYaml["layout"]       = std::string(dockingLayoutName(layoutType));
    dashboardYaml["windowLayout"] = windowLayout;

    dashboardYaml["propertyControlWindows"] = [this] {
        gr::property_map out;
        for (const auto& [id, controlWindow] : propertyControlWindows) {
            gr::property_map windowProperties = {{"id", static_cast<gr::Size_t>(id)}, {"label", controlWindow.label}};
            out.try_emplace(std::pmr::string{controlWindow.window->name}, std::move(windowProperties));
        }
        return out;
    }();

    dashboardYaml["exportedProperties"] = [this] {
        auto             exported = this->session.graphModel.recursiveGatherExportedProperties();
        gr::property_map properties;
        for (const auto& [block, exportedPropertiesPtr] : exported) {
            gr::property_map blockProperties;
            blockProperties.reserve(static_cast<std::uint32_t>(exportedPropertiesPtr->size()));
            for (const auto& [propertyName, info] : *exportedPropertiesPtr) {
                blockProperties.try_emplace(std::pmr::string{propertyName}, info.windowId ? gr::pmt::Value{static_cast<gr::Size_t>(*info.windowId)} : gr::pmt::Value{});
            }
            properties[std::pmr::string{block}] = blockProperties;
        }
        return properties;
    }();

    gr::Tensor<gr::pmt::Value> plots;
    // Iterate UIWindows for chart serialization (new API)
    for (const auto& w : uiWindows) {
        if (!w.block) {
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
            if (const auto axesPmt = constraints.find_value("axes")) {
                plotAxes = axesPmt->value_or(gr::Tensor<gr::pmt::Value>{});
            }
        }
        plotMap["axes"] = plotAxes;

        // Serialize signal sinks from data_sinks property
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
    return name.empty() ? std::string(block.uniqueName()) : name;
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
    pendingCharts.try_emplace(chartName);
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", resolveChartTypeName(chartType.empty() ? "XYChart" : chartType)}, {"properties", std::move(properties)}});
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

    gr::property_map properties{{"chart_name", newName}};
    if (const auto sinkNames = grc_compat::getBlockSinkNames(&sourceBlock); !sinkNames.empty()) {
        properties["data_sinks"] = grc_compat::sinkNamesTensor(sinkNames);
    }
    gr::property_map constraints = sourceBlock.uiConstraints(); // without the window position
    constraints.erase("window");
    pendingCharts.insert_or_assign(newName, std::move(constraints));
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", std::string(sourceBlock.typeName())}, {"properties", std::move(properties)}});
}

bool Dashboard::transmuteUIWindow(UIWindow& win, std::string_view newChartType) {
    if (!win.block) {
        return false;
    }
    const std::string newTypeName = resolveChartTypeName(newChartType);
    if (newTypeName == win.block->typeName()) {
        return true;
    }
    const std::string windowName = win.window ? win.window->name : std::string(win.block->uniqueName());
    gr::property_map  properties{{"chart_name", windowName}};
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
            uiWindows.emplace_back(DeserializeTag{}, block, name);
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

std::pair<std::size_t, Dashboard::PropertyControlWindow&> Dashboard::newPropertyControlWindow(UiGraphBlock* block, std::string_view propertyName, std::string_view label) {
    static std::size_t lastUsedId = 0;
    do {
        ++lastUsedId;
    } while (this->propertyControlWindows.contains(lastUsedId));
    auto [iter, _] = this->propertyControlWindows.try_emplace(lastUsedId, *this, block, propertyName, label);
    return {lastUsedId, iter->second};
}

void Dashboard::applyExportedPropertiesToUiGraph() {
    for (const auto& [blockNameKey, mapValue] : this->exportedProperties) {
        const auto exportedPropertiesForThisBlock = mapValue.get_if<gr::property_map>();
        auto*      block                          = session.graphModel.recursiveFindBlockByName(blockNameKey).block;
        if (block && exportedPropertiesForThisBlock) {
            for (const auto& [propertyName, maybeWindowId] : *exportedPropertiesForThisBlock) {
                auto optionalWindowId = maybeWindowId.is_unsigned_integral() ? std::optional<gr::Size_t>{maybeWindowId.value_or<>(gr::Size_t{})} : std::optional<gr::Size_t>{};
                block->exportedProperties.try_emplace(std::string{propertyName}, optionalWindowId);
            }
        }
    }
    this->exportedProperties = {};
}

void Dashboard::removeSinkFromPlots(std::string_view sinkName) {
    for (auto& w : uiWindows) {
        if (w.block) {
            auto names = grc_compat::getBlockSinkNames(w.block.get());
            std::erase(names, std::string(sinkName));
            grc_compat::setBlockSinkNames(w.block.get(), names);
        }
    }
    std::erase_if(uiWindows, [this](const UIWindow& w) {
        if (!w.block || !grc_compat::getBlockSinkNames(w.block.get()).empty()) {
            return false;
        }
        session.sendToScheduler(gr::scheduler::property::kRemoveBlock, {{"uniqueName", std::string(w.block->uniqueName())}});
        return true;
    });
}

void Dashboard::loadUIWindowSources() {

    for (auto& w : uiWindows) {
        if (!w.block) {
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
    if (!uri || !restClient) {
        return;
    }

    const auto flowgraphUri = opencmw::URI<>::UriFactory(*uri).path("/flowgraph").setQuery({}).build().str();
    gr::log::debug("block {} adds subscription to remote flowgraph service: {} -> {}", blockName, uri->str(), flowgraphUri);
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

    gr::property_map properties{{"remote_uri", uriStr}, {"signal_name", signalData.signalName}, {"signal_unit", signalData.unit}};
    session.sendToScheduler(gr::scheduler::property::kEmplaceBlock, {{"type", std::move(blockType) + std::move(blockParams)}, {"properties", std::move(properties)}});
}

void Dashboard::Service::reload() {
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Get;
    command.topic    = opencmw::URI<>(uri);
    command.callback = [this](const opencmw::mdp::Message& rep) {
        auto buf = rep.data;

        opendigitizer::flowgraph::SerialisedFlowgraphMessage serialisedMessage;
        opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buf, serialisedMessage);

        gr::Message message      = opendigitizer::gnuradio::deserialiseMessage(serialisedMessage.data);
        auto        newFlowgraph = opendigitizer::flowgraph::getFlowgraphFromMessage(message);

        if (newFlowgraph) {
            EventLoop::instance().executeLater([this, newGrc = std::move(newFlowgraph->serialisedFlowgraph), newLayout = std::move(newFlowgraph->serialisedUiLayout)]() mutable {
                this->grc    = std::move(newGrc);
                this->layout = std::move(newLayout);
            });
        } else {
            EventLoop::instance().executeLater([] { components::Notification::warning("Error reading flowgraph from the service reply"); });
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
    command.callback = [](const opencmw::mdp::Message& rep) {
        if (!rep.error.empty()) {
            EventLoop::instance().executeLater([error = rep.error] { components::Notification::warning(error); });
        }
    };
    restClient->request(command);
}

std::string Dashboard::resolveChartTypeName(std::string_view chartTypeName) const {
    using namespace opendigitizer::charts;
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
        if (registered.ends_with(requested) || requested.ends_with(registered.substr(registered.rfind("::") + 2))) {
            return registeredType;
        }
    }
    return std::string(kDefaultChartType);
}

std::shared_ptr<gr::BlockModel> Dashboard::emplaceChartBlock(gr::Graph& graph, std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters) {
    gr::property_map initParams = chartParameters;
    if (!chartName.empty()) {
        initParams.try_emplace("chart_name", chartName);
    }
    auto blockModel = graph.emplaceBlock(resolveChartTypeName(chartTypeName), initParams);
    return blockModel ? std::move(*blockModel) : nullptr;
}

void Dashboard::Service::execute() {
    opencmw::client::Command command;
    command.command = opencmw::mdp::Command::Set;

    FlowgraphMessage request;
    request.flowgraph = this->grc;
    request.layout    = this->layout;
    opencmw::serialise<opencmw::Json>(command.data, request);

    command.topic    = opencmw::URI<>(uri);
    command.callback = [](const opencmw::mdp::Message& rep) {
        if (!rep.error.empty()) {
            EventLoop::instance().executeLater([error = rep.error] { components::Notification::warning(error); });
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

            auto dashboardDescription          = std::make_shared<DashboardDescription>(PrivateTag{}, std::filesystem::path(name).stem().native(), storageInfo, name, isFavorite, lastUsed);
            dashboardDescription->tags         = readStringList(rootMap, "tags");
            dashboardDescription->keyValueTags = readStringMap(rootMap, "keyValueTags");
            cb(std::move(dashboardDescription));
        },
        [cb]() { cb({}); });
}

void DashboardDescription::loadFlowgraphAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, std::function<void(std::string&&)>&& cb, std::function<void()>&& errCb) {
    fetch(std::move(client), storageInfo, filename, {What::Flowgraph}, [callback = std::move(cb)](std::array<std::string, 1>&& data) mutable { callback(std::move(data[0])); }, std::move(errCb));
}

std::shared_ptr<const DashboardDescription> DashboardDescription::createEmpty(const std::string& name) { return std::make_shared<DashboardDescription>(PrivateTag{}, name, DashboardStorageInfo::memoryDashboardStorage(), std::string{}, false, std::nullopt); }
} // namespace DigitizerUi
