#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <chrono>
#include <string>
#include <vector>

#include "dashboard.h"

namespace opencmw::client {
class RestClient;
}

namespace DigitizerUi {

class App;

class OpenDashboardPage {
public:
    OpenDashboardPage();
    ~OpenDashboardPage();

    void draw(App *app);

    void addSource(std::string_view path);
    void addDashboard(const std::shared_ptr<DashboardSource> &source, const auto &n);

private:
    void                                               drawAddSourcePopup();
    void                                               unsubscribeSource(const std::shared_ptr<DashboardSource> &source);

    std::vector<std::shared_ptr<DashboardDescription>> m_dashboards;
    std::vector<std::shared_ptr<DashboardSource>>      m_sources;
    bool                                               m_favoritesEnabled    = true;
    bool                                               m_notFavoritesEnabled = true;
    std::chrono::time_point<std::chrono::system_clock> m_date;
    int                                                m_filterDate;
    bool                                               m_filterDateEnabled = false;
    DashboardSource                                   *m_sourceHovered     = nullptr;
    std::unique_ptr<opencmw::client::RestClient>       m_restClient;
};

} // namespace DigitizerUi

#endif
