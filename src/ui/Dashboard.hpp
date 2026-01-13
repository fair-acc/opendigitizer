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
class SignalSink; // Forward declaration for SignalSink interface
}

namespace DigitizerUi {

struct DashboardDescription;
struct SignalData;

// Use axis types from charts namespace (avoid duplication)
using AxisScale   = opendigitizer::charts::AxisScale;
using LabelFormat = opendigitizer::charts::LabelFormat;

// Defines where the dashboard is stored and fetched from
struct DashboardStorageInfo {
private:
    struct PrivateTag {};

public:
    DashboardStorageInfo(std::string _path, PrivateTag) : path(std::move(_path)) {}

    ~DashboardStorageInfo() noexcept;

    std::string path;
    bool        isEnabled = true;

    static std::shared_ptr<DashboardStorageInfo>             get(std::string_view path);
    static std::vector<std::weak_ptr<DashboardStorageInfo>>& knownDashboardStorage();
    static std::shared_ptr<DashboardStorageInfo>             memoryDashboardStorage();

    bool isInMemoryDashboardStorage() const { return this == memoryDashboardStorage().get(); }
};

struct DashboardDescription {
private:
    struct PrivateTag {};

    using OptionalTimePoint = std::optional<std::chrono::time_point<std::chrono::system_clock>>;

    static std::vector<std::shared_ptr<DashboardDescription>> s_knownDashboards;

public:
    DashboardDescription(PrivateTag, std::string _name, std::shared_ptr<DashboardStorageInfo> _storageInfo, std::string _filename, bool _isFavorite, OptionalTimePoint _lastUsed) : name(std::move(_name)), storageInfo(std::move(_storageInfo)), filename(std::move(_filename)), isFavorite(_isFavorite), lastUsed(std::move(_lastUsed)) {}

    static constexpr const char* fileExtension = ".ddd"; // ddd for "Digitizer Dashboard Description"

    std::string                           name;
    std::shared_ptr<DashboardStorageInfo> storageInfo;
    std::string                           filename;

    mutable bool              isFavorite;
    mutable OptionalTimePoint lastUsed;

    void save();

    static void loadAndThen(std::shared_ptr<opencmw::client::RestClient> client, const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, const std::function<void(std::shared_ptr<const DashboardDescription>&&)>& cb);

    static std::shared_ptr<const DashboardDescription> createEmpty(const std::string& name);
};

class Dashboard {
public:
    // Alias for axis configuration (defined in ChartUtils.hpp)
    using AxisConfig = opendigitizer::charts::DashboardAxisConfig;

    /**
     * @brief UIWindow - Maps a UI block to its layout window.
     *
     * Generic window-to-block mapping for all drawable UI elements (charts, toolbars, etc.).
     * The block is the source of truth; the window provides layout/docking information.
     * Sink management is done via the block's settings interface (data_sinks property).
     */
    struct UIWindow {
        std::shared_ptr<DockSpace::Window> window;
        std::shared_ptr<gr::BlockModel>    block; // Shared ownership with m_uiGraph

        UIWindow() = default;
        explicit UIWindow(std::shared_ptr<gr::BlockModel> blk, std::string_view name = "")
            : window(std::make_shared<DockSpace::Window>(name.empty() ? std::string(blk->uniqueName()) : std::string(name)))
            , block(std::move(blk)) {}

        [[nodiscard]] bool hasBlock() const noexcept { return block != nullptr; }
        [[nodiscard]] bool isChart() const noexcept { return uiCategory() == gr::UICategory::ChartPane; }
        [[nodiscard]] gr::UICategory uiCategory() const noexcept {
            return block ? block->uiCategory() : gr::UICategory::None;
        }

        // TODO(deprecated): Remove sinkNames()/setSinkNames() - chart-specific code that shouldn't be in Dashboard.
        // Sink management belongs exclusively in charts. These helpers exist temporarily for backward compatibility
        // with the 'sources:' section in .grc files (doLoad transfers 'sources:' to block's 'data_sinks' setting).
        // Once 'sources:' is deprecated and charts define 'data_sinks' directly in their 'parameters:' section,
        // these helpers and related Dashboard code (loadUIWindowSources, removeSinkFromPlots) should be removed.

        /// @brief Get sink names from the block's data_sinks property
        /// @deprecated Use chart's data_sinks setting directly
        [[nodiscard]] std::vector<std::string> sinkNames() const {
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

        /// @brief Set sink names via the block's data_sinks property
        /// @deprecated Use chart's data_sinks setting directly
        void setSinkNames(const std::vector<std::string>& names) {
            if (block) {
                block->settings().set({{"data_sinks", names}});
            }
        }
    };

    // Legacy Plot struct - to be removed after migration to UIWindow
    struct Plot {
        std::string              name;
        std::string              chartTypeName = "XYChart"; // String-based chart type
        std::vector<std::string> sourceNames;

        // Chart block pointers (owned by Dashboard::m_uiGraph)
        gr::BlockModel*               chartBlock = nullptr; // BlockModel interface (for draw(), uiCategory())
        opendigitizer::charts::Chart* chart      = nullptr; // ChartInterface (for signal management)

        std::vector<AxisConfig> axes;

        // Helper to check if chart is valid
        [[nodiscard]] bool hasChart() const noexcept { return chart != nullptr && chartBlock != nullptr; }

        // Helper to clear signal sinks from chart
        void clearChartSinks() {
            if (chart) {
                chart->clearSignalSinks();
            }
        }

        std::shared_ptr<DockSpace::Window> window;

        Plot() {
            static int n = 1;
            name         = std::format("Plot {}", n++);
            window       = std::make_shared<DockSpace::Window>(name);
        }
    };

    struct Service {
        Service(std::shared_ptr<opencmw::client::RestClient> client, std::string n, std::string u) : restClient{std::move(client)}, name(std::move(n)), uri(std::move(u)) {}

        std::shared_ptr<opencmw::client::RestClient> restClient;
        std::string                                  name;
        std::string                                  uri;
        std::string                                  layout;
        std::string                                  grc;

        void reload();

        void execute();

        void emplaceBlock(std::string type, std::string params);
    };

private:
    std::shared_ptr<opencmw::client::RestClient> m_restClient;
    std::shared_ptr<const DashboardDescription>  m_desc = nullptr;
    std::vector<Plot>                            m_plots;      // Legacy - to be removed
    std::vector<UIWindow>                        m_uiWindows;  // New: window-block mapping for all UI elements
    DockingLayoutType                            m_layout;
    std::unordered_map<std::string, std::string> m_flowgraphUriByRemoteSource;
    plf::colony<Service>                         m_services;
    std::atomic<bool>                            m_isInitialised = false;

    Scheduler m_scheduler;

    // UI Graph: holds drawable blocks (charts, legends) rendered during UI loop
    // Separate from the signal processing graph managed by m_scheduler
    gr::Graph m_uiGraph;

    UiGraphModel m_graphModel;

    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        std::vector<std::filesystem::path> pluginPaths;
#ifndef __EMSCRIPTEN__
        // TODO set correct paths
        pluginPaths.push_back(std::filesystem::current_path() / "plugins");
#endif
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), gr::globalSchedulerRegistry(), pluginPaths);
    }();

private:
    class PrivateTag {};

    // Helper to create chart blocks in the UI Graph
    // Returns {BlockModel*, ChartInterface*} - both point to the same block
    std::pair<gr::BlockModel*, opendigitizer::charts::Chart*> emplaceChartBlock(std::string_view chartTypeName, const std::string& chartName, const gr::property_map& chartParameters = {});

public:
    explicit Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> restClient, const std::shared_ptr<const DashboardDescription>& desc);

    static std::unique_ptr<Dashboard> create(std::shared_ptr<opencmw::client::RestClient> restClient, const std::shared_ptr<const DashboardDescription>& desc);

    ~Dashboard();

    void load();

    void loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler);

    void save();

    /// @brief Create a new chart and return the UIWindow (new API)
    UIWindow& newChart(int x, int y, int w, int h, std::string_view chartType = "XYChart");

    /// @brief Legacy: Create a new plot (wraps newChart for backward compatibility)
    /// @deprecated Use newChart() instead
    DigitizerUi::Dashboard::Plot& newPlot(int x, int y, int w, int h, std::string_view chartType = "XYChart");

    void deletePlot(Plot* plot);

    /// @brief Delete a chart by UIWindow
    void deleteChart(UIWindow* uiWindow);

    /**
     * @brief Transmute a chart to a different type while preserving signals.
     *
     * @param plot The plot to transmute
     * @param newChartType Target chart type (e.g., "XYChart", "YYChart")
     * @return true if transmutation succeeded, false otherwise
     */
    bool transmuteChart(Plot& plot, std::string_view newChartType);

    /**
     * @brief Transmute a UIWindow's chart to a different type.
     * New API using UIWindow instead of Plot.
     */
    bool transmuteUIWindow(UIWindow& uiWindow, std::string_view newChartType);

    void removeSinkFromPlots(std::string_view sinkName);

    inline auto& plots() { return m_plots; }

    DockingLayoutType layout() { return m_layout; }

    void setNewDescription(const std::shared_ptr<DashboardDescription>& desc);

    inline std::shared_ptr<const DashboardDescription> description() const { return m_desc; }

    void registerRemoteService(std::string_view blockName, std::optional<opencmw::URI<>> uri);

    void unregisterRemoteService(std::string_view blockName);

    void removeUnusedRemoteServices();

    void saveRemoteServiceFlowgraph(Service* s);

    inline auto& remoteServices() { return m_services; }

    void addRemoteSignal(const SignalData& signalData);

    void loadPlotSources();

    void loadPlotSourcesFor(Plot& plot);

    /// @brief Load signal sinks for all UIWindows from the SinkRegistry
    void loadUIWindowSources();

    UiGraphModel& graphModel();

    std::atomic<bool> isInUse = false;

    std::function<void(Dashboard*)> requestClose;

    void doLoad(const gr::property_map& dashboard);

    template<typename TScheduler, typename... Args>
    void emplaceScheduler(Args&&... args) {
        m_scheduler.emplaceScheduler<TScheduler, Args...>(std::forward<Args>(args)...);
    }

    template<typename... Args>
    void emplaceGraph(Args&&... args) {
        m_scheduler.emplaceGraph(std::forward<Args>(args)...);
    }

    auto& scheduler() { return m_scheduler; }

    void handleMessages() { m_scheduler.handleMessages(m_graphModel); }

    [[nodiscard]] bool isInitialised() const noexcept { return m_isInitialised.load(std::memory_order_acquire); }

    // UI Graph accessors for drawable blocks (charts, legends, etc.)
    [[nodiscard]] gr::Graph&       uiGraph() noexcept { return m_uiGraph; }
    [[nodiscard]] const gr::Graph& uiGraph() const noexcept { return m_uiGraph; }

    // Find Plot by its chartBlock pointer (for uiGraph iteration) - Legacy
    [[nodiscard]] Plot* findPlotByBlock(gr::BlockModel* block) noexcept {
        if (!block) {
            return nullptr;
        }
        for (auto& plot : m_plots) {
            if (plot.chartBlock == block) {
                return &plot;
            }
        }
        return nullptr;
    }

    // --- UIWindow management (new API) ---

    /// Access all UI windows
    [[nodiscard]] std::vector<UIWindow>&       uiWindows() noexcept { return m_uiWindows; }
    [[nodiscard]] const std::vector<UIWindow>& uiWindows() const noexcept { return m_uiWindows; }

    /// Find UIWindow by block (returns nullptr if not found)
    [[nodiscard]] UIWindow* findUIWindow(const std::shared_ptr<gr::BlockModel>& block) noexcept {
        if (!block) {
            return nullptr;
        }
        for (auto& uiWindow : m_uiWindows) {
            if (uiWindow.block == block) {
                return &uiWindow;
            }
        }
        return nullptr;
    }

    /// Find UIWindow by block unique name
    [[nodiscard]] UIWindow* findUIWindowByName(std::string_view uniqueName) noexcept {
        for (auto& uiWindow : m_uiWindows) {
            if (uiWindow.block && uiWindow.block->uniqueName() == uniqueName) {
                return &uiWindow;
            }
        }
        return nullptr;
    }

    /// Get or create UIWindow for a block (lazy creation)
    UIWindow& getOrCreateUIWindow(const std::shared_ptr<gr::BlockModel>& block) {
        if (auto* existing = findUIWindow(block)) {
            return *existing;
        }
        m_uiWindows.emplace_back(block);
        return m_uiWindows.back();
    }

    /// Remove UIWindow for a block
    void removeUIWindow(const std::shared_ptr<gr::BlockModel>& block) {
        std::erase_if(m_uiWindows, [&block](const UIWindow& w) { return w.block == block; });
    }
};
} // namespace DigitizerUi

#endif
