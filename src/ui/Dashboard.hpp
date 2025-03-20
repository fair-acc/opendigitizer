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
#include <gnuradio-4.0/PluginLoader.hpp>

#include "components/Docking.hpp"

namespace gr {
class Graph;
}

namespace opendigitizer {
class ImPlotSinkModel;
}

namespace DigitizerUi {
class FlowGraphItem;
class UiGraphModel;

struct DashboardDescription;

enum class AxisScale : std::uint8_t {
    Linear = 0U,   /// default linear scale [t0, .., tn]
    LinearReverse, /// reverse linear scale [t0-tn, ..., 0]
    Time,          /// date/timescale
    Log10,         /// base 10 logarithmic scale
    SymLog,        /// symmetric log scale
};

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
    static constexpr const char* fileExtension = ".ddd"; // ddd for "Digitizer Dashboard Description"

    std::string                           name;
    std::shared_ptr<DashboardStorageInfo> storageInfo;
    std::string                           filename;
    bool                                  isFavorite;

    std::optional<std::chrono::time_point<std::chrono::system_clock>> lastUsed;

    void save();

    static void loadAndThen(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename, const std::function<void(std::shared_ptr<DashboardDescription>&&)>& cb);

    static std::shared_ptr<DashboardDescription> createEmpty(const std::string& name);
};

class Dashboard {
public:
    std::shared_ptr<gr::PluginLoader> pluginLoader = [] {
        std::vector<std::filesystem::path> pluginPaths;
#ifndef __EMSCRIPTEN__
        // TODO set correct paths
        pluginPaths.push_back(std::filesystem::current_path() / "plugins");
#endif
        return std::make_shared<gr::PluginLoader>(gr::globalBlockRegistry(), pluginPaths);
    }();

    struct Plot {
        enum class AxisKind { X = 0, Y };

        struct AxisData {
            AxisKind  axis  = AxisKind::X;
            float     min   = std::numeric_limits<float>::quiet_NaN();
            float     max   = std::numeric_limits<float>::quiet_NaN();
            AxisScale scale = AxisScale::Linear;
            float     width = std::numeric_limits<float>::max();
        };

        std::string                                  name;
        std::vector<std::string>                     sourceNames;
        std::vector<opendigitizer::ImPlotSinkModel*> sources; // Not owned by us
        std::vector<AxisData>                        axes;

        std::shared_ptr<DockSpace::Window> window;

        Plot() {
            static int n = 1;
            name         = fmt::format("Plot {}", n++);
            window       = std::make_shared<DockSpace::Window>(name);
        }
    };

private:
    class PrivateTag {};

public:
    explicit Dashboard(PrivateTag, FlowGraphItem* fgItem, const std::shared_ptr<DashboardDescription>& desc);

    static std::unique_ptr<Dashboard> create(FlowGraphItem*, const std::shared_ptr<DashboardDescription>& desc);

    ~Dashboard();

    void load();

    void load(const std::string& grcData, const std::string& dashboardData, std::function<void(gr::Graph&&)> assignScheduler = {});

    void save();

    void newPlot(int x, int y, int w, int h);

    void deletePlot(Plot* plot);

    void removeSinkFromPlots(std::string_view sinkName);

    inline auto& plots() { return m_plots; }

    DockingLayoutType layout() { return m_layout; }

    void setNewDescription(const std::shared_ptr<DashboardDescription>& desc);

    inline std::shared_ptr<DashboardDescription> description() const { return m_desc; }

    struct Service {
        Service(std::string n, std::string u) : name(std::move(n)), uri(std::move(u)) {}

        std::string name;
        std::string uri;
        std::string layout;
        std::string grc;

        opencmw::client::RestClient client;

        void reload();

        void execute();

        void emplaceBlock(std::string type, std::string params);
    };

    void registerRemoteService(std::string_view blockName, std::optional<opencmw::URI<>> uri);

    void unregisterRemoteService(std::string_view blockName);

    void removeUnusedRemoteServices();

    void saveRemoteServiceFlowgraph(Service* s);

    inline auto& remoteServices() { return m_services; }

    void loadPlotSources();

    UiGraphModel& graphModel();

    std::atomic<bool> inUse = false;

private:
    void doLoad(const std::string& desc);

    std::shared_ptr<DashboardDescription>        m_desc;
    std::vector<Plot>                            m_plots;
    DockingLayoutType                            m_layout;
    std::unordered_map<std::string, std::string> m_flowgraphUriByRemoteSource;
    plf::colony<Service>                         m_services;
    FlowGraphItem*                               m_fgItem = nullptr;
};
} // namespace DigitizerUi

#endif
