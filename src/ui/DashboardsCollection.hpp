#ifndef OPENDIGITIZER_UI_DASHBOARDS_COLLECTION_HPP_
#define OPENDIGITIZER_UI_DASHBOARDS_COLLECTION_HPP_

#include <opencmw.hpp>

#include <ClientCommon.hpp>
#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>

#include "Dashboard.hpp"

#include "common/Events.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace DigitizerUi {

struct DashboardsCollection {
    std::function<void()> onChanged;

    std::vector<std::shared_ptr<const DashboardDescription>> _dashboards;
    std::vector<std::shared_ptr<DashboardStorageInfo>>       _sources;
    std::shared_ptr<opencmw::client::RestClient>             _restClient;

    explicit DashboardsCollection(std::shared_ptr<opencmw::client::RestClient> restClient) : _restClient{std::move(restClient)} {}

    void addSource(std::string_view path) {
        _sources.push_back(DashboardStorageInfo::get(path));
        auto& storageInfo = _sources.back();

        if (path.starts_with("https://") || path.starts_with("http://")) {
            opencmw::client::Command command;
            command.command = opencmw::mdp::Command::Subscribe;
            command.topic   = opencmw::URI<opencmw::STRICT>::UriFactory().path(path).build();

            command.callback = [this, storageInfo](const opencmw::mdp::Message& rep) {
                if (rep.data.size() == 0) {
                    return;
                }

                auto                     buf = rep.data;
                std::vector<std::string> names;
                opencmw::IoSerialiser<opencmw::Json, decltype(names)>::deserialise(buf, opencmw::FieldDescriptionShort{}, names);

                EventLoop::instance().executeLater([this, storageInfo, names = std::move(names)]() {
                    for (const auto& n : names) {
                        loadDashboard(storageInfo, n);
                    }
                });
            };
            // subscribe to get notified when the dashboards list is modified
            // _restClient->request(command); // try to only get dashboard list via get, not via subscribe

            // also request the list to be sent immediately
            command.command = opencmw::mdp::Command::Get;
            _restClient->request(command);
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
        } else if (path.starts_with("example://")) {
            auto fs  = cmrc::sample_dashboards::get_filesystem();
            auto dir = fs.iterate_directory("assets/sampleDashboards/");
            for (auto d : dir) {
                if (d.is_file() && d.filename().ends_with(".grc")) {
                    loadDashboard(storageInfo, d.filename().substr(0, d.filename().size() - 4));
                }
            }
#endif
        } else {
#ifndef EMSCRIPTEN
            namespace fs = std::filesystem;
            const fs::path rootPath{path};
            if (!fs::is_directory(rootPath)) {
                return;
            }

            for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_directory() && it->path().filename().native().starts_with(".")) { // skip hidden folders
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && it->path().extension() == DashboardDescription::fileExtension) {
                    loadDashboard(storageInfo, it->path().lexically_relative(rootPath).native());
                }
            }
#endif
        }
    }

    void removeSource(const std::shared_ptr<DashboardStorageInfo>& source) {
        std::erase_if(_dashboards, [&source](const auto& dashboard) { return dashboard->storageInfo == source; });
        if (source->path.starts_with("https://") || source->path.starts_with("http://")) {
            opencmw::client::Command command;
            command.command = opencmw::mdp::Command::Unsubscribe;
            command.topic   = opencmw::URI<opencmw::STRICT>::UriFactory().path(source->path).build();
            _restClient->request(command);
        }
        std::erase(_sources, source);
        if (onChanged) {
            onChanged();
        }
    }

    void addDashboard(std::shared_ptr<const DashboardDescription> dashboard) {
        _dashboards.push_back(std::move(dashboard));
        if (onChanged) {
            onChanged();
        }
    }

    void loadDashboard(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename) {
        DashboardDescription::loadAndThen(_restClient, storageInfo, filename, [&](std::shared_ptr<const DashboardDescription>&& desc) {
            if (desc) {
                auto it = std::ranges::find_if(_dashboards, [&](const auto& d) { return d->storageInfo.get() == storageInfo.get() && d->name == desc->name; });
                if (it == _dashboards.end()) {
                    addDashboard(std::move(desc));
                }
            }
        });
    }

    [[nodiscard]] const std::vector<std::shared_ptr<const DashboardDescription>>& dashboards() const { return _dashboards; }
    [[nodiscard]] const std::vector<std::shared_ptr<DashboardStorageInfo>>&       sources() const { return _sources; }
};

} // namespace DigitizerUi

#endif
