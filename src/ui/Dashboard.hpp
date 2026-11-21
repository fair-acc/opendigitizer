#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "GraphModel.hpp"
#include "GraphSession.hpp"

#include "charts/Chart.hpp"

#include "components/Docking.hpp"
#include "components/SignalColours.hpp"

#include "utils/TransparentStringHash.hpp"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "PluginPaths.hpp"

#include <RestClient.hpp>

#include <cmrc/cmrc.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

CMRC_DECLARE(sample_dashboards);

namespace gr {
class Graph;
class BlockModel;
} // namespace gr

namespace gr::od {

struct SignalSink;
struct DashboardDescription;
struct SignalData;

using AxisScale   = charts::AxisScale;
using LabelFormat = charts::LabelFormat;

enum class StorageKind { Memory, Remote, Example, File };

[[nodiscard]] constexpr bool isRemotePath(std::string_view path) noexcept { return path.starts_with("http://") || path.starts_with("https://"); }

struct DashboardStorageInfo {
    std::string path;
    bool        isEnabled = true;

    ~DashboardStorageInfo() noexcept;

    static std::shared_ptr<DashboardStorageInfo>             get(std::string_view path);
    static std::vector<std::weak_ptr<DashboardStorageInfo>>& knownDashboardStorage();
    static std::shared_ptr<DashboardStorageInfo>             memoryDashboardStorage();

    [[nodiscard]] bool        isInMemoryDashboardStorage() const { return this == memoryDashboardStorage().get(); }
    [[nodiscard]] StorageKind kind() const {
        if (isInMemoryDashboardStorage()) {
            return StorageKind::Memory;
        }
        if (isRemotePath(path)) {
            return StorageKind::Remote;
        }
        return path.starts_with("example://") ? StorageKind::Example : StorageKind::File;
    }
};

struct DashboardDescription {
    using OptionalTimePoint = std::optional<std::chrono::time_point<std::chrono::system_clock>>;
    using StringMap         = std::unordered_map<std::string, std::string, TransparentStringHash, std::equal_to<>>;

    static constexpr const char* kFileExtension = ".ddd";

    std::string                           name;
    std::shared_ptr<DashboardStorageInfo> storageInfo;
    std::string                           filename;
    bool                                  isFavorite = false;
    OptionalTimePoint                     lastUsed;
    std::vector<std::string>              tags;
    StringMap                             keyValueTags;

    static void                                  loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, const std::function<void(std::shared_ptr<DashboardDescription>&&)>& cb);
    static void                                  loadFlowgraphAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, std::function<void(std::string&&)>&& cb, std::function<void()>&& errCb);
    static std::shared_ptr<DashboardDescription> createEmpty(const std::string& name);
};

struct Dashboard {
    using AxisConfig = charts::AxisConfig;

    struct UIWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::shared_ptr<gr::BlockModel>    block;
    };

    struct PropertyControlWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::string                        label;
    };

    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        auto pluginPaths = digitizer::resolvePluginSearchPaths();
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), gr::globalSchedulerRegistry(), std::span<const std::string>(pluginPaths));
    }();

    std::atomic<bool>               isInUse = false;
    std::function<void(Dashboard*)> requestClose;

    std::shared_ptr<opencmw::client::RestClient>           restClient;
    std::shared_ptr<DashboardDescription>                  description = nullptr;
    std::vector<UIWindow>                                  uiWindows;
    std::unordered_map<std::size_t, PropertyControlWindow> propertyControlWindows;
    DockingLayoutType                                      layoutType  = DockingLayoutType::Grid;
    bool                                                   schedulerUi = false; // .grc dashboard.scheduler_ui
    bool                                                   editable    = true;  // .grc dashboard.editable: false starts locked
    gr::property_map                                       windowLayout;
    std::atomic<bool>                                      isInitialised = false;
    GraphSession                                           session;
    std::unordered_map<std::string, gr::property_map>      pendingCharts; // by chart_name, until in the graph
    std::uint64_t                                          boundChartsTopology = 0;

    Dashboard(std::shared_ptr<opencmw::client::RestClient> client, std::shared_ptr<DashboardDescription> desc);

    void                                                        load();
    void                                                        loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler);
    void                                                        loadPlugins(std::function<void()> done);
    void                                                        save(DockingLayoutType liveLayoutType, const gr::property_map& liveWindowLayout);
    [[nodiscard]] std::pair<gr::property_map, gr::property_map> serialise();
    void                                                        saveStore(const gr::property_map& headerYaml, const gr::property_map& graphYaml);
    [[nodiscard]] std::expected<void, gr::Error>                doLoad(const gr::property_map& dashboard, gr::Graph& graph);

    void newUIBlock(std::string_view chartType = "XYChart", const gr::property_map& chartInitialParameters = {});
    void deleteChart(UIWindow* uiWindow);
    void copyChart(std::string_view sourceChartId);
    bool transmuteUIWindow(UIWindow& uiWindow, std::string_view newChartType);
    void bindChartWindows();

    std::unordered_set<std::string_view> getAllWindowNamesInUse() const;

    std::shared_ptr<gr::BlockModel> emplaceChartBlock(gr::Graph& graph, std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters = {});
    [[nodiscard]] std::string       resolveChartTypeName(std::string_view chartTypeName) const;
    void                            addRemoteSignal(const SignalData& signalData);
    void                            loadUIWindowSources();

    void setNewDescription(const std::shared_ptr<DashboardDescription>& desc);

    [[nodiscard]] UIWindow* findUIWindow(const std::shared_ptr<gr::BlockModel>& block) noexcept {
        if (!block) {
            return nullptr;
        }
        for (auto& w : uiWindows) {
            if (w.block == block) {
                return &w;
            }
        }
        return nullptr;
    }

    [[nodiscard]] UIWindow* findUIWindowByName(std::string_view uniqueName) noexcept {
        for (auto& w : uiWindows) {
            if (w.block && w.block->uniqueName() == uniqueName) {
                return &w;
            }
        }
        return nullptr;
    }
};
} // namespace gr::od

namespace grc_compat {

inline std::vector<std::string> getBlockSinkNames(const gr::BlockModel* block) {
    if (!block) {
        return {};
    }
    const auto& settings = block->settings().get();
    if (auto it = settings.find("data_sinks"); it != settings.end()) {
        const gr::pmt::Value sinksVal = it->second;
        if (const auto sinks = sinksVal.get_if<gr::TensorView<gr::pmt::Value>>()) {
            std::vector<std::string> result;
            result.reserve(sinks->size());
            for (const auto& sink : *sinks) {
                result.push_back(sink.value_or(std::string()));
            }
            return result;
        }
    }
    return {};
}

[[nodiscard]] inline gr::Tensor<gr::pmt::Value> sinkNamesTensor(const std::vector<std::string>& names) {
    gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {names.size()});
    std::ranges::copy(names, sinks.begin());
    return sinks;
}

inline void setBlockSinkNames(gr::BlockModel* block, const std::vector<std::string>& names) {
    if (block) {
        std::ignore = block->settings().set(gr::property_map{{std::pmr::string("data_sinks"), sinkNamesTensor(names)}});
        std::ignore = block->settings().activateContext();
        std::ignore = block->settings().applyStagedParameters();
    }
}

} // namespace grc_compat

#endif
