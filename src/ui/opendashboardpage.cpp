#include "opendashboardpage.h"
#include "app.h"
#include "imguiutils.h"

namespace DigitizerUi {

namespace {

enum FilterDate {
    Before,
    After
};

}

OpenDashboardPage::OpenDashboardPage()
    : m_date(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()))
    , m_filterDate(FilterDate::Before) {
    addSource(".");
}

void OpenDashboardPage::addSource(std::string_view path) {
    auto &source = *m_sources.insert(DashboardSource{ std::string(path), true });

    namespace fs = std::filesystem;
    if (!fs::is_directory(path)) {
        return;
    }

    for (auto &file : fs::directory_iterator(path)) {
        if (file.is_regular_file() && file.path().extension() == ".ddd") { // ddd for "Digitizer Dashboard Description"
            auto dd = source.load(file.path().filename().native());
            if (dd) {
                m_dashboards.push_back(dd);
            }
        }
    }
}

void OpenDashboardPage::draw(App *app) {
    ImGui::Spacing();
    ImGui::PushFont(app->font16);
    if (app->dashboard) {
        ImGui::Text("%s (%s)", app->dashboard->name().c_str(), app->dashboard->path().c_str());
    } else {
        ImGui::Text("-");
    }
    ImGui::PopFont();
    ImGui::Dummy({ 20, 20 });
    ImGui::SameLine();
    const bool dashboardLoaded = app->dashboard != nullptr;
    if (!dashboardLoaded) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Save")) {
        app->dashboard->save();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        app->closeDashboard();
    }
    if (!dashboardLoaded) {
        ImGui::EndDisabled();
    }

    ImGui::Dummy({ 0, 30 });
    ImGui::PushFont(app->font16);
    ImGui::TextUnformatted("New Digitizer Window");
    ImGui::PopFont();
    ImGui::Dummy({ 20, 00 });
    ImGui::SameLine();
    if (ImGui::Button("Open a new Digitizer Window")) {
        app->openNewWindow();
    }

    ImGui::Dummy({ 0, 30 });
    ImGui::PushFont(app->font16);
    ImGui::TextUnformatted("Load a new Dashboard");
    ImGui::PopFont();
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
        ImGui::PushID(item.second.data());
        auto  pos  = ImGui::GetCursorPos();
        auto  size = ImGui::GetContentRegionAvail();
        float h    = ImGui::GetTextLineHeightWithSpacing() * 2;
        ImGui::PushFont(app->font14);
        h += ImGui::GetTextLineHeightWithSpacing();

        auto     pp       = ImGui::GetCursorScreenPos();
        auto    &style    = ImGui::GetStyle();
        auto     colorVec = style.Colors[dashboardCount++ % 2 == 0 ? ImGuiCol_TableRowBg : ImGuiCol_TableRowBgAlt];
        uint32_t color    = 0xff | uint32_t(colorVec.x * 0xff) << 24 | uint32_t(colorVec.y * 0xff) << 16 | uint32_t(colorVec.z * 0xff) << 8;
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
        const auto &path              = item.first->source->path;
        bool        isDashboardActive = app->dashboard && name == app->dashboard->name() && path == app->dashboard->path();
        ImGui::PushFont(isDashboardActive ? app->fontIconsSolid : app->fontIcons);
        if (ImGui::Button("\uf144")) { // , play icon
            app->loadDashboard(item.first);
        }
        ImGui::PopFont();
        ImGui::EndGroup();

        ImGui::SetCursorPos(p2);
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
        ImGui::PushID(s.path.c_str());
        ImGui::BeginGroup();
        ImGui::Checkbox(s.path.c_str(), &s.enabled);
        ImGui::SameLine();
        x = std::max(x, ImGui::GetCursorPosX() + 40);
        ImGui::PushFont(app->fontIcons);
        if (m_sourceHovered == &s && ImGui::Button("\uf2ed")) { // trash can
            m_dashboards.erase(std::remove_if(m_dashboards.begin(), m_dashboards.end(), [&](auto &d) {
                return d->source == &s;
            }),
                    m_dashboards.end());
            it = m_sources.erase(it);
        } else {
            ++it;
        }
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            newHovered = &s;
        }
        ImGui::PopFont();
        ImGui::PopID();
    }
    m_sourceHovered = newHovered;
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SetCursorPosX(x);
    if (ImGui::Button("Add new")) {
        ImGui::OpenPopup("addSourcePopup");
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
    char                        date[11];
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

    ImGui::EndGroup();

    ImGui::SetNextWindowSize({ 600, 80 }, ImGuiCond_Once);
    if (ImGui::BeginPopupModal("addSourcePopup")) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Path:");
        ImGui::SameLine();
        std::string path;
        ImGui::InputText("##sourcePath", &path);

        if (ImGuiUtils::drawDialogButton() == ImGuiUtils::DialogButton::Ok) {
            addSource(path);
        }
        ImGui::EndPopup();
    }
}

} // namespace DigitizerUi
