#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(sample_dashboards);

#include <RestClient.hpp>

#ifdef EMSCRIPTEN
#include "utils/emscripten_compat.hpp"
#endif
#include <plf_colony.h>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "components/ColourManager.hpp"
#include "components/Docking.hpp"

namespace detail {
[[maybe_unused]] inline static opendigitizer::ColourManager& _colourManager = opendigitizer::ColourManager::instance();
}

#include "GraphModel.hpp"
#include "Scheduler.hpp"
#include "settings.hpp"

#include "charts/Charts.hpp"

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

    static constexpr const char* fileExtension = ".ddd";

    std::string                           name;
    std::shared_ptr<DashboardStorageInfo> storageInfo;
    std::string                           filename;
    mutable bool                          isFavorite;
    mutable OptionalTimePoint             lastUsed;

    static std::vector<std::shared_ptr<DashboardDescription>> s_knownDashboards;

    DashboardDescription(PrivateTag, std::string _name, std::shared_ptr<DashboardStorageInfo> _storageInfo, std::string _filename, bool _isFavorite, OptionalTimePoint _lastUsed) : name(std::move(_name)), storageInfo(std::move(_storageInfo)), filename(std::move(_filename)), isFavorite(_isFavorite), lastUsed(std::move(_lastUsed)) {}

    void save();

    static void                                        loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, const std::function<void(std::shared_ptr<const DashboardDescription>&&)>& cb);
    static std::shared_ptr<const DashboardDescription> createEmpty(const std::string& name);
};

struct Dashboard {
    using AxisConfig = opendigitizer::charts::AxisConfig;
    struct PrivateTag {};

    struct UIWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::shared_ptr<gr::BlockModel>    block;

        UIWindow() = default;
        explicit UIWindow(std::shared_ptr<gr::BlockModel> blk, std::string_view name = "") : window(std::make_shared<DockSpace::Window>(name.empty() ? std::string(blk->uniqueName()) : std::string(name))), block(std::move(blk)) {}

        [[nodiscard]] bool           hasBlock() const noexcept { return block != nullptr; }
        [[nodiscard]] bool           isChart() const noexcept { return uiCategory() == gr::UICategory::ChartPane; }
        [[nodiscard]] gr::UICategory uiCategory() const noexcept { return block ? block->uiCategory() : gr::UICategory::None; }
    };

    struct Service {
        std::shared_ptr<opencmw::client::RestClient> restClient;
        std::string                                  name;
        std::string                                  uri;
        std::string                                  layout;
        std::string                                  grc;

        Service(std::shared_ptr<opencmw::client::RestClient> client, std::string n, std::string u) : restClient{std::move(client)}, name(std::move(n)), uri(std::move(u)) {}

        void reload();
        void execute();
        void emplaceBlock(std::string type, std::string params);
    };

    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        std::vector<std::filesystem::path> pluginPaths;
#ifndef __EMSCRIPTEN__
        pluginPaths.push_back(std::filesystem::current_path() / "plugins");
#endif
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), gr::globalSchedulerRegistry(), pluginPaths);
    }();
    std::atomic<bool>               isInUse = false;
    std::function<void(Dashboard*)> requestClose;

    std::shared_ptr<opencmw::client::RestClient> restClient;
    std::shared_ptr<const DashboardDescription>  description = nullptr;
    std::vector<UIWindow>                        uiWindows;
    DockingLayoutType                            layout;
    std::unordered_map<std::string, std::string> flowgraphUriByRemoteSource;
    plf::colony<Service>                         services;
    std::atomic<bool>                            isInitialised = false;
    Scheduler                                    scheduler;
    gr::Graph                                    uiGraph{*pluginLoader};
    UiGraphModel                                 graphModel;

    explicit Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc);
    ~Dashboard();

    static std::unique_ptr<Dashboard> create(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<const DashboardDescription>& desc);

    void load();
    void loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler);
    void save();
    void doLoad(const gr::property_map& dashboard);

    UIWindow& newUIBlock(int x, int y, int w, int h, std::string_view chartType = "XYChart");
    void      deleteChart(UIWindow* uiWindow);
    UIWindow* copyChart(std::string_view sourceChartId);
    bool      transmuteUIWindow(UIWindow& uiWindow, std::string_view newChartType);

    gr::BlockModel* emplaceChartBlock(std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters = {});
    void            removeSinkFromPlots(std::string_view sinkName);
    void            addRemoteSignal(const SignalData& signalData);
    void            loadUIWindowSources();

    void setNewDescription(const std::shared_ptr<DashboardDescription>& desc);
    void registerRemoteService(std::string_view blockName, std::optional<opencmw::URI<>> uri);
    void unregisterRemoteService(std::string_view blockName);
    void removeUnusedRemoteServices();
    void saveRemoteServiceFlowgraph(Service* s);

    template<typename TScheduler, typename... Args>
    void emplaceScheduler(Args&&... args) {
        scheduler.emplaceScheduler<TScheduler, Args...>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    void emplaceGraph(Args&&... args) {
        scheduler.emplaceGraph(std::forward<Args>(args)...);
    }

    void handleMessages() { scheduler.handleMessages(graphModel); }

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
        uiWindows.emplace_back(block);
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
        if (auto* vec = std::get_if<std::vector<std::string>>(&it->second)) {
            return *vec;
        }
    }
    return {};
}

inline void setBlockSinkNames(gr::BlockModel* block, const std::vector<std::string>& names) {
    if (block) {
        std::ignore = block->settings().set({{"data_sinks", names}});
        std::ignore = block->settings().activateContext();
        std::ignore = block->settings().applyStagedParameters();
    }
}

} // namespace grc_compat

#endif
