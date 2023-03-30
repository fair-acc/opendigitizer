#include "app.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace DigitizerUi {

void App::openNewWindow() {
#ifdef EMSCRIPTEN
    std::string script = fmt::format("window.open('{}').focus()", executable);
    ;
    emscripten_run_script(script.c_str());
#else
    if (fork() == 0) {
        execl(executable.c_str(), executable.c_str(), nullptr);
    }
#endif
}

void App::loadEmptyDashboard() {
    loadDashboard(DashboardDescription::createEmpty("New dashboard"));
}

void App::loadDashboard(const std::shared_ptr<DashboardDescription> &desc) {
    dashboard = std::make_unique<Dashboard>(desc, &flowGraph);
}

void App::loadDashboard(std::string_view url) {
    namespace fs = std::filesystem;
    fs::path path(url);
    if (!fs::exists(path)) {
        return;
    }

    auto source = DashboardSource::get(path.parent_path().native());
    auto desc   = DashboardDescription::load(source, path.filename());
    loadDashboard(desc);
}

void App::closeDashboard() {
    dashboard = {};
}

} // namespace DigitizerUi
