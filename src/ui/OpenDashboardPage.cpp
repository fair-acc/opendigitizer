#include <opencmw.hpp>

#include <ClientCommon.hpp>
#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>

#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include <ImGuiDatePicker.hpp>

#include "common/Events.hpp"
#include "common/FramePacer.hpp"
#include "common/LookAndFeel.hpp"

#include "DashboardPage.hpp"
#include "OpenDashboardPage.hpp"

#include "OAuthSession.hpp"
#include "components/Dialog.hpp"
#include "components/FilterTagBar.hpp"
#include "components/NewBlockSelectorFuzzySearch.hpp"
#include "components/VirtualScrollTable.hpp"
#include "settings.hpp"

#include <gnuradio-4.0/meta/utils.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iterator>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

namespace DigitizerUi {

namespace {

constexpr const char*                  addSourcePopupId = "Include path in dashboard search##addSourcePopup";
constexpr const char*                  calendarPopupId  = "Select date##calendarPopup";
[[maybe_unused]] constexpr const char* oauthPopupId     = "OAuth/RBAC"; // unused in emscripten builds

constexpr const char* kIconSave     = "\u{f0c7}";
constexpr const char* kIconSaveAs   = "\u{f56e}";
constexpr const char* kIconClose    = "\u{f00d}";
constexpr const char* kIconSearch   = "\u{f002}";
constexpr const char* kIconStar     = "\u{f005}";
constexpr const char* kIconTrash    = "\u{f2ed}";
constexpr const char* kIconCalendar = "\u{f073}";
constexpr const char* kIconPlus     = "\u{f067}";
constexpr const char* kIconArrowUp  = "\u{f062}";

constexpr std::array  kFilterDateLabels     = {"Before", "After"};
constexpr const char* kFilterDateComboLabel = "##filterDateDirection";
constexpr const char* kLastUsedText         = "Last used";
constexpr const char* kExampleDate          = "00/00/0000";

void wrapIfTooLarge(float nextItemWidth, float availableWidth = ImGui::GetContentRegionAvail().x) {
    const float rightLimit = ImGui::GetCursorScreenPos().x + availableWidth;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextItemWidth <= rightLimit) {
        ImGui::SameLine();
    }
}

void drawVerticallyCenteredText(const char* label, float lineHeight) {
    const auto size = ImGui::CalcTextSize(label);
    assert(lineHeight >= size.y);
    auto oldPosition = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{oldPosition.x, oldPosition.y + (lineHeight - size.y) / 2.f});
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2{ImGui::GetCursorScreenPos().x, oldPosition.y});
}

bool doIconTextButton(const char* label, const char* icon, ImVec2 size) {
    const ImRect buttonBoundingBox{ImGui::GetCursorScreenPos(), ImGui::GetCursorScreenPos() + size};
    const auto   id = ImGui::GetID(label);

    ImGui::ItemSize(size, ImGui::GetStyle().FramePadding.y);
    if (!ImGui::ItemAdd(buttonBoundingBox, id)) {
        return false;
    }

    bool       hovered{};
    bool       held{};
    const bool pressed = ImGui::ButtonBehavior(buttonBoundingBox, id, &hovered, &held, ImGuiButtonFlags_None);

    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    // RenderFrame, RenderNavCursor, and RenderTextClipped are all very internal functions used to
    // implement ButtonEx(), we use them here just to enable us to draw multiple fonts in a button
    ImGui::RenderNavCursor(buttonBoundingBox, id);
    ImGui::RenderFrame(buttonBoundingBox.Min, buttonBoundingBox.Max, col, true, ImGui::GetStyle().FrameRounding);

    auto         framePadding = ImGui::GetStyle().FramePadding;
    ImVec2       textCursor   = buttonBoundingBox.Min + framePadding;
    ImVec2       textMax      = buttonBoundingBox.Max - framePadding;
    const ImVec2 labelSize    = ImGui::CalcTextSize(label, NULL, true);

    const ImVec2 align{0.5f, 0.5f}; // ImGui::GetStyle().ButtonTextAlign is typical but we wouldn't automatically respect that as expected
    {
        IMW::Font iconFont(LookAndFeel::instance().fontIconsSolidLarge);

        ImVec2      iconSize       = ImGui::CalcTextSize(icon);
        const float availableWidth = textMax.x - textCursor.x;
        const float gap            = ImGui::GetStyle().ItemInnerSpacing.x;
        const float totalWidth     = iconSize.x + gap + labelSize.x;
        const float padding        = (availableWidth - totalWidth) / 2.f;
        textCursor.x += padding;
        textMax = ImVec2{textCursor.x + totalWidth, textMax.y};
        ImGui::RenderTextClipped(textCursor, ImVec2{textCursor.x + iconSize.x, textMax.y}, icon, NULL, &iconSize, align, &buttonBoundingBox);
        textCursor.x += iconSize.x + gap;
    }
    ImGui::RenderTextClipped(textCursor, textMax, label, NULL, &labelSize, align, &buttonBoundingBox);

    return pressed;
}

float calcFilterDateComboWidth() { //
    return IMW::CalcComboSize(kFilterDateComboLabel, kFilterDateLabels[0], ImGuiComboFlags_WidthFitPreview).preferred.x;
}

// we use the date filter (the busiest + longest element) to determine minimum
// width of the sidebar. more bugprone than measuring the side of the sidebar
// every frame... but avoids the sidebar resizing on the first frame visible
float dateFilterRowWidth() {
    IMW::Font   font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    const float spacing        = ImGui::GetStyle().ItemSpacing.x;
    const float squareButton   = ImGui::GetFrameHeight();
    const float dateInputWidth = ImGui::CalcTextSize(kExampleDate).x + ImGui::GetFrameHeight();
    return squareButton + spacing + ImGui::CalcTextSize(kLastUsedText).x + spacing + calcFilterDateComboWidth() + spacing + dateInputWidth + spacing + squareButton;
}

[[nodiscard]] bool drawPlusButton(bool enabled) {
    const ImVec2 squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    IMW::Disabled disabled(!enabled);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
    const bool pressed = ImGui::Button(kIconPlus, squareButtonSize);
    ImGui::PopFont();
    return pressed;
}

void drawRowLabel(const char* label) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
}

[[nodiscard]] bool drawCalendarButton() {
    const ImVec2 squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f); // TODO: use FontWithSize when rebased on branch that added it
    const bool result = ImGui::Button(kIconCalendar, squareButtonSize);
    ImGui::PopFont();
    return result;
}

using DashboardList = std::vector<std::shared_ptr<const DashboardDescription>>;

struct FavoriteFilter {
    FavoritesFilter shown;
};
struct BeforeFilter {
    std::chrono::time_point<std::chrono::system_clock> date;
};
struct AfterFilter {
    std::chrono::time_point<std::chrono::system_clock> date;
};
struct TagFilter {
    std::string tag;
};
struct KeyValueFilter {
    std::string key;
    std::string value;
};
using DashboardFilterData = std::variant<FavoriteFilter, BeforeFilter, AfterFilter, TagFilter, KeyValueFilter>;

struct DashboardFilter final : components::SortFilterModelFilter {
    const DashboardList& dashboards;
    DashboardFilterData  filterData;

    DashboardFilter(const DashboardList& _viewedDashboards, DashboardFilterData _filterData) : dashboards(_viewedDashboards), filterData(std::move(_filterData)) {}

    bool matches(std::size_t itemIndex) const override {
        const DashboardDescription& dashboard = *dashboards[itemIndex];
        return std::visit(gr::meta::overloaded{
                              [&](const FavoriteFilter& filter) { return dashboard.isFavorite == (filter.shown == FavoritesFilter::ShowFavorited); },
                              [&](const BeforeFilter& filter) { return dashboard.lastUsed < filter.date; },
                              [&](const AfterFilter& filter) { return dashboard.lastUsed > filter.date; },
                              [&](const TagFilter& filter) { return std::ranges::contains(dashboard.tags, filter.tag); },
                              [&](const KeyValueFilter& filter) {
                                  const auto valueIt = dashboard.keyValueTags.find(filter.key);
                                  return valueIt != dashboard.keyValueTags.end() && valueIt->second == filter.value;
                              },
                          },
            filterData);
    }
};

template<typename Kind>
bool sortModelFilterIsType(const std::unique_ptr<components::SortFilterModelFilter>& filter) {
    return std::holds_alternative<Kind>(static_cast<const DashboardFilter&>(*filter).filterData);
}

enum class DashboardSortKind {
    lastUsed, // most recently used first, never used last
    name,     // lexicographically by dashboard name
    source,   // lexicographically by source URI
};

bool dashboardLess(const DashboardDescription& lhs, const DashboardDescription& rhs, DashboardSortKind kind) {
    switch (kind) {
    case DashboardSortKind::lastUsed: return lhs.lastUsed > rhs.lastUsed;
    case DashboardSortKind::name: return lhs.name < rhs.name;
    case DashboardSortKind::source: return lhs.storageInfo->path < rhs.storageInfo->path;
    }
    std::unreachable();
}

struct DashboardComparator final : components::SortFilterModelComparator {
    const DashboardList& dashboards;
    DashboardSortKind    kind;
    bool                 reversed;

    DashboardComparator(const DashboardList& viewedDashboards, DashboardSortKind sortKind, bool sortReversed = false) : dashboards(viewedDashboards), kind(sortKind), reversed(sortReversed) {}

    bool less(std::size_t lhsIndex, std::size_t rhsIndex) const override {
        if (reversed) {
            std::swap(lhsIndex, rhsIndex);
        }
        return dashboardLess(*dashboards[lhsIndex], *dashboards[rhsIndex], kind);
    }
};

struct DashboardTreeComparator final : components::SortFilterTreeModelComparator {
    const DashboardList& dashboards;
    DashboardSortKind    kind;
    bool                 reversed;

    DashboardTreeComparator(const DashboardList& viewedDashboards, DashboardSortKind sortKind, bool sortReversed = false) : dashboards(viewedDashboards), kind(sortKind), reversed(sortReversed) {}

    bool less(const components::SortFilterTreeModelNode& lhs, const components::SortFilterTreeModelNode& rhs) const override {
        // for now, branches just sort before leaves and otherwise this is the same as the regular filter
        if (lhs.isBranch() != rhs.isBranch()) {
            return lhs.isBranch();
        }
        if (lhs.isBranch()) {
            return lhs.name < rhs.name;
        }
        if (reversed) {
            return dashboardLess(*dashboards[*rhs.index], *dashboards[*lhs.index], kind);
        }
        return dashboardLess(*dashboards[*lhs.index], *dashboards[*rhs.index], kind);
    }
};

bool isDashboardSourceEnabled(const DashboardList& dashboards, std::size_t index) { return index < dashboards.size() && dashboards[index]->storageInfo->isEnabled; }

/// when we get std::polymorphic we can use that for filters and this can be removed
components::ModelFilters copyModelFilters(const components::ModelFilters& filters) {
    const auto copyUniquePtr = [](const auto& uniquePtr) -> std::unique_ptr<components::SortFilterModelFilter> { return std::make_unique<DashboardFilter>(static_cast<const DashboardFilter&>(*uniquePtr)); };
    auto       copiedFilters = filters.filterObjects | std::views::transform(copyUniquePtr) | std::ranges::to<std::vector>();
    return {
        .filterObjects  = std::move(copiedFilters),
        .requiredFilter = filters.requiredFilter,
        .requiresAll    = filters.requiresAll,
    };
}

std::string filterTagLabel(const components::SortFilterModelFilter& filter) {
    constexpr static auto formatDate = [](std::chrono::time_point<std::chrono::system_clock> date) {
        const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(date)};
        return std::format("{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
    };
    return std::visit(gr::meta::overloaded{
                          [](const FavoriteFilter& favoriteFilter) -> std::string { return favoriteFilter.shown == FavoritesFilter::ShowFavorited ? "favorited" : "not favorited"; },
                          [](const BeforeFilter& beforeFilter) { return std::format("used before {}", formatDate(beforeFilter.date)); },
                          [](const AfterFilter& afterFilter) { return std::format("used after {}", formatDate(afterFilter.date)); },
                          [](const TagFilter& tagFilter) { return std::format("tag: {}", tagFilter.tag); },
                          [](const KeyValueFilter& keyValueFilter) { return std::format("{} = {}", keyValueFilter.key, keyValueFilter.value); },
                      },
        static_cast<const DashboardFilter&>(filter).filterData);
}

} // namespace

components::SortFilterModelParams OpenDashboardPage::defaultFilterParams() {
    return {
        .numViewedItems = m_dashboards.size(),
        .filters        = {.filterObjects = {}, .requiredFilter = [this](std::size_t idx) { return isDashboardSourceEnabled(m_dashboards, idx); }},
        .sortStrategy   = std::make_unique<DashboardComparator>(m_dashboards, DashboardSortKind::lastUsed),
    };
}

components::SortFilterTreeModelParams OpenDashboardPage::makeSortFilterTreeModelParamsThatMatchListModel() {
    components::SortFilterTreeModelParams params{.numItems = m_dashboards.size(), .getItemPathFunction = {}, .filters = {}, .sortStrategy = {}};
    if (m_viewMode == ListViewMode::CustomTree) {
        params.getItemPathFunction = [this](std::size_t index) { return customDashboardTreePath(index); };
    } else {
        params.getItemPathFunction = [this](std::size_t index) { return dashboardTreePath(index); };
    }
    params.filters = copyModelFilters(m_sortFilterModel.filters());

    if (const auto* tableComparator = m_sortFilterModel.comparisonOperator()) {
        const auto* dashboardComparator = static_cast<const DashboardComparator*>(tableComparator);
        params.sortStrategy             = std::make_unique<DashboardTreeComparator>(m_dashboards, dashboardComparator->kind, dashboardComparator->reversed);
    } else if (m_sortFilterModel.scoreEvaluator() != nullptr) {
        // do the fuzzy search + scoring for every tree node, leaves and branches/folders
        params.sortStrategy = components::makeSimpleSortFilterTreeModelScoreEvaluator([this](const components::SortFilterTreeModelNode& node) { //
            return static_cast<float>(components::filterTypename(node.name, m_searchString).score);
        });
    }
    return params;
}

std::vector<std::string> OpenDashboardPage::dashboardTreePath(std::size_t index) const {
    const DashboardDescription& dashboard = *m_dashboards[index];
    std::vector<std::string>    path{dashboard.storageInfo->path};
    for (const auto& component : std::filesystem::path(dashboard.filename).parent_path()) {
        path.push_back(component.native());
    }
    path.push_back(dashboard.name);
    return path;
}

/// returns an empty path, which makes SortFilterTreeModel skip the item, when the dashboard lacks any of the requested keys
std::vector<std::string> OpenDashboardPage::customDashboardTreePath(std::size_t index) const {
    const DashboardDescription& dashboard = *m_dashboards[index];
    std::vector<std::string>    path;
    for (const std::string& key : m_customTreeKeys) {
        const auto valueIt = dashboard.keyValueTags.find(key);
        if (valueIt == dashboard.keyValueTags.end()) {
            return {};
        }
        path.push_back(valueIt->second);
    }
    path.push_back(dashboard.name);
    return path;
}

void OpenDashboardPage::forceRecalculateSortingAndFilteringOfTreeModel() { m_sortFilterTreeModel = components::SortFilterTreeModel(makeSortFilterTreeModelParamsThatMatchListModel()); }

OpenDashboardPage::OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient)           //
    : m_restClient{std::move(restClient)},                                                              //
      m_sortFilterModel(defaultFilterParams()),                                                         //
      m_sortFilterTreeModel(makeSortFilterTreeModelParamsThatMatchListModel()),                         //
      m_pendingDateFilterDate(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())), //
      m_pendingDateFilterDirection(DateFilterDirection::Before)                                         //
{
#ifndef __EMSCRIPTEN__
    addDashboard(".");
#endif
}

OpenDashboardPage::~OpenDashboardPage() = default;

void OpenDashboardPage::addDashboard(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const auto& n) {
    DashboardDescription::loadAndThen(m_restClient, storageInfo, n, [&](std::shared_ptr<const DashboardDescription>&& desc) {
        if (desc) {
            auto it = std::ranges::find_if(m_dashboards, [&](const auto& d) { return d->storageInfo.get() == storageInfo.get() && d->name == desc->name; });
            if (it == m_dashboards.end()) {
                pushBackDashboard(std::move(desc));
            }
        }
    });
}

void OpenDashboardPage::addDashboard(std::string_view path) {
    m_storageInfos.push_back(DashboardStorageInfo::get(path));
    auto& storageInfo = m_storageInfos.back();

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
                    addDashboard(storageInfo, n);
                }
            });
        };
        // subscribe to get notified when the dashboards list is modified
        // m_restClient->request(command); // try to only get dashboard list via get, not via subscribe

        // also request the list to be sent immediately
        command.command = opencmw::mdp::Command::Get;
        m_restClient->request(command);
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
    } else if (path.starts_with("example://")) {
        auto fs  = cmrc::sample_dashboards::get_filesystem();
        auto dir = fs.iterate_directory("assets/sampleDashboards/");
        for (auto d : dir) {
            if (d.is_file() && d.filename().ends_with(".grc")) {
                addDashboard(storageInfo, d.filename().substr(0, d.filename().size() - 4));
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
                addDashboard(storageInfo, it->path().lexically_relative(rootPath).native());
            }
        }
#endif
    }
}

void OpenDashboardPage::unsubscribeSource(const std::shared_ptr<DashboardStorageInfo>& storageInfo) {
    if (storageInfo->path.starts_with("https://") || storageInfo->path.starts_with("http://")) {
        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Unsubscribe;
        command.topic   = opencmw::URI<opencmw::STRICT>::UriFactory().path(storageInfo->path).build();
        m_restClient->request(command);
    }
}

void OpenDashboardPage::drawSaveAsDialog(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    ImGui::SetNextWindowSize({600, 300}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup("saveAsDialog", nullptr, 0)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name:");
        ImGui::SameLine();
        auto                                         desc = optionalDashboard != nullptr && optionalDashboard->isInitialised ? optionalDashboard->description : nullptr;
        static std::string                           name;
        static std::shared_ptr<DashboardStorageInfo> storageInfo;
        if (ImGui::IsWindowAppearing() && desc != nullptr) {
            name        = desc->name;
            storageInfo = !desc->storageInfo->isInMemoryDashboardStorage() || m_storageInfos.empty() ? desc->storageInfo : m_storageInfos.front();
        }
        ImGui::InputText("##name", &name);

        ImGui::TextUnformatted("Source:");
        ImGui::SameLine();

        {
            IMW::Group group;
            for (const auto& s : m_storageInfos) {
                bool enabled = s == storageInfo;
                if (ImGui::Checkbox(s->path.c_str(), &enabled)) {
                    storageInfo = s;
                }
            }
            if (ImGui::Button("Add new")) {
                ImGui::OpenPopup(addSourcePopupId);
            }
        }

        drawAddSourcePopup();

        bool okEnabled = !name.empty() && !storageInfo->isInMemoryDashboardStorage();
        if (components::DialogButtons(okEnabled) == components::DialogButton::Ok && desc != nullptr) {
            auto newDesc         = std::make_shared<DashboardDescription>(*desc);
            newDesc->name        = name;
            newDesc->storageInfo = storageInfo;
            pushBackDashboard(newDesc);

            if (optionalDashboard != nullptr && optionalDashboard->isInitialised) {
                optionalDashboard->setNewDescription(newDesc);

                if (optionalDashboardPage) {
                    std::tie(optionalDashboard->layoutType, optionalDashboard->windowLayout) = optionalDashboardPage->saveLayoutConfiguration();
                }

                optionalDashboard->save();
            }
        }
    }
}

void OpenDashboardPage::drawSourcesSection() {
    ImGui::SeparatorText("Available Sources");

    IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);

    const ImVec2 trashButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    DashboardStorageInfo*                 newHovered = nullptr;
    std::shared_ptr<DashboardStorageInfo> sourceToRemove;
    bool                                  isFirstItem = true;

    const auto drawSource = [this, trashButtonSize, &sourceToRemove](const std::shared_ptr<DashboardStorageInfo>& source, const std::string& displayName) {
        IMW::Group sourceGroup;
        if (ImGui::Checkbox(displayName.c_str(), &source->isEnabled)) {
            forceRecalculateSortingAndFilteringOfListModel(); // we have a filter that removes things that are not enabled
        }
        ImGui::SameLine();

        // only draw trash can if hovered
        if (m_storageInfoHovered == source.get()) {
            ImGui::PushFont(LookAndFeel::instance().fontIcons, trashButtonSize.y / 2.f);
            if (ImGui::Button(kIconTrash, trashButtonSize)) {
                sourceToRemove = source;
            }
            ImGui::PopFont();
        } else {
            ImGui::Dummy(trashButtonSize);
        }
    };

    for (auto& source : m_storageInfos) {
        const static std::string cwdPath     = "Current application directory (.)";
        const static std::string parentPath  = "Current application parent directory (..)";
        const std::string&       displayPath = [&source] {
            if (source->path == ".") {
                return cwdPath;
            } else if (source->path == "..") {
                return parentPath;
            }
            return source->path;
        }();

        IMW::ChangeStrId id(displayPath.c_str());

        const float itemWidth = IMW::CalcCheckboxSize(displayPath.c_str()).preferred.x + ImGui::GetStyle().ItemSpacing.x + trashButtonSize.x;
        if (!isFirstItem) {
            wrapIfTooLarge(itemWidth);
        }
        isFirstItem = false;

        drawSource(source, displayPath);

        if (ImGui::IsItemHovered()) {
            newHovered = source.get();
        }
    }
    m_storageInfoHovered = newHovered;

    if (sourceToRemove) {
        std::erase_if(m_dashboards, [&](const auto& dashboard) { return dashboard->storageInfo == sourceToRemove; });
        unsubscribeSource(sourceToRemove);
        std::erase(m_storageInfos, sourceToRemove);
        forceRecalculateSortingAndFilteringOfListModel();
    }

    if (!isFirstItem) {
        wrapIfTooLarge(IMW::CalcButtonSize("Add new source").x);
    }
    if (ImGui::Button("Add new source")) {
        ImGui::OpenPopup(addSourcePopupId);
    }
}

void OpenDashboardPage::drawDateFilter() {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId("dateFilterRow");
    if (drawPlusButton(true)) {
        // only one Before and one After filter can be active at a time
        auto params = std::move(m_sortFilterModel).takeParams();
        if (m_pendingDateFilterDirection == DateFilterDirection::Before) {
            std::erase_if(params.filters.filterObjects, sortModelFilterIsType<BeforeFilter>);
            params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(m_dashboards, BeforeFilter{m_pendingDateFilterDate}));
        } else {
            std::erase_if(params.filters.filterObjects, sortModelFilterIsType<AfterFilter>);
            params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(m_dashboards, AfterFilter{m_pendingDateFilterDate}));
        }
        useSortFilterParams(std::move(params));
    }
    drawRowLabel(kLastUsedText);

    ImGui::SetNextItemWidth(calcFilterDateComboWidth());
    if (auto combo = IMW::Combo(kFilterDateComboLabel, kFilterDateLabels[static_cast<std::size_t>(m_pendingDateFilterDirection)], 0)) {
        if (ImGui::Selectable(kFilterDateLabels[static_cast<int>(DateFilterDirection::Before)])) {
            m_pendingDateFilterDirection = DateFilterDirection::Before;
        }
        if (ImGui::Selectable(kFilterDateLabels[static_cast<int>(DateFilterDirection::After)])) {
            m_pendingDateFilterDirection = DateFilterDirection::After;
        }
    }

    constexpr std::size_t       dateStringMaxLength = 11;
    std::chrono::year_month_day date(std::chrono::floor<std::chrono::days>(m_pendingDateFilterDate));
    char                        dateStr[dateStringMaxLength] = {};
    std::format_to_n(dateStr, dateStringMaxLength, "{:02}/{:02}/{:04}", static_cast<unsigned>(date.day()), static_cast<unsigned>(date.month()), static_cast<int>(date.year()));

    constexpr auto dateValidator = [](ImGuiInputTextCallbackData* d) -> int {
        if (d->EventChar == '/' || (d->EventChar >= '0' && d->EventChar <= '9')) {
            return 0;
        }
        return 1;
    };

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::CalcTextSize(kExampleDate).x + ImGui::GetFrameHeight());
    if (ImGui::InputTextWithHint("##date", "today", dateStr, dateStringMaxLength, ImGuiInputTextFlags_CallbackCharFilter, dateValidator) && std::string_view(dateStr).size() == 10) {
        unsigned day = 0, month = 0;
        int      year = 0;
        std::from_chars(dateStr, dateStr + 2, day);
        std::from_chars(dateStr + 3, dateStr + 5, month);
        std::from_chars(dateStr + 6, dateStr + 10, year);
        std::chrono::year_month_day ldate{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
        if (ldate.ok()) {
            m_pendingDateFilterDate = std::chrono::sys_days(ldate);
        }
    }

    ImGui::SameLine();
    if (drawCalendarButton()) {
        ImGui::OpenPopup(calendarPopupId);
    }

    if (ImGui::IsPopupOpen(calendarPopupId)) {
        time_t dateAsTimeT = std::chrono::system_clock::to_time_t(m_pendingDateFilterDate);
        if (std::tm* temporaryTime = std::localtime(&dateAsTimeT)) { // temporaryTime is thread unsafe, some static variable internal to libc
            if (ImGui::DatePicker(calendarPopupId, *temporaryTime)) {
                m_pendingDateFilterDate = std::chrono::system_clock::from_time_t(std::mktime(temporaryTime));
            }
        } else {
            IMW::StyleColor redText(ImGuiCol_Text, LookAndFeel::instance().palette().errorColor);
            ImGui::TextUnformatted("Error parsing selected date...");
        }
    }
}

void OpenDashboardPage::drawFavoritesFilter() {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId("favoritesFilterRow");

    // the plus button is only enabled while pressing it would change the active filter
    if (drawPlusButton(hasActiveFavoritesFilter() != m_pendingFavoritesFilter)) {
        // only one favourites filter can be active at a time
        auto params = std::move(m_sortFilterModel).takeParams();
        std::erase_if(params.filters.filterObjects, sortModelFilterIsType<FavoriteFilter>);
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(m_dashboards, FavoriteFilter{m_pendingFavoritesFilter}));
        useSortFilterParams(std::move(params));
    }
    drawRowLabel("Items that are");

    const auto favoritesFilterToString = [](FavoritesFilter filter) { return filter == FavoritesFilter::ShowFavorited ? "favorited" : "not favorited"; };

    ImGui::SetNextItemWidth(IMW::CalcComboSize("##favoritesFilterCombo", favoritesFilterToString(FavoritesFilter::ShowUnfavorited), ImGuiComboFlags_None).preferred.x);
    if (auto comboBox = IMW::Combo{"##favoritesFilterCombo", favoritesFilterToString(m_pendingFavoritesFilter), ImGuiComboFlags_None}) {
        if (ImGui::Selectable(favoritesFilterToString(FavoritesFilter::ShowFavorited))) {
            m_pendingFavoritesFilter = FavoritesFilter::ShowFavorited;
        }
        if (ImGui::Selectable(favoritesFilterToString(FavoritesFilter::ShowUnfavorited))) {
            m_pendingFavoritesFilter = FavoritesFilter::ShowUnfavorited;
        }
    }
}

void OpenDashboardPage::drawTagFilter() {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId("tagFilterRow");
    const float      rowWidth  = ImGui::GetContentRegionAvail().x;
    bool             addFilter = drawPlusButton(!m_pendingTagFilterInput.empty());
    drawRowLabel("Tag: ");
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize(), rowWidth - (ImGui::GetCursorPosX() - ImGui::GetCursorStartPos().x)));
    addFilter |= ImGui::InputText("##tagFilterInput", &m_pendingTagFilterInput, ImGuiInputTextFlags_EnterReturnsTrue);

    if (addFilter && !m_pendingTagFilterInput.empty()) {
        auto params = std::move(m_sortFilterModel).takeParams();
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(m_dashboards, TagFilter{std::move(m_pendingTagFilterInput)}));
        m_pendingTagFilterInput.clear();
        useSortFilterParams(std::move(params));
    }
}

void OpenDashboardPage::drawKeyValueFilter() {
    IMW::Font        font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);
    IMW::ChangeStrId rowId("keyValueFilterRow");
    const bool       hasKey = !m_pendingKeyValueFilterKeyInput.empty();

    // two inputs share whatever the plus button, the two labels and the spacing between all five widgets leave over
    const float spacing     = ImGui::GetStyle().ItemSpacing.x;
    const float labelsWidth = ImGui::CalcTextSize("Key: ").x + ImGui::CalcTextSize("Value: ").x;
    const float fixedWidth  = ImGui::GetFrameHeight() + labelsWidth + 4.f * spacing;
    const float inputWidth  = std::max(ImGui::GetFontSize(), (ImGui::GetContentRegionAvail().x - fixedWidth) * 0.5f);

    bool addFilter = drawPlusButton(hasKey && !m_pendingKeyValueFilterValueInput.empty());
    drawRowLabel("Key: ");
    ImGui::SetNextItemWidth(inputWidth);
    addFilter |= ImGui::InputText("##keyValueFilterKeyInput", &m_pendingKeyValueFilterKeyInput, ImGuiInputTextFlags_EnterReturnsTrue);
    {
        IMW::StyleColor labelColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(hasKey ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        drawRowLabel("Value: ");
    }
    {
        IMW::Disabled disabled(!hasKey);
        ImGui::SetNextItemWidth(inputWidth);
        addFilter |= ImGui::InputText("##keyValueFilterValueInput", &m_pendingKeyValueFilterValueInput, ImGuiInputTextFlags_EnterReturnsTrue);
    }

    if (addFilter && hasKey && !m_pendingKeyValueFilterValueInput.empty()) {
        auto params = std::move(m_sortFilterModel).takeParams();
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(m_dashboards, KeyValueFilter{std::move(m_pendingKeyValueFilterKeyInput), std::move(m_pendingKeyValueFilterValueInput)}));
        m_pendingKeyValueFilterKeyInput.clear();
        m_pendingKeyValueFilterValueInput.clear();
        useSortFilterParams(std::move(params));
    }
}

void OpenDashboardPage::drawViewOptionsSection() {
    IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);

    const auto drawModeSelectable = [this](const char* label, ListViewMode mode) {
        const ImVec2 padding   = ImGui::GetStyle().FramePadding * 2.f;
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        // use SelectableTextAlign to position text at a distance (padding) from the left edge, requires some math to convert to percentage of available space
        const float   leftoverWidth = std::max(1.f, ImGui::GetContentRegionAvail().x - labelSize.x);
        IMW::StyleVar textAlign(ImGuiStyleVar_SelectableTextAlign, ImVec2{std::min(1.f, padding.x / leftoverWidth), 0.5f});
        // use the size parameter of Selectable to add padding vertically
        if (ImGui::Selectable(label, m_viewMode == mode, ImGuiSelectableFlags_None, ImVec2{0.f, labelSize.y + 2.f * padding.y}) && m_viewMode != mode) {
            const bool customTreeChanged = (m_viewMode == ListViewMode::CustomTree) != (mode == ListViewMode::CustomTree);
            m_viewMode                   = mode;
            m_scrollToSelected           = true;
            if (customTreeChanged) {
                forceRecalculateSortingAndFilteringOfTreeModel(); // the path function depends on the view mode
            }
        }
    };
    drawModeSelectable("Table", ListViewMode::Table);
    drawModeSelectable("File Tree", ListViewMode::FileTree);
    drawModeSelectable("Custom Tree", ListViewMode::CustomTree);
    if (m_viewMode == ListViewMode::CustomTree) {
        drawCustomTreeKeyInputs();
    }
}

void OpenDashboardPage::drawCustomTreeKeyInputs() {
    ImGui::Indent();
    const ImVec2 squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};
    const float  inputWidth = ImGui::GetFontSize() * 12.f;

    std::optional<std::size_t> rowToDelete;
    for (std::size_t row = 0UZ; row < m_customTreeKeyInputs.size(); ++row) {
        IMW::ChangeId rowId(static_cast<int>(row));
        const bool    isLastRow = row + 1UZ == m_customTreeKeyInputs.size();
        if (isLastRow && m_focusLastCustomTreeKeyInput) {
            ImGui::SetKeyboardFocusHere();
            m_focusLastCustomTreeKeyInput = false;
        }
        ImGui::SetNextItemWidth(inputWidth);
        const bool enterPressed = ImGui::InputText("##customTreeKey", &m_customTreeKeyInputs[row], ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();

        if (isLastRow) {
            const bool canAddRow = !m_customTreeKeyInputs[row].empty();
            if (drawPlusButton(canAddRow) || (enterPressed && canAddRow)) {
                m_customTreeKeyInputs.emplace_back();
                m_focusLastCustomTreeKeyInput = true;
            }
        } else {
            ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
            if (ImGui::Button(kIconTrash, squareButtonSize)) {
                rowToDelete = row;
            }
            ImGui::PopFont();
        }
    }
    if (rowToDelete) {
        m_customTreeKeyInputs.erase(m_customTreeKeyInputs.begin() + static_cast<std::ptrdiff_t>(*rowToDelete));
    }

    std::vector<std::string> nonEmptyKeys;
    std::ranges::copy_if(m_customTreeKeyInputs, std::back_inserter(nonEmptyKeys), [](const std::string& key) { return !key.empty(); });
    if (nonEmptyKeys != m_customTreeKeys) {
        m_customTreeKeys = std::move(nonEmptyKeys);
        forceRecalculateSortingAndFilteringOfTreeModel();
    }
    ImGui::Unindent();
}

void OpenDashboardPage::drawActiveFilterTags() {
    if (m_sortFilterModel.numFilters() == 0) {
        return;
    }

    ImGui::Spacing();
    components::FilterTagBar tagBar("##dashboardFilterTags");

    std::size_t index = 0;
    for (const auto& filter : m_sortFilterModel.filters().filterObjects) {
        tagBar.beginTag(static_cast<std::size_t>(index));
        ImGui::TextUnformatted(filterTagLabel(*filter).c_str());
        tagBar.endTag();
        ++index;
    }

    if (const auto removedIndex = tagBar.finish()) {
        auto params = std::move(m_sortFilterModel).takeParams();
        params.filters.filterObjects.erase(params.filters.filterObjects.begin() + static_cast<std::ptrdiff_t>(*removedIndex));
        useSortFilterParams(std::move(params));
    }
}

/// Sometimes we change the mutable fields in dashboards, in which case we force refilter everything
std::optional<FavoritesFilter> OpenDashboardPage::hasActiveFavoritesFilter() {
    for (const auto& filter : m_sortFilterModel.filters().filterObjects) {
        if (const auto* favoriteFilter = std::get_if<FavoriteFilter>(&static_cast<const DashboardFilter&>(*filter).filterData)) {
            return favoriteFilter->shown;
        }
    }
    return std::nullopt;
}

void OpenDashboardPage::forceRecalculateSortingAndFilteringOfListModel() { useSortFilterParams(std::move(m_sortFilterModel).takeParams()); }

void OpenDashboardPage::useSortFilterParams(components::SortFilterModelParams&& params) {
    params.numViewedItems = m_dashboards.size();
    if (const auto* comparisonOperator = std::get_if<std::unique_ptr<components::SortFilterModelComparator>>(&params.sortStrategy); comparisonOperator != nullptr && *comparisonOperator == nullptr) {
        params.sortStrategy = std::make_unique<DashboardComparator>(m_dashboards, DashboardSortKind::lastUsed);
    }
    components::SortFilterModel newModel(std::move(params));
    m_sortFilterModel = std::move(newModel);
    forceRecalculateSortingAndFilteringOfTreeModel();
}

void OpenDashboardPage::setDashboardsSort(components::SortFilterModelSortStrategy strategy) {
    auto params         = std::move(m_sortFilterModel).takeParams();
    params.sortStrategy = std::move(strategy);
    useSortFilterParams(std::move(params));
}

void OpenDashboardPage::pushBackDashboard(std::shared_ptr<const DashboardDescription> dashboard) {
    m_dashboards.push_back(dashboard);
    forceRecalculateSortingAndFilteringOfListModel();
}

void OpenDashboardPage::drawSearchInput() {
    IMW::Font font(LookAndFeel::instance().fontBig[LookAndFeel::instance().prototypeMode]);

    const float inputFrameHeight = ImGui::GetFrameHeight();
    {
        // TODO fix magnifying glass icon here, looks a bit weird, I think it may be more of an issue with the font
        IMW::Font iconFont(LookAndFeel::instance().fontIconsSolidBig);
        drawVerticallyCenteredText(kIconSearch, inputFrameHeight);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    const bool sortingBySearch = m_sortFilterModel.scoreEvaluator() != nullptr;

    bool edited = false;
    {
        // grey out the search text while it is not the thing being sorted by
        IMW::StyleColor greyedOutText(ImGuiCol_Text, ImGui::GetStyleColorVec4(sortingBySearch ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        edited = ImGui::InputTextWithHint("##dashboardSearch", "Search dashboards...", &m_searchString);
    }
    const bool searchInputActive = ImGui::IsItemActive();

    if (sortingBySearch && m_searchString.empty()) {
        setDashboardsSort(std::make_unique<DashboardComparator>(m_dashboards, DashboardSortKind::lastUsed));
    } else if (!m_searchString.empty() && (edited || (searchInputActive && !sortingBySearch))) {
        setDashboardsSort(makeRelevanceSortStrategy());
    }
}

components::SortFilterModelSortStrategy OpenDashboardPage::makeRelevanceSortStrategy() {
    auto scoreFunction = [dashboards = &m_dashboards, searchString = &m_searchString](std::size_t index) -> float { //
        return static_cast<float>(components::filterTypename((*dashboards)[index]->name, *searchString).score);
    };
    return components::makeSimpleSortFilterModelScoreEvaluator(std::move(scoreFunction));
}

void OpenDashboardPage::drawSortByCombo() {
    constexpr const char* kSortMostRecentlyUsed  = "by most recently used";
    constexpr const char* kSortLeastRecentlyUsed = "by least recently used";
    constexpr const char* kSortName              = "alphabetically by name";
    constexpr const char* kSortNameReversed      = "reverse alphabetically by name";
    constexpr const char* kSortSource            = "alphabetically by source URI";
    constexpr const char* kSortSourceReversed    = "reverse alphabetically by source URI";
    constexpr const char* kSortRelevance         = "by relevance of names to the search";
    constexpr std::array  kAllSortLabels         = {kSortMostRecentlyUsed, kSortLeastRecentlyUsed, kSortName, kSortNameReversed, kSortSource, kSortSourceReversed, kSortRelevance};

    const char* currentLabel = kSortMostRecentlyUsed;
    if (m_sortFilterModel.scoreEvaluator() != nullptr) {
        currentLabel = kSortRelevance;
    } else if (const auto* comparator = m_sortFilterModel.comparisonOperator()) {
        const auto* dashboardComparator = static_cast<const DashboardComparator*>(comparator);
        switch (dashboardComparator->kind) {
        case DashboardSortKind::lastUsed: currentLabel = dashboardComparator->reversed ? kSortLeastRecentlyUsed : kSortMostRecentlyUsed; break;
        case DashboardSortKind::name: currentLabel = dashboardComparator->reversed ? kSortNameReversed : kSortName; break;
        case DashboardSortKind::source: currentLabel = dashboardComparator->reversed ? kSortSourceReversed : kSortSource; break;
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("           Sort dashboards ");
    ImGui::SameLine();

    const auto  comboWidthFor = [](const char* label) { return IMW::CalcComboSize("##sortByCombo", label, ImGuiComboFlags_None).preferred.x; };
    const float comboWidth    = std::ranges::max(kAllSortLabels | std::views::transform(comboWidthFor));
    ImGui::SetNextItemWidth(comboWidth);
    if (auto combo = IMW::Combo("##sortByCombo", currentLabel, ImGuiComboFlags_None)) {
        const auto sortSelectable = [this, currentLabel](const char* label, DashboardSortKind kind, bool reversed) {
            if (ImGui::Selectable(label, currentLabel == label)) {
                setDashboardsSort(std::make_unique<DashboardComparator>(m_dashboards, kind, reversed));
            }
        };
        sortSelectable(kSortMostRecentlyUsed, DashboardSortKind::lastUsed, false);
        sortSelectable(kSortLeastRecentlyUsed, DashboardSortKind::lastUsed, true);
        sortSelectable(kSortName, DashboardSortKind::name, false);
        sortSelectable(kSortNameReversed, DashboardSortKind::name, true);
        sortSelectable(kSortSource, DashboardSortKind::source, false);
        sortSelectable(kSortSourceReversed, DashboardSortKind::source, true);
        {
            IMW::Disabled disabled(m_searchString.empty());
            if (ImGui::Selectable(kSortRelevance, currentLabel == kSortRelevance)) {
                setDashboardsSort(makeRelevanceSortStrategy());
            }
        }
    }
}

OpenDashboardPage::ViewResult OpenDashboardPage::drawDashboardTable(Dashboard* optionalDashboard, ImVec2 size) {
    m_sortFilterModel.work();

    const auto shownItems  = m_sortFilterModel.items();
    const auto dashboardAt = [this](const components::SortFilterModel::ScoredIndex& item) -> const std::shared_ptr<const DashboardDescription>& { return m_dashboards[item.second]; };

    ViewResult result;

    float starButtonWidth = 0.f;
    {
        IMW::Font iconFont(LookAndFeel::instance().fontIcons);
        starButtonWidth = IMW::CalcButtonSize(kIconStar).x;
    }

    const float cellPaddingY  = ImGui::GetStyle().CellPadding.y;
    const float rowHeight     = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeightWithSpacing() * 3.f);
    const float previewHeight = rowHeight - 2.f * cellPaddingY;
    const float previewWidth  = previewHeight * DashboardPreview::preferredAspectRatio;

    // text vertically centered within a table row
    const auto drawCenteredText = [rowHeight](const char* text) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(text);
    };

    const float elementHeight = rowHeight + 2.f * cellPaddingY;

    std::optional<std::size_t> scrollToRow;
    if (m_scrollToTop) {
        scrollToRow = 0UZ;
    } else if (m_scrollToSelected && m_selectedDashboard != nullptr) {
        if (const auto selectedIt = std::ranges::find(shownItems, m_selectedDashboard, dashboardAt); selectedIt != shownItems.end()) {
            scrollToRow = static_cast<std::size_t>(std::distance(shownItems.begin(), selectedIt));
        }
    }
    m_scrollToTop      = false;
    m_scrollToSelected = false;

    constexpr static auto columnHeaderLabels = std::to_array<std::string>({"", "", "Dashboard", "Source", kLastUsedText});
    const std::array      fixedColumnWidths{starButtonWidth + 2.f * ImGui::GetStyle().CellPadding.x, previewWidth + 2.f * ImGui::GetStyle().CellPadding.x, 0.f, 0.f, 0.f};

    constexpr std::size_t kNameColumn     = 2UZ;
    constexpr std::size_t kSourceColumn   = 3UZ;
    constexpr std::size_t kLastUsedColumn = 4UZ;

    using SortMarker = components::VirtualScrollTableParams::SortMarker;
    std::optional<SortMarker> sortMarker;
    if (const auto* comparator = m_sortFilterModel.comparisonOperator()) {
        const auto* dashboardComparator = static_cast<const DashboardComparator*>(comparator);
        const auto  flipIfReversed      = [reversed = dashboardComparator->reversed](ImGuiSortDirection direction) {
            if (!reversed) {
                return direction;
            }
            return direction == ImGuiSortDirection_Ascending ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending;
        };
        switch (dashboardComparator->kind) {
        case DashboardSortKind::lastUsed: sortMarker = SortMarker{.columnIndex = kLastUsedColumn, .direction = flipIfReversed(ImGuiSortDirection_Descending)}; break;
        case DashboardSortKind::name: sortMarker = SortMarker{.columnIndex = kNameColumn, .direction = flipIfReversed(ImGuiSortDirection_Ascending)}; break;
        case DashboardSortKind::source: sortMarker = SortMarker{.columnIndex = kSourceColumn, .direction = flipIfReversed(ImGuiSortDirection_Ascending)}; break;
        }
    }

    components::VirtualScrollTable table(
        {
            .numElements       = shownItems.size(),
            .elementHeight     = elementHeight,
            .columns           = columnHeaderLabels,
            .fixedColumnWidths = fixedColumnWidths,
            .scrollToElement   = scrollToRow,
            .columnHeaderFont  = LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode],
            .sortMarker        = sortMarker,
        },
        ImVec2{0.f, size.y});

    result.contentAreaVerticalOffset = table.columnHeaderRowHeight;

    while (auto visibleRange = table.step()) {
        for (std::size_t row = visibleRange->first; row < visibleRange->second; ++row) {
            const auto& description = dashboardAt(shownItems[row]);

            IMW::ChangeStrId outerId(description->storageInfo->path.c_str());
            IMW::ChangeStrId innerId(description->name.c_str());

            table.beginRow();
            ImGui::TableNextColumn();

            const ImVec2 rowCursor     = ImGui::GetCursorPos();
            const float  starCellWidth = ImGui::GetContentRegionAvail().x;
            const bool   isSelected    = m_selectedDashboard == description;
            if (ImGui::Selectable("##dashboardRow", isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0.f, rowHeight})) {
                m_selectedDashboard = description;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                result.dashboardToLoad = description;
            }

            const bool isLoadedDashboard = optionalDashboard != nullptr && optionalDashboard->isInitialised && optionalDashboard->description && description->name == optionalDashboard->description->name && description->storageInfo == optionalDashboard->description->storageInfo;
            if (isLoadedDashboard) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_HeaderActive), 0.f, 0, 2.f);
            }

            ImGui::SetCursorPos({rowCursor.x + std::max(0.f, (starCellWidth - starButtonWidth) * 0.5f), rowCursor.y + std::max(0.f, (rowHeight - ImGui::GetFrameHeight()) * 0.5f)});
            {
                IMW::Font starFont(description->isFavorite ? LookAndFeel::instance().fontIconsSolid : LookAndFeel::instance().fontIcons);
                if (ImGui::Button(kIconStar)) {
                    description->isFavorite = !description->isFavorite;
                    if (hasActiveFavoritesFilter()) {
                        result.dashboardAction = DashboardAction::favoriteChanged;
                    }
                }
            }

            ImGui::TableNextColumn();
            {
                const ImVec2 previewSize{previewWidth, previewHeight};
                const ImVec2 cellTop = ImGui::GetCursorScreenPos();
                const ImVec2 previewTop{cellTop.x + std::max(0.f, (ImGui::GetContentRegionAvail().x - previewSize.x) * 0.5f), cellTop.y + std::max(0.f, (rowHeight - previewSize.y) * 0.5f)};
                ImGui::SetCursorScreenPos(previewTop);

                // NOTE: this is potentially doing IO, it is important that list virtualization is working otherwise we would dispatch
                // a request for every single dashboard on the first frame and potentially parse them all at once as well
                const DashboardPreview* preview = findPreviewOrElseTryFetchAndParseDashboard(description);
                ImGui::Dummy(previewSize);
                if (preview != nullptr) {
                    preview->draw(previewTop, {previewTop.x + previewSize.x, previewTop.y + previewSize.y});
                    if (ImGui::BeginItemTooltip()) {
                        const ImVec2 tooltipTop = ImGui::GetCursorScreenPos();
                        ImGui::Dummy({previewSize.x * 3.f, previewSize.y * 3.f});
                        preview->draw(tooltipTop, {tooltipTop.x + previewSize.x * 3.f, tooltipTop.y + previewSize.y * 3.f});
                        ImGui::EndTooltip();
                    }
                } else {
                    // placeholder in case the preview is still loading or failed, so the rows stay the same size
                    ImGui::GetWindowDrawList()->AddRectFilled(previewTop, {previewTop.x + previewSize.x, previewTop.y + previewSize.y}, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.f);
                }
            }

            ImGui::TableNextColumn();
            drawCenteredText(description->name.c_str());

            ImGui::TableNextColumn();
            drawCenteredText(description->storageInfo->path.c_str());

            ImGui::TableNextColumn();
            if (description->lastUsed) {
                const std::chrono::year_month_day ymd  = std::chrono::floor<std::chrono::days>(*description->lastUsed);
                const auto                        date = std::format("{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
                drawCenteredText(date.c_str());
            } else {
                drawCenteredText("never");
            }
        }
    }
    if (table.clickedColumn == kNameColumn) {
        result.dashboardAction = DashboardAction::sortByName;
    } else if (table.clickedColumn == kSourceColumn) {
        result.dashboardAction = DashboardAction::sortBySource;
    } else if (table.clickedColumn == kLastUsedColumn) {
        result.dashboardAction = DashboardAction::sortByLastUsed;
    }

    return result;
}

void OpenDashboardPage::applyDashboardAction(DashboardAction change) {
    // clicking the column of the already active sort reverses its direction
    const auto sortByColumn = [this](DashboardSortKind kind) {
        const auto* comparator = static_cast<const DashboardComparator*>(m_sortFilterModel.comparisonOperator());
        const bool  reversed   = comparator != nullptr && comparator->kind == kind && !comparator->reversed;
        setDashboardsSort(std::make_unique<DashboardComparator>(m_dashboards, kind, reversed));
    };
    switch (change) {
    case DashboardAction::none: break;
    case DashboardAction::favoriteChanged: forceRecalculateSortingAndFilteringOfListModel(); break;
    case DashboardAction::sortByName: sortByColumn(DashboardSortKind::name); break;
    case DashboardAction::sortBySource: sortByColumn(DashboardSortKind::source); break;
    case DashboardAction::sortByLastUsed: sortByColumn(DashboardSortKind::lastUsed); break;
    }
}

OpenDashboardPage::ViewResult OpenDashboardPage::drawDashboardFileTree(ImVec2 size) {
    m_sortFilterTreeModel.work();

    // force open item if we are scrolling to it
    std::optional<std::size_t> selectedIndex;
    std::vector<std::string>   selectedPath;
    if (m_scrollToSelected && m_selectedDashboard != nullptr) {
        if (const auto selectedIt = std::ranges::find(m_dashboards, m_selectedDashboard); selectedIt != m_dashboards.end()) {
            selectedIndex = static_cast<std::size_t>(selectedIt - m_dashboards.begin());
            selectedPath  = m_viewMode == ListViewMode::CustomTree ? customDashboardTreePath(*selectedIndex) : dashboardTreePath(*selectedIndex);
        }
    }

    ViewResult result;

    {
        IMW::Child treeChild("##dashboardFileTree", size, 0, ImGuiWindowFlags_HorizontalScrollbar);
        if (m_scrollToTop) {
            ImGui::SetScrollY(0.f);
        }

        // draw date, might be unreadable but the user can scroll horizontally to it
        const float dateColumnWidth      = ImGui::CalcTextSize("00/00/0000").x;
        const auto  drawRightAlignedDate = [dateColumnWidth](const char* dateText) {
            ImGui::SameLine();
            const float  visibleRightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().ScrollbarSize - ImGui::GetStyle().WindowPadding.x;
            const ImVec2 cursor           = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2{std::max(cursor.x + ImGui::GetStyle().ItemSpacing.x, visibleRightEdge - dateColumnWidth), cursor.y});
            IMW::StyleColor dimmedDate(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(dateText);
        };

        const auto drawLeafRow = [this, &result, selectedIndex, &drawRightAlignedDate](std::size_t itemIndex) {
            const auto&   description = m_dashboards[itemIndex];
            IMW::ChangeId leafId(static_cast<int>(itemIndex));

            {
                // explicitly sized to match the selectable next to it, a bare SmallButton would hug the glyph
                const ImVec2 starButtonSize{ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()};
                ImGui::PushFont(description->isFavorite ? LookAndFeel::instance().fontIconsSolid : LookAndFeel::instance().fontIcons, starButtonSize.y * 0.75f);
                if (ImGui::Button(kIconStar, starButtonSize)) {
                    description->isFavorite = !description->isFavorite;
                    if (hasActiveFavoritesFilter()) {
                        result.dashboardAction = DashboardAction::favoriteChanged;
                    }
                }
                ImGui::PopFont();
            }
            ImGui::SameLine();

            const bool isSelected = m_selectedDashboard == description;
            if (ImGui::Selectable(description->name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2{ImGui::CalcTextSize(description->name.c_str()).x, 0.f})) {
                m_selectedDashboard = description;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    result.dashboardToLoad = description;
                }
            }
            if (m_scrollToSelected && selectedIndex == itemIndex) {
                ImGui::SetScrollHereY(0.5f);
            }

            if (description->lastUsed) {
                const std::chrono::year_month_day ymd = std::chrono::floor<std::chrono::days>(*description->lastUsed);
                drawRightAlignedDate(std::format("{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year())).c_str());
            } else {
                drawRightAlignedDate("never");
            }
        };

        const auto drawBranchNode = [this, &selectedPath](const components::SortFilterTreeModelNode& node) {
            if (m_treeSetAllOpen.has_value()) {
                ImGui::SetNextItemOpen(*m_treeSetAllOpen, ImGuiCond_Always);
            }
            const bool onSelectedPath = node.path.size() + 1UZ < selectedPath.size()   //
                                        && node.name == selectedPath[node.path.size()] //
                                        && std::ranges::equal(node.path, std::span{selectedPath}.first(node.path.size()));
            if (onSelectedPath) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            return ImGui::TreeNodeEx(node.name.data(), ImGuiTreeNodeFlags_SpanAvailWidth);
        };

        const auto beginDraw = [&drawBranchNode, &drawLeafRow](const components::SortFilterTreeModelNode& node) {
            if (node.isBranch()) {
                return drawBranchNode(node);
            }
            drawLeafRow(*node.index);
            return false;
        };
        const auto endDraw = [](const components::SortFilterTreeModelNode&) { ImGui::TreePop(); };
        m_sortFilterTreeModel.visitAllItemsDepthFirst(beginDraw, endDraw);
    }

    m_scrollToTop      = false;
    m_scrollToSelected = false;
    m_treeSetAllOpen.reset();

    return result;
}

void OpenDashboardPage::drawMatchAnyOrAllFiltersCombo() {
    constexpr const char* matchAllLabel = "All filters";
    constexpr const char* matchAnyLabel = "Any filter";

    ImGui::SetNextItemWidth(IMW::CalcComboSize("##matchModeCombo", matchAllLabel, ImGuiComboFlags_None).preferred.x);
    bool matchAllFilters = m_sortFilterModel.requiresAllFilters();
    if (auto combo = IMW::Combo("##matchModeCombo", matchAllFilters ? matchAllLabel : matchAnyLabel, ImGuiComboFlags_None)) {
        if (ImGui::Selectable(matchAllLabel)) {
            matchAllFilters = true;
        }
        if (ImGui::Selectable(matchAnyLabel)) {
            matchAllFilters = false;
        }
    }

    if (matchAllFilters != m_sortFilterModel.requiresAllFilters()) {
        auto params                = std::move(m_sortFilterModel).takeParams();
        params.filters.requiresAll = matchAllFilters;
        useSortFilterParams(std::move(params));
    }
}

void OpenDashboardPage::drawViewOverlayButtons(ImVec2 viewTopLeft, ImVec2 viewSize) {
    // Overlay buttons are drawn small, the default imgui size with little padding around the text. However the text nearly clips that way,
    // so at least add some horizontal padding while keeping the buttons unobtrusive
    IMW::StyleVar framePadding(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x * 2.f, ImGui::GetStyle().FramePadding.y});

    const float  frameHeight = ImGui::GetFrameHeight();
    const ImVec2 squareButtonSize{frameHeight, frameHeight};
    const float  spacing = ImGui::GetStyle().ItemSpacing.x;

    float overlayWidth = squareButtonSize.x;
    if (m_viewMode != ListViewMode::Table) {
        overlayWidth += IMW::CalcButtonSize("collapse all").x + IMW::CalcButtonSize("expand all").x + 2.f * spacing;
    }

    const ImVec2 oldCursorScreenPosition = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{viewTopLeft.x + viewSize.x - overlayWidth - ImGui::GetStyle().ScrollbarSize - spacing, viewTopLeft.y + viewSize.y - frameHeight - spacing});

    // with a background you can see text scrolling behind the buttons, which is kind of noisy looking
    IMW::StyleColor overlayBg(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    {
        IMW::Child overlay("##viewOverlayButtons", ImVec2{overlayWidth, frameHeight}, 0, ImGuiWindowFlags_NoScrollbar);
        if (m_viewMode != ListViewMode::Table) {
            if (ImGui::Button("collapse all")) {
                m_treeSetAllOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("expand all")) {
                m_treeSetAllOpen = true;
            }
            ImGui::SameLine();
        }
        ImGui::PushFont(LookAndFeel::instance().fontIconsSolid, frameHeight / 2.f);
        if (ImGui::Button(kIconArrowUp, squareButtonSize)) {
            m_scrollToTop = true;
        }
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(oldCursorScreenPosition);
}

const DashboardPreview* OpenDashboardPage::findPreviewOrElseTryFetchAndParseDashboard(const std::shared_ptr<const DashboardDescription>& description) {
    if (const auto it = m_previews.find(description); it != m_previews.end()) {
        return it->second.state == PreviewEntry::State::Ready ? &it->second.preview : nullptr;
    }
    m_previews.try_emplace(description);

    DashboardDescription::loadFlowgraphAndThen(
        m_restClient, description->storageInfo, description->filename,
        [this, description](std::string&& flowgraphYaml) {
            PreviewEntry& entry = m_previews[description];
            if (auto preview = DashboardPreview::fromFlowgraphYaml(flowgraphYaml)) {
                entry.preview = std::move(*preview);
                entry.state   = PreviewEntry::State::Ready;
            } else {
                entry.state = PreviewEntry::State::Failed;
            }
        },
        [this, description] { m_previews[description].state = PreviewEntry::State::Failed; });
    return nullptr;
}

void OpenDashboardPage::drawOAuthPopup() {
#ifndef __EMSCRIPTEN__
    ImGui::SetNextWindowSize({500, 0}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup(oauthPopupId, nullptr, 0)) {
        auto& session = DigitizerUi::OAuthSession::instance();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scope:");
        ImGui::SameLine();
        static std::string scope = "openid";
        ImGui::InputText("##scope", &scope);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Client ID:");
        ImGui::SameLine();
        static std::string clientid = "testclientid";
        ImGui::InputText("##clientid", &clientid);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Endpoint:");
        ImGui::SameLine();
        static std::string endpoint = "mdp://127.0.0.1:12340/oauth";
        ImGui::InputText("##endpoint", &endpoint);

        if (ImGui::Button("Sign in")) {
            session.signIn(scope, clientid, endpoint);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Roles: %s", session.availableRoles().empty() ? "N/A" : session.availableRoles().c_str());

        ImGui::Separator();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
#endif
}

void OpenDashboardPage::drawCurrentDashboardPanel(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    constexpr float panelPadding     = 15.f;
    constexpr float panelItemSpacing = 10.f;

    const bool hasDashboardAndDescription = optionalDashboard && optionalDashboard->description;

    float titleHeight    = 0.f;
    float subtitleHeight = 0.f;
    {
        IMW::Font titleFont(LookAndFeel::instance().fontBigger[LookAndFeel::instance().prototypeMode]);
        titleHeight = ImGui::GetTextLineHeightWithSpacing();
    }
    {
        IMW::Font subtitleFont(LookAndFeel::instance().fontBig[LookAndFeel::instance().prototypeMode]);
        subtitleHeight = ImGui::GetTextLineHeightWithSpacing();
    }

    const ImVec2 panelStart    = ImGui::GetCursorScreenPos();
    const float  panelWidth    = ImGui::GetContentRegionAvail().x;
    const float  previewHeight = std::max(titleHeight + subtitleHeight, ImGui::GetFrameHeight() * 2.f);
    const float  previewWidth  = previewHeight * DashboardPreview::preferredAspectRatio;
    const float  panelHeight   = previewHeight + 2.f * panelPadding;

    ImGui::GetWindowDrawList()->AddRectFilled(panelStart, panelStart + ImVec2{panelWidth, panelHeight}, ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().currentDashboardPanelBg));

    // draw preview, or else a grey placeholder
    bool         previewDrawn = false;
    const ImVec2 previewStart = panelStart + ImVec2{panelPadding, panelPadding};
    const ImVec2 previewEnd   = previewStart + ImVec2{previewWidth, previewHeight};
    if (hasDashboardAndDescription) {
        if (const auto* preview = findPreviewOrElseTryFetchAndParseDashboard(optionalDashboard->description)) {
            preview->draw(previewStart, previewEnd);
            previewDrawn = true;
        }
    }
    if (!previewDrawn) {
        ImGui::GetWindowDrawList()->AddRectFilled(previewStart, previewEnd, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.f);
    }

    // vertically centered title and optional "Currently open" subtitle
    {
        const float  textBlockHeight = hasDashboardAndDescription ? titleHeight + subtitleHeight : titleHeight;
        const ImVec2 textStart{previewEnd.x + panelItemSpacing, previewStart.y + std::max(0.f, (previewHeight - textBlockHeight) / 2.f)};
        {
            IMW::Font titleFont(LookAndFeel::instance().fontBigger[LookAndFeel::instance().prototypeMode]);
            ImGui::SetCursorScreenPos(textStart);
            ImGui::TextUnformatted(hasDashboardAndDescription ? optionalDashboard->description->name.c_str() : "No dashboard currently open");
        }
        if (hasDashboardAndDescription) {
            IMW::Font subtitleFont(LookAndFeel::instance().fontBig[LookAndFeel::instance().prototypeMode]);
            ImGui::SetCursorScreenPos(textStart + ImVec2{0, titleHeight});
            ImGui::TextUnformatted("Currently open");
        }
    }

    // save / save as / close buttons
    const auto iconTextButtonWidth = [](const char* label, const char* icon) {
        float iconWidth = 0.f;
        {
            IMW::Font iconFont(LookAndFeel::instance().fontIconsSolidLarge);
            iconWidth = ImGui::CalcTextSize(icon).x;
        }
        return 4.f * ImGui::GetStyle().FramePadding.x + iconWidth + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
    };
    const float  buttonWidth = std::max({iconTextButtonWidth("Save", kIconSave), iconTextButtonWidth("Save as", kIconSaveAs), iconTextButtonWidth("Close", kIconClose)});
    const ImVec2 buttonSize{buttonWidth, previewHeight};
    const ImVec2 buttonStart{panelStart.x + panelWidth - panelPadding - 3.f * buttonWidth - 2.f * panelItemSpacing, previewStart.y};

    {
        const bool dashboardLoaded = optionalDashboard != nullptr && optionalDashboard->isInitialised;

        IMW::Disabled disabled(!dashboardLoaded);
        {
            IMW::Disabled inMemoryDisabled(dashboardLoaded && optionalDashboard->description->storageInfo->isInMemoryDashboardStorage());
            ImGui::SetCursorScreenPos(buttonStart);
            if (doIconTextButton("Save", kIconSave, buttonSize) && dashboardLoaded) {
                if (optionalDashboardPage) {
                    std::tie(optionalDashboard->layoutType, optionalDashboard->windowLayout) = optionalDashboardPage->saveLayoutConfiguration();
                }
                optionalDashboard->save();
            }
        }

        ImGui::SetCursorScreenPos(buttonStart + ImVec2{buttonSize.x + panelItemSpacing, 0});
        if (doIconTextButton("Save as", kIconSaveAs, buttonSize)) {
            ImGui::OpenPopup("saveAsDialog");
        }

        ImGui::SetCursorScreenPos(buttonStart + ImVec2{(buttonSize.x + panelItemSpacing) * 2.f, 0});
        if (doIconTextButton("Close", kIconClose, buttonSize)) {
            requestCloseDashboard();
        }
    }

    drawSaveAsDialog(optionalDashboard, optionalDashboardPage);

    // we did everything else with custom draw so we have to register the actual space this took up with the layout
    ImGui::SetCursorScreenPos(panelStart);
    ImGui::Dummy(ImVec2{panelWidth, panelHeight});
}

void OpenDashboardPage::draw(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    ImGui::Dummy({});
    ImGui::SameLine();
    drawCurrentDashboardPanel(optionalDashboard, optionalDashboardPage);
    ImGui::Spacing();

    const auto buttonHeight = LookAndFeel::instance().mainWindowIconButtonSize();

    drawSearchInput();
    ImGui::Spacing();

    constexpr float sidebarRatio    = 1.f / 4.f;
    constexpr float sidebarMaxWidth = 320.f;

    const float sidebarWidth     = std::max(dateFilterRowWidth(), std::min(ImGui::GetContentRegionAvail().x * sidebarRatio, sidebarMaxWidth));
    const float mainContentWidth = ImGui::GetContentRegionAvail().x - sidebarWidth - ImGui::GetStyle().ItemSpacing.x;

    // vertical separator between main content and sidebar
    const ImVec2 contentTopLeft = ImGui::GetCursorScreenPos();
    const float  dividerX       = contentTopLeft.x + mainContentWidth + ImGui::GetStyle().ItemSpacing.x * 0.5f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2{dividerX, contentTopLeft.y}, ImVec2{dividerX, contentTopLeft.y + ImGui::GetContentRegionAvail().y}, ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().contentSeparator));

    std::shared_ptr<const DashboardDescription> dashboardToLoad;

    {
        IMW::Child mainContent("##dashboardMainContent", ImVec2{mainContentWidth, 0.f}, 0, 0);

        {
            IMW::Font font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode]);

            drawActiveFilterTags();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Show dashboards which match");
            ImGui::SameLine();
            drawMatchAnyOrAllFiltersCombo();
            ImGui::SameLine();
            drawSortByCombo();
        }
        ImGui::Spacing();

        // leave space for buttons below the table
        const float  tableHeight = ImGui::GetContentRegionAvail().y - (buttonHeight + ImGui::GetStyle().ItemSpacing.y);
        const ImVec2 viewSize{mainContentWidth, tableHeight};
        const ImVec2 viewTopLeft = ImGui::GetCursorScreenPos();
        ViewResult   viewResult;
        switch (m_viewMode) {
        case ListViewMode::Table: viewResult = drawDashboardTable(optionalDashboard, viewSize); break;
        case ListViewMode::FileTree:
        case ListViewMode::CustomTree: viewResult = drawDashboardFileTree(viewSize); break;
        }
        // okay, iteration and drawing is done, it is safe to change the models
        applyDashboardAction(viewResult.dashboardAction);
        dashboardToLoad = std::move(viewResult.dashboardToLoad);

        // draw loading bar for models that are incomplete
        if (m_viewMode == ListViewMode::Table ? !m_sortFilterModel.isComplete() : !m_sortFilterTreeModel.isComplete()) {
            const float progress                = m_viewMode == ListViewMode::Table ? m_sortFilterModel.progress() : m_sortFilterTreeModel.progress();
            const float progressBarHeight       = ImGui::GetFrameHeight();
            const auto  oldCursorScreenPosition = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(viewTopLeft + ImVec2{0, viewSize.y - progressBarHeight});
            ImGui::ProgressBar(progress, ImVec2{viewSize.x, progressBarHeight});
            ImGui::SetCursorScreenPos(oldCursorScreenPosition);
        }

        drawViewOverlayButtons(viewTopLeft, viewSize);

        const auto buttonSize = ImVec2{0, buttonHeight};
        {
            IMW::Disabled disabled(m_selectedDashboard == nullptr);
            const auto    loadButtonMinSize = IMW::CalcButtonSize("Load");
            const auto    loadButtonSize    = ImVec2{loadButtonMinSize.x + ImGui::GetStyle().ItemInnerSpacing.x * 4.f, buttonHeight};
            if (ImGui::Button("Load", loadButtonSize) && m_selectedDashboard != nullptr) {
                // it is okay to override dashboardToLoad because there is only one interaction per frame
                dashboardToLoad = m_selectedDashboard;
            }
        }
        ImGui::SameLine();

        constexpr const char* openEmptyDashboardButtonLabel     = "Open empty dashboard";
        constexpr const char* openNewDigitizerWindowButtonlabel = "Open a new Digitizer Window";

        auto openButtonsTotalWidth = IMW::CalcAdjacentButtonSizes(std::array{openEmptyDashboardButtonLabel, openNewDigitizerWindowButtonlabel}).x;

#ifndef __EMSCRIPTEN__
        constexpr const char* openOAuthPopupLabel = "OAuth/RBAC";
        if (Digitizer::Settings::instance().editableMode && LookAndFeel::instance().prototypeMode) {
            openButtonsTotalWidth += ImGui::GetStyle().ItemSpacing.x + IMW::CalcButtonSize(openOAuthPopupLabel).x;
        }
#endif

        wrapIfTooLarge(openButtonsTotalWidth, mainContentWidth);

        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2{std::max(0.f, ImGui::GetContentRegionAvail().x - openButtonsTotalWidth), 0});

        if (ImGui::Button(openEmptyDashboardButtonLabel, buttonSize)) {
            requestLoadDashboard(nullptr);
        }
        ImGui::SameLine();
        if (ImGui::Button(openNewDigitizerWindowButtonlabel, buttonSize)) {
            // TODO: ivan
            // app->openNewWindow();
        }

#ifndef __EMSCRIPTEN__
        if (Digitizer::Settings::instance().editableMode && LookAndFeel::instance().prototypeMode) {
            ImGui::SameLine();
            if (ImGui::Button("OAuth/RBAC", buttonSize)) {
                ImGui::OpenPopup(oauthPopupId);
            }
        }
        drawOAuthPopup();
#endif
    }

    ImGui::SameLine();

    {
        IMW::Child sidebar("##dashboardSidebar", ImVec2{0.f, 0.f}, 0, 0);

        ImGui::SeparatorText("Filter options");
        drawDateFilter();
        drawFavoritesFilter();
        drawTagFilter();
        drawKeyValueFilter();

        drawSourcesSection();
        drawAddSourcePopup(); // drawSourcesSection() may open the popup

        ImGui::SeparatorText("View options");
        drawViewOptionsSection();
    }

    if (dashboardToLoad) {
        requestLoadDashboard(dashboardToLoad);
    }
}

void OpenDashboardPage::drawAddSourcePopup() {
    ImGui::SetNextWindowSize({600, 0.f}, ImGuiCond_Once);
    if (auto popup = IMW::ModalPopup(addSourcePopupId, nullptr, 0)) {
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
        if (components::DialogButtons(okEnabled) == components::DialogButton::Ok) {
            addDashboard(path);
        }
    }
}

std::shared_ptr<const DashboardDescription> OpenDashboardPage::get(const size_t index) {
    if (m_dashboards.size() > index) {
        return {m_dashboards.at(index)};
    }
    return {};
}

} // namespace DigitizerUi
