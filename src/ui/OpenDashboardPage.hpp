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

    std::function<void()>                                                   requestCloseDashboard;
    std::function<void(const std::shared_ptr<const DashboardDescription>&)> requestLoadDashboard;

    void draw(Dashboard* optionalDashboard);

    void addDashboard(std::string_view path);
    void addDashboard(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const auto& n);

    std::shared_ptr<const DashboardDescription> get(const size_t index);

private:
    void dashboardControls(Dashboard* optionalDashboard);
    void drawAddSourcePopup();
    void unsubscribeSource(const std::shared_ptr<DashboardStorageInfo>& source);

    std::vector<std::shared_ptr<const DashboardDescription>> m_dashboards;
    std::vector<std::shared_ptr<DashboardStorageInfo>>       m_storageInfos;
    bool                                                     m_favoritesEnabled    = true;
    bool                                                     m_notFavoritesEnabled = true;
    std::chrono::time_point<std::chrono::system_clock>       m_date;
    int                                                      m_filterDate;
    bool                                                     m_filterDateEnabled  = false;
    DashboardStorageInfo*                                    m_storageInfoHovered = nullptr;
    std::unique_ptr<opencmw::client::RestClient>             m_restClient;
};

} // namespace DigitizerUi

#endif
