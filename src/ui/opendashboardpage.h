#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <chrono>
#include <string>
#include <vector>

#ifdef EMSCRIPTEN
#include "emscripten_compat.h"
#endif
#include <plf_colony.h>

#include "dashboard.h"

namespace DigitizerUi {

class App;

class OpenDashboardPage {
public:
    OpenDashboardPage();

    void draw(App *app);

    void addSource(std::string_view path);

private:
    std::vector<std::shared_ptr<DashboardDescription>> m_dashboards;
    plf::colony<DashboardSource>                       m_sources;
    bool                                               m_favoritesEnabled    = true;
    bool                                               m_notFavoritesEnabled = true;
    std::chrono::time_point<std::chrono::system_clock> m_date;
    int                                                m_filterDate;
    bool                                               m_filterDateEnabled = false;
    DashboardSource                                   *m_sourceHovered     = nullptr;
};

} // namespace DigitizerUi

#endif
