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
}

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

    struct Plot {
        std::string                                  name;
        std::string                                  chartTypeName = "XYChart"; // String-based chart type
        std::vector<std::string>                     sourceNames;

        // Chart storage and interface
        std::shared_ptr<void>                      chartStorage;    // Owns the chart (type-erased)
        opendigitizer::charts::ChartInterface* chart = nullptr; // Polymorphic interface

        std::vector<AxisConfig> axes;

        // Helper to check if chart is valid
        [[nodiscard]] bool hasChart() const noexcept { return chart != nullptr; }

        // Helper to clear signal sinks from chart
        void clearChartSinks() {
            if (chart) {
                chart->clearSignalSinks();
            }
        }

        // Helper to build axis categories
        void buildChartAxisCategories() {
            if (chart) {
                chart->buildAxisCategories();
            }
        }

        // Helper to set dashboard axis config
        void setChartDashboardAxisConfig(std::vector<opendigitizer::charts::DashboardAxisConfig> configs) {
            if (chart) {
                chart->setDashboardAxisConfig(std::move(configs));
            }
        }

        // Helper to setup all axes
        void setupChartAllAxes() {
            if (chart) {
                chart->setupAllAxes();
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
    std::vector<Plot>                            m_plots;
    DockingLayoutType                            m_layout;
    std::unordered_map<std::string, std::string> m_flowgraphUriByRemoteSource;
    plf::colony<Service>                         m_services;
    std::atomic<bool>                            m_isInitialised = false;

    Scheduler m_scheduler;

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

public:
    explicit Dashboard(PrivateTag, std::shared_ptr<opencmw::client::RestClient> restClient, const std::shared_ptr<const DashboardDescription>& desc);

    static std::unique_ptr<Dashboard> create(std::shared_ptr<opencmw::client::RestClient> restClient, const std::shared_ptr<const DashboardDescription>& desc);

    ~Dashboard();

    void load();

    void loadAndThen(std::string_view grcData, std::function<void(gr::Graph&&)> assignScheduler);

    void save();

    DigitizerUi::Dashboard::Plot& newPlot(int x, int y, int w, int h, std::string_view chartType = "XYChart");

    void deletePlot(Plot* plot);

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
};
} // namespace DigitizerUi

#endif
