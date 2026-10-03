#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "GraphModel.hpp"
#include "Scheduler.hpp"

#include "charts/Chart.hpp"

#include "components/ColourManager.hpp"
#include "components/Docking.hpp"
#include "components/ExportedPropertiesList.hpp"

#include "utils/TransparentStringHash.hpp"

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "PluginPaths.hpp"

#include <RestClient.hpp>

#include <cmrc/cmrc.hpp>

#ifdef EMSCRIPTEN
#include "utils/emscripten_compat.hpp"
#endif

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

CMRC_DECLARE(sample_dashboards);

namespace detail {
[[maybe_unused]] inline static opendigitizer::ColourManager& _colourManager = opendigitizer::ColourManager::instance();
}

namespace gr {
class Graph;
class BlockModel;
} // namespace gr

namespace opendigitizer {
struct SignalSink; // forward declaration
}

namespace DigitizerUi {

struct DashboardDescription;
struct SignalData;

// Use axis types from charts namespace (avoid duplication)
using AxisScale   = opendigitizer::charts::AxisScale;
using LabelFormat = opendigitizer::charts::LabelFormat;

struct DashboardStorageInfo {
    struct PrivateTag {};

    std::string path;
    bool        isEnabled = true;

    DashboardStorageInfo(std::string _path, PrivateTag) : path(std::move(_path)) {}
    ~DashboardStorageInfo() noexcept;

    static std::shared_ptr<DashboardStorageInfo>             get(std::string_view path);
    static std::vector<std::weak_ptr<DashboardStorageInfo>>& knownDashboardStorage();
    static std::shared_ptr<DashboardStorageInfo>             memoryDashboardStorage();

    bool isInMemoryDashboardStorage() const { return this == memoryDashboardStorage().get(); }
};

struct DashboardDescription {
    struct PrivateTag {};
    using OptionalTimePoint = std::optional<std::chrono::time_point<std::chrono::system_clock>>;
    using StringMap         = std::unordered_map<std::string, std::string, opendigitizer::TransparentStringHash, std::equal_to<>>;

    static constexpr const char* fileExtension = ".ddd";

    std::string                           name;
    std::shared_ptr<DashboardStorageInfo> storageInfo;
    std::string                           filename;
    mutable bool                          isFavorite;
    mutable OptionalTimePoint             lastUsed;
    std::vector<std::string>              tags;
    StringMap                             keyValueTags;

    DashboardDescription(PrivateTag, std::string _name, std::shared_ptr<DashboardStorageInfo> _storageInfo, std::string _filename, bool _isFavorite, OptionalTimePoint _lastUsed) : name(std::move(_name)), storageInfo(std::move(_storageInfo)), filename(std::move(_filename)), isFavorite(_isFavorite), lastUsed(std::move(_lastUsed)) {}

    void save();

    static void                                        loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, const std::function<void(std::shared_ptr<const DashboardDescription>&&)>& cb);
    static void                                        loadFlowgraphAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, std::function<void(std::string&&)>&& cb, std::function<void()>&& errCb);
    static std::shared_ptr<const DashboardDescription> createEmpty(const std::string& name);
};

struct Dashboard {
    using AxisConfig = opendigitizer::charts::AxisConfig;
    struct PrivateTag {};
    struct DeserializeTag {};

    struct UIWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::shared_ptr<gr::BlockModel>    block;

        UIWindow() = default;
        explicit UIWindow(Dashboard& dashboard, std::shared_ptr<gr::BlockModel> blk, std::string_view name = "");
        explicit UIWindow(DeserializeTag, std::shared_ptr<gr::BlockModel> blk, std::string_view name = "");

        [[nodiscard]] bool           hasBlock() const noexcept { return block != nullptr; }
        [[nodiscard]] bool           isChart() const noexcept { return uiCategory() == gr::UICategory::Content; }
        [[nodiscard]] gr::UICategory uiCategory() const noexcept { return block ? block->uiCategory() : gr::UICategory::None; }
    };

    struct PropertyControlWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::string                        label; // label for the editor widget, chosen by user

        PropertyControlWindow(DeserializeTag, std::string_view labelView, std::string_view windowName);

        // all the parameters of these constructors are just to know how to generate a good and unique name for the window
        PropertyControlWindow(Dashboard& dashboard, UiGraphModel* graphModel, std::string_view propertyName, std::string_view labelView, std::string_view blockName);
        PropertyControlWindow(Dashboard& dashboard, UiGraphBlock* block, std::string_view propertyName, std::string_view labelView);
    };

    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        auto pluginPaths = Digitizer::resolvePluginSearchPaths();
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), gr::globalSchedulerRegistry(), std::span<const std::string>(pluginPaths));
    }();

    std::atomic<bool>               isInUse = false;
    std::function<void(Dashboard*)> requestClose;

    std::shared_ptr<opencmw::client::RestClient>           restClient;
    std::shared_ptr<const DashboardDescription>            description = nullptr;
    std::vector<UIWindow>                                  uiWindows;
    std::unordered_map<std::size_t, PropertyControlWindow> propertyControlWindows;
    DockingLayoutType                                      layoutType  = DockingLayoutType::Grid;
    bool                                                   schedulerUi = false; // play/pause/stop controls shown (.grc dashboard.scheduler_ui)
    gr::property_map                                       windowLayout;
    gr::property_map                                       exportedProperties;
    std::atomic<bool>                                      isInitialised = false;
    Scheduler                                              scheduler;
    gr::Graph                                              uiGraph{*pluginLoader};
    UiGraphModel                                           graphModel;

    explicit Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc);
    ~Dashboard();

    static std::unique_ptr<Dashboard> create(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc);

    void load();
    void loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler);
    void loadPlugins(std::function<void()> done);
    /// saves with the layout the view shows now; it becomes the dashboard's stored layout
    void save(DockingLayoutType liveLayoutType, const gr::property_map& liveWindowLayout);
    /// header and graph YAML (flowgraph plus the dashboard section) with the given layout
    [[nodiscard]] std::pair<gr::property_map, gr::property_map> serialise();
    void                                                        saveStore(const gr::property_map& headerYaml, const gr::property_map& graphYaml);
    void                                                        doLoad(const gr::property_map& dashboard);

    UIWindow& newUIBlock(std::string_view chartType = "XYChart", const gr::property_map& chartInitialParameters = {});
    void      deleteChart(UIWindow* uiWindow);
    UIWindow* copyChart(std::string_view sourceChartId);
    bool      transmuteUIWindow(UIWindow& uiWindow, std::string_view newChartType);

    std::pair<std::size_t, PropertyControlWindow&> newPropertyControlWindow(UiGraphBlock* block, std::string_view propertyName, std::string_view label);
    void                                           applyExportedPropertiesToUiGraph();
    [[nodiscard]] constexpr bool                   hasPendingExportedPropertiesConfiguration() const noexcept { return !this->exportedProperties.empty(); }
    std::string                                    generateUniqueNameForPropertyControlWindow(UiGraphBlock* block, std::string_view propertyName);
    std::string                                    generateUniqueNameForUiWindow(gr::BlockModel& block, std::string_view desiredName);
    std::unordered_set<std::string_view>           getAllWindowNamesInUse() const;

    gr::BlockModel* emplaceChartBlock(std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters = {});
    void            removeSinkFromPlots(std::string_view sinkName);
    void            addRemoteSignal(const SignalData& signalData);
    void            loadUIWindowSources();

    void setNewDescription(const std::shared_ptr<DashboardDescription>& desc);

    template<typename... Args>
    void emplaceGraph(Args&&... args) {
        scheduler.emplaceGraph(std::forward<Args>(args)...);
    }

    void handleMessages() {
        scheduler.handleMessages(graphModel);

        if (hasPendingExportedPropertiesConfiguration()) [[unlikely]] {
            const auto* schedulerInfo = std::get_if<UiGraphBlock::SchedulerBlockInfo>(&graphModel.rootBlock.blockCategoryInfo);
            if (schedulerInfo && schedulerInfo->childrenLoaded) {
                applyExportedPropertiesToUiGraph();
            }
        }
    }

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

    UIWindow& getOrCreateUIWindow(const std::shared_ptr<gr::BlockModel>& block) {
        if (auto* existing = findUIWindow(block)) {
            return *existing;
        }
        uiWindows.emplace_back(*this, block);
        return uiWindows.back();
    }

    void removeUIWindow(const std::shared_ptr<gr::BlockModel>& block) {
        std::erase_if(uiWindows, [&block](const UIWindow& w) { return w.block == block; });
    }
};
} // namespace DigitizerUi

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

inline void setBlockSinkNames(gr::BlockModel* block, const std::vector<std::string>& names) {
    if (block) {
        gr::Tensor<gr::pmt::Value> sinks(gr::extents_from, {names.size()});
        for (std::size_t i = 0; i < names.size(); ++i) {
            sinks[i] = names[i];
        }
        std::ignore = block->settings().set(gr::property_map{{std::pmr::string("data_sinks"), sinks}});
        std::ignore = block->settings().activateContext();
        std::ignore = block->settings().applyStagedParameters();
    }
}

} // namespace grc_compat

#endif
