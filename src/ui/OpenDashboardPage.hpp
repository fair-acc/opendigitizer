#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <chrono>
#include <string>
#include <vector>

#include "Dashboard.hpp"

namespace opencmw::client {
class RestClient;
}

namespace DigitizerUi {

class OpenDashboardPage {
public:
    OpenDashboardPage();
    ~OpenDashboardPage();

    std::function<void()>                                             requestCloseDashboard;
    std::function<void(const std::shared_ptr<DashboardDescription>&)> requestLoadDashboard;

    void draw(const std::shared_ptr<Dashboard>& optionalDashboard);

    void addSource(std::string_view path);
    void addDashboard(const std::shared_ptr<DashboardSource>& source, const auto& n);

    std::shared_ptr<DashboardDescription> get(const size_t index);

private:
    void dashboardControls(const std::shared_ptr<Dashboard>& optionalDashboard);
    void drawAddSourcePopup();
    void unsubscribeSource(const std::shared_ptr<DashboardSource>& source);

    std::vector<std::shared_ptr<DashboardDescription>> m_dashboards;
    std::vector<std::shared_ptr<DashboardSource>>      m_sources;
    bool                                               m_favoritesEnabled    = true;
    bool                                               m_notFavoritesEnabled = true;
    std::chrono::time_point<std::chrono::system_clock> m_date;
    int                                                m_filterDate;
    bool                                               m_filterDateEnabled = false;
    DashboardSource*                                   m_sourceHovered     = nullptr;
    std::unique_ptr<opencmw::client::RestClient>       m_restClient;
};

} // namespace DigitizerUi

#endif
