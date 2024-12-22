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

#include "Flowgraph.hpp"
#include "components/Docking.hpp"

namespace DigitizerUi {

class Block;
class FlowGraph;
class FlowGraphItem;
struct DashboardDescription;
struct DashboardSource {
    ~DashboardSource() noexcept;

    std::string path;
    bool        enabled;
    const bool  isValid = true;

    static std::shared_ptr<DashboardSource> get(std::string_view path);
};

struct DashboardDescription {
    static constexpr const char* fileExtension = ".ddd"; // ddd for "Digitizer Dashboard Description"

    std::string                                                       name;
    std::shared_ptr<DashboardSource>                                  source;
    std::string                                                       filename;
    bool                                                              isFavorite;
    std::optional<std::chrono::time_point<std::chrono::system_clock>> lastUsed;

    void save();

    static void                                  load(const std::shared_ptr<DashboardSource>& source, const std::string& filename, const std::function<void(std::shared_ptr<DashboardDescription>&&)>& cb);
    static std::shared_ptr<DashboardDescription> createEmpty(const std::string& name);
};

class Dashboard : public std::enable_shared_from_this<Dashboard> {
public:
    std::shared_ptr<Dashboard> shared() { return shared_from_this(); }

    struct Source {
        std::string blockName;
        std::string name;
        uint32_t    color;
        bool        visible{true};

        inline bool operator==(const Source& s) const { return s.blockName == blockName; };
    };
    struct Plot {
        enum class Axis { X, Y };

        Plot();

        std::string              name;
        std::vector<std::string> sourceNames;
        std::vector<Source*>     sources;
        struct AxisData {
            Axis  axis  = Axis::X;
            float min   = std::numeric_limits<float>::quiet_NaN();
            float max   = std::numeric_limits<float>::quiet_NaN();
            float width = std::numeric_limits<float>::max();
        };
        std::vector<AxisData> axes;

        std::shared_ptr<DockSpace::Window> window;
    };

private:
    class PrivateTag {};

public:
    explicit Dashboard(PrivateTag, FlowGraphItem* fgItem, const std::shared_ptr<DashboardDescription>& desc);

    static std::shared_ptr<Dashboard> create(FlowGraphItem*, const std::shared_ptr<DashboardDescription>& desc);

    ~Dashboard();

    void setPluginLoader(std::shared_ptr<gr::PluginLoader> loader) { localFlowGraph.setPluginLoader(std::move(loader)); }

    void load();
    void load(const std::string& grcData, const std::string& dashboardData);
    void save();

    void newPlot(int x, int y, int w, int h);
    void deletePlot(Plot* plot);
    void removeSinkFromPlots(std::string_view sinkName);

    inline const auto& sources() const { return m_sources; }
    inline auto&       sources() { return m_sources; }

    inline auto& plots() { return m_plots; }

    void                                         setNewDescription(const std::shared_ptr<DashboardDescription>& desc);
    inline std::shared_ptr<DashboardDescription> description() const { return m_desc; }

    struct Service {
        Service(std::string n, std::string u) : name(std::move(n)), uri(std::move(u)) {}
        std::string                 name;
        std::string                 uri;
        std::string                 layout;
        std::string                 grc;
        FlowGraph                   flowGraph;
        opencmw::client::RestClient client;

        void reload();
        void execute();
        void emplaceBlock(std::string type, std::string params);
    };
    void registerRemoteService(std::string_view blockName, std::string_view uri);
    void unregisterRemoteService(std::string_view blockName);
    void removeUnusedRemoteServices();

    void saveRemoteServiceFlowgraph(Service* s);

    inline auto& remoteServices() { return m_services; }

    Block* createSink();
    void   loadPlotSources();

    FlowGraph localFlowGraph;

private:
    void doLoad(const std::string& desc);

    std::shared_ptr<DashboardDescription>        m_desc;
    std::vector<Plot>                            m_plots;
    plf::colony<Source>                          m_sources;
    std::unordered_map<std::string, std::string> m_flowgraphUriByRemoteSource;
    plf::colony<Service>                         m_services;
    FlowGraphItem*                               m_fgItem = nullptr;
};

} // namespace DigitizerUi

#endif
