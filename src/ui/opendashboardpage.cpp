#include "opendashboardpage.h"
#include "app.h"
#include "imgui.h"
#include "imguiutils.h"

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>

namespace DigitizerUi {

namespace {

enum FilterDate {
    Before,
    After
};

constexpr const char *addSourcePopupId = "addSourcePopup";
} // namespace

OpenDashboardPage::OpenDashboardPage()
    : m_date(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()))
    , m_filterDate(FilterDate::Before)
    , m_restClient(std::make_unique<opencmw::client::RestClient>()) {
#ifndef __EMSCRIPTEN__
    addSource(".");
#endif
}

OpenDashboardPage::~OpenDashboardPage() = default;

void OpenDashboardPage::addDashboard(const std::shared_ptr<DashboardSource> &source, const auto &n) {
    DashboardDescription::load(source, n, [&](std::shared_ptr<DashboardDescription> &&dd) {
        if (dd) {
            auto it = std::find_if(m_dashboards.begin(), m_dashboards.end(), [&](const auto &d) {
                return d->source.get() == source.get() && d->name == dd->name;
            });
            if (it == m_dashboards.end()) {
                m_dashboards.push_back(dd);
            }
        }
    });
}

void OpenDashboardPage::addSource(std::string_view path) {
    m_sources.push_back(DashboardSource::get(path));
    auto &source = m_sources.back();

    if (path.starts_with("https://") | path.starts_with("http://")) {
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Subscribe;
        command.topic    = opencmw::URI<opencmw::STRICT>::UriFactory().path(path).build();

        command.callback = [this, source](const opencmw::mdp::Message &rep) {
            if (rep.data.size() == 0) {
                return;
            }

            auto                     buf = rep.data;
            std::vector<std::string> names;
            opencmw::IoSerialiser<opencmw::Json, decltype(names)>::deserialise(buf, opencmw::FieldDescriptionShort{}, names);

            App::instance().executeLater([this, source, names = std::move(names)]() {
                for (const auto &n : names) {
                    addDashboard(source, n);
                }
            });
        };
        // subscribe to get notified when the dashboards list is modified
        m_restClient->request(command);

        // also request the list to be sent immediately
        command.command = opencmw::mdp::Command::Get;
        m_restClient->request(command);
    } else if (path.starts_with("example://")) {
        auto fs  = cmrc::sample_dashboards::get_filesystem();
        auto dir = fs.iterate_directory("assets/sampleDashboards/");
        for (auto d : dir) {
            if (d.is_file() && d.filename().ends_with(".yml")) {
                addDashboard(source, d.filename().substr(0, d.filename().size() - 4));
            }
        }
    } else {
#ifndef EMSCRIPTEN
        namespace fs = std::filesystem;
        if (!fs::is_directory(path)) {
            return;
        }

        for (auto &file : fs::directory_iterator(path)) {
            if (file.is_regular_file() && file.path().extension() == DashboardDescription::fileExtension) {
                addDashboard(source, file.path().filename().native());
            }
        }
#endif
    }
}

void OpenDashboardPage::unsubscribeSource(const std::shared_ptr<DashboardSource> &source) {
    if (source->path.starts_with("https://") || source->path.starts_with("http://")) {
        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Unsubscribe;
        command.topic   = opencmw::URI<opencmw::STRICT>::UriFactory().path(source->path).build();
        m_restClient->request(command);
    }
}

void OpenDashboardPage::draw(App *app) {
    ImGui::Spacing();
    ImGui::PushFont(app->fontBigger[app->prototypeMode]);
    if (app->dashboard) {
        auto desc = app->dashboard->description();
        ImGui::Text("%s (%s)", desc->name.c_str(), desc->source->path.c_str());
    } else {
        ImGui::Text("-");
    }

    static const float indent = 20;

    ImGui::PopFont();
    ImGui::Dummy({ indent, 20 });
    ImGui::SameLine();
    const bool dashboardLoaded = app->dashboard != nullptr;
    if (!dashboardLoaded) {
        ImGui::BeginDisabled();
    }

    // Only enable the save button if the dashboard has a valid source, that is it has been saved somewhere before
    if (dashboardLoaded && !app->dashboard->description()->source->isValid) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save")) {
        app->dashboard->save();
    }
    if (dashboardLoaded && !app->dashboard->description()->source->isValid) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
        ImGui::OpenPopup("saveAsDialog");
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        app->closeDashboard();
    }
    if (!dashboardLoaded) {
        ImGui::EndDisabled();
    }

    ImGui::SetNextWindowSize({ 600, 300 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("saveAsDialog")) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name:");
        ImGui::SameLine();
        auto                                    desc = app->dashboard->description();
        static std::string                      name;
        static std::shared_ptr<DashboardSource> source;
        if (ImGui::IsWindowAppearing()) {
            name   = desc->name;
            source = desc->source->isValid || m_sources.empty() ? desc->source : m_sources.front();
        }
        ImGui::InputText("##name", &name);

        ImGui::TextUnformatted("Source:");
        ImGui::SameLine();

        ImGui::BeginGroup();
        for (const auto &s : m_sources) {
            bool enabled = s == source;
            if (ImGui::Checkbox(s->path.c_str(), &enabled)) {
                source = s;
            }
        }
        if (ImGui::Button("Add new")) {
            ImGui::OpenPopup(addSourcePopupId);
        }
        ImGui::EndGroup();

        drawAddSourcePopup();

        bool okEnabled = !name.empty() && source->isValid;
        if (ImGuiUtils::drawDialogButtons(okEnabled) == ImGuiUtils::DialogButton::Ok) {
            auto newDesc    = std::make_shared<DashboardDescription>(*desc);
            newDesc->name   = name;
            newDesc->source = source;
            m_dashboards.push_back(newDesc);

            app->dashboard->setNewDescription(newDesc);
            app->dashboard->save();
        }

        ImGui::EndPopup();
    }

    ImGui::Dummy({ 0, 30 });
    ImGui::PushFont(app->fontBigger[app->prototypeMode]);
    ImGui::TextUnformatted("New Digitizer Window");
    ImGui::PopFont();
    ImGui::Dummy({ indent, 00 });
    ImGui::SameLine();
    if (ImGui::Button("Open a new Digitizer Window")) {
        app->openNewWindow();
    }

    ImGui::Dummy({ 0, 30 });
    ImGui::PushFont(app->fontBigger[app->prototypeMode]);
    ImGui::TextUnformatted("Load a new Dashboard");
    ImGui::PopFont();
    ImGui::Spacing();

    ImGui::Dummy({ indent, 0 });
    ImGui::SameLine();
    if (ImGui::Button("Open empty dashboard")) {
        app->loadEmptyDashboard();
    }
    ImGui::Spacing();

    auto getDashboard = [this](auto &it) -> std::pair<std::shared_ptr<DashboardDescription>, std::string> {
        if (!it->source->enabled) {
            return { it, {} };
        }
        if ((!m_favoritesEnabled && it->isFavorite) || (!m_notFavoritesEnabled && !it->isFavorite)) {
            return { it, {} };
        }
        if (m_filterDateEnabled) {
            switch (m_filterDate) {
            case FilterDate::Before:
                if (it->lastUsed >= m_date) {
                    return { it, {} };
                }
                break;
            case FilterDate::After:
                if (it->lastUsed <= m_date) {
                    return { it, {} };
                }
            }
        }
        return { it, it->name };
    };
    int  dashboardCount = 0;
    auto drawDashboard  = [&](auto &&item, bool selected) {
        ImGui::PushID(item.first->source->path.c_str());
        ImGui::PushID(item.second.data());

        auto  pos  = ImGui::GetCursorPos();
        auto  size = ImGui::GetContentRegionAvail();
        float h    = ImGui::GetTextLineHeightWithSpacing() * 2;
        ImGui::PushFont(app->fontBig[app->prototypeMode]);
        h += ImGui::GetTextLineHeightWithSpacing();

        auto  pp       = ImGui::GetCursorScreenPos();
        auto &style    = ImGui::GetStyle();
        auto  colorVec = style.Colors[dashboardCount++ % 2 == 0 ? ImGuiCol_TableRowBg : ImGuiCol_TableRowBgAlt];
        auto  color    = ImGui::ColorConvertFloat4ToU32(colorVec);
        ImGui::GetWindowDrawList()->AddRectFilled(pp, pp + ImVec2(size.x, h), color);

        // selected = ImGui::Selectable("", selected, 0, { size.x, h });
        // auto p2 = ImGui::GetCursorPos();
        ImGui::SetCursorPos(pos);
        ImGui::TextUnformatted(item.second.data());
        ImGui::PopFont();
        ImGui::TextUnformatted(item.first->source->path.c_str());
        if (item.first->lastUsed) {
            std::chrono::year_month_day ymd  = std::chrono::floor<std::chrono::days>(item.first->lastUsed.value());
            auto                        date = fmt::format("Last used: {:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
            ImGui::TextUnformatted(date.c_str());
        } else {
            ImGui::TextUnformatted("Last used: never");
        }
        auto p2 = ImGui::GetCursorPos();

        ImGui::SetCursorPosX(pos.x + size.x - 20);
        ImGui::SetCursorPosY(pos.y + 5);
        ImGui::BeginGroup();

        ImGui::PushFont(item.first->isFavorite ? app->fontIconsSolid : app->fontIcons);
        // ImGui::SetNextItemWidth(-5)
        if (ImGui::Button("\uf005")) { // , star icon
            item.first->isFavorite = !item.first->isFavorite;
        }
        ImGui::PopFont();

        const auto &name              = item.first->name;
        const auto &source            = item.first->source;
        bool        isDashboardActive = app->dashboard && name == app->dashboard->description()->name && source == app->dashboard->description()->source;
        ImGui::PushFont(isDashboardActive ? app->fontIconsSolid : app->fontIcons);
        if (ImGui::Button("\uf144")) { // , play icon
            app->loadDashboard(item.first);
            app->mainViewMode = "View";
        }
        ImGui::PopFont();
        ImGui::EndGroup();

        ImGui::SetCursorPos(p2);
        ImGui::PopID();
        ImGui::PopID();
        return false;
    };

    ImGuiUtils::filteredListBox("dashboards", m_dashboards, getDashboard, drawDashboard, { 300, 300 });
    ImGui::SameLine();

    ImGui::Dummy({ 20, 0 });
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Source:");
    ImGui::SameLine();

    ImGui::BeginGroup();
    DashboardSource *newHovered = nullptr;
    float            x          = ImGui::GetCursorPosX() + 100;
    for (auto it = m_sources.begin(); it != m_sources.end();) {
        auto &s = *it;
        // push a ID beacuse the button has a constant label
        ImGui::PushID(s->path.c_str());
        ImGui::BeginGroup();
        ImGui::Checkbox(s->path.c_str(), &s->enabled);
        ImGui::SameLine();
        x = std::max(x, ImGui::GetCursorPosX() + 40);
        ImGui::PushFont(app->fontIcons);
        if (m_sourceHovered == s.get() && ImGui::Button("\uf2ed")) { // trash can
            m_dashboards.erase(std::remove_if(m_dashboards.begin(), m_dashboards.end(), [&](auto &d) {
                return d->source == s;
            }),
                    m_dashboards.end());
            unsubscribeSource(*it);
            it = m_sources.erase(it);
        } else {
            ++it;
        }
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            newHovered = s.get();
        }
        ImGui::PopFont();
        ImGui::PopID();
    }
    m_sourceHovered = newHovered;
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Add new")) {
        ImGui::OpenPopup(addSourcePopupId);
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Favorite:");
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::Checkbox("Favorite", &m_favoritesEnabled);
    ImGui::Checkbox("Not Favorite", &m_notFavoritesEnabled);
    ImGui::EndGroup();
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Checkbox("Last used:", &m_filterDateEnabled);
    ImGui::SameLine();
    static const char *filterLabels[] = {
        "Before",
        "After"
    };
    if (ImGui::BeginCombo("##menu", filterLabels[m_filterDate])) {
        for (int i = 0; i < 2; ++i) {
            if (ImGui::Selectable(filterLabels[i])) {
                m_filterDate        = i;
                m_filterDateEnabled = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(m_date));
    char                        date[11] = {};
    fmt::format_to(date, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
    if (ImGui::InputTextWithHint("##date", "today", date, 11, ImGuiInputTextFlags_CallbackCharFilter,
                [](ImGuiInputTextCallbackData *d) -> int {
                    fmt::print("{}\n", d->BufTextLen);
                    if (d->EventChar == '/' || (d->EventChar >= '0' && d->EventChar <= '9')) {
                        return 0;
                    }
                    return 1;
                })
            && strnlen(date, sizeof(date)) == 10) {
        unsigned                    day   = std::atoi(date);
        unsigned                    month = std::atoi(date + 3);
        int                         year  = std::atoi(date + 6);
        std::chrono::year_month_day date{ std::chrono::year{ year }, std::chrono::month{ month }, std::chrono::day{ day } };
        fmt::print("{} {} {}\n", day, month, year);
        if (date.ok()) {
            m_date = std::chrono::sys_days(date);
        }
    }
    ImGui::SameLine();
    ImGui::PushFont(app->fontIcons);
    if (ImGui::Button("")) {
        ImGui::OpenPopup("calendar popup");
    }
    ImGui::PopFont();
    if (ImGui::BeginPopup("calendar popup")) {
        ImGui::TextUnformatted("TODO: Calendar widget");
        ImGui::EndPopup();
    }

    drawAddSourcePopup();

    ImGui::EndGroup();
}

void OpenDashboardPage::drawAddSourcePopup() {
    ImGui::SetNextWindowSize({ 600, 80 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal(addSourcePopupId)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Path:");
        ImGui::SameLine();
        static std::string path;
        if (ImGui::IsWindowAppearing()) {
            path = {};
        }
        ImGui::InputText("##sourcePath", &path);

#ifdef EMSCRIPTEN
        // on emscripten we cannot use local sources
        const bool okEnabled = path.starts_with("https://") || path.starts_with("http://");
#else
        const bool okEnabled = !path.empty();
#endif
        if (ImGuiUtils::drawDialogButtons(okEnabled) == ImGuiUtils::DialogButton::Ok) {
            addSource(path);
        }
        ImGui::EndPopup();
    }
}

std::shared_ptr<DashboardDescription> OpenDashboardPage::get(const size_t index) {
    if (m_dashboards.size() > index) {
        return { m_dashboards.at(index) };
    }
    return {};
}

} // namespace DigitizerUi
