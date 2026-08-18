#include "common/ImguiWrap.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "common/LookAndFeel.hpp"

#include "DashboardPage.hpp"
#include "OpenDashboardPage.hpp"

#include "OAuthSession.hpp"
#include "components/Dialog.hpp"
#include "components/NewBlockSelectorFuzzySearch.hpp"
#include "components/VirtualScrollTable.hpp"
#include "settings.hpp"

#include <gnuradio-4.0/meta/utils.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <ranges>
#include <span>
#include <utility>
#include <variant>

namespace DigitizerUi {

namespace {

constexpr const char*                  addSourcePopupId = "Include path in dashboard search##addSourcePopup";
[[maybe_unused]] constexpr const char* oauthPopupId     = "OAuth/RBAC"; // unused in emscripten builds

constexpr const char* kIconSave    = "\u{f0c7}";
constexpr const char* kIconSaveAs  = "\u{f56e}";
constexpr const char* kIconClose   = "\u{f00d}";
constexpr const char* kIconStar    = "\u{f005}";
constexpr const char* kIconTrash   = "\u{f2ed}";
constexpr const char* kIconPlus    = "\u{f067}";
constexpr const char* kIconArrowUp = "\u{f062}";

constexpr const char* kLastUsedText = "Last used";

std::string formatDate(std::chrono::time_point<std::chrono::system_clock> date) {
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(date)};
    return std::format("{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
}

void wrapIfTooLarge(float nextItemWidth, float availableWidth = ImGui::GetContentRegionAvail().x) {
    const float rightLimit = ImGui::GetCursorScreenPos().x + availableWidth;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextItemWidth <= rightLimit) {
        ImGui::SameLine();
    }
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
    const ImVec2 labelSize    = ImGui::CalcTextSize(label, nullptr, true);

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
        ImGui::RenderTextClipped(textCursor, ImVec2{textCursor.x + iconSize.x, textMax.y}, icon, nullptr, &iconSize, align, &buttonBoundingBox);
        textCursor.x += iconSize.x + gap;
    }
    ImGui::RenderTextClipped(textCursor, textMax, label, nullptr, &labelSize, align, &buttonBoundingBox);

    return pressed;
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

struct DashboardComparator final : components::SortFilterModelSortByComparisonStrategy {
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

struct DashboardTreeComparator final : components::SortFilterTreeModelSortByComparisonStrategy {
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
        .numViewedItems = dashboards().size(),
        .filters        = {.filterObjects = {}, .requiredFilter = [this](std::size_t idx) { return isDashboardSourceEnabled(dashboards(), idx); }},
        .sortStrategy   = std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::lastUsed),
    };
}

components::SortFilterTreeModelParams OpenDashboardPage::makeSortFilterTreeModelParamsThatMatchListModel() {
    components::SortFilterTreeModelParams params{.numItems = dashboards().size(), .getItemPathFunction = {}, .filters = {}, .sortStrategy = {}};
    if (m_viewMode == ListViewMode::CustomTree) {
        params.getItemPathFunction = [this](std::size_t index) { return customDashboardTreePath(index); };
    } else {
        params.getItemPathFunction = [this](std::size_t index) { return dashboardTreePath(index); };
    }
    params.filters = copyModelFilters(m_sortFilterModel.filters());

    if (const auto* tableComparator = m_sortFilterModel.comparisonOperator()) {
        const auto* dashboardComparator = static_cast<const DashboardComparator*>(tableComparator);
        params.sortStrategy             = std::make_unique<DashboardTreeComparator>(dashboards(), dashboardComparator->kind, dashboardComparator->reversed);
    } else if (m_sortFilterModel.scoreEvaluator() != nullptr) {
        // do the fuzzy search + scoring for every tree node, leaves and branches/folders
        params.sortStrategy = components::makeSimpleSortFilterTreeModelSortByScoreStrategy([this](const components::SortFilterTreeModelNode& node) { //
            return static_cast<float>(components::filterTypename(node.name, m_searchInput.text).score);
        });
    }
    return params;
}

std::vector<std::string> OpenDashboardPage::dashboardTreePath(std::size_t index) const {
    const DashboardDescription& dashboard = *dashboards()[index];
    std::vector<std::string>    path{dashboard.storageInfo->path};
    std::filesystem::path       parentPath = std::filesystem::path(dashboard.filename).parent_path();
#ifndef NDEBUG
    // keeping testdata in the testing directory is nice for organization, but it is annoying to
    // unfold many tree nodes. so pretend these entries are in root level `testdata/` instead
    constexpr std::string_view kTestdataDir = "src/ui/test/testdata/dashboards";
    if (const std::string parentStr = parentPath.generic_string(); parentStr.starts_with(kTestdataDir)) {
        parentPath = "testdata" + parentStr.substr(kTestdataDir.size());
    }
#endif
    for (const auto& component : parentPath) {
        path.push_back(component.native());
    }
    path.push_back(dashboard.name);
    return path;
}

/// returns an empty path, which makes SortFilterTreeModel skip the item, when the dashboard lacks any of the requested keys
std::vector<std::string> OpenDashboardPage::customDashboardTreePath(std::size_t index) const {
    const DashboardDescription& dashboard = *dashboards()[index];
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

OpenDashboardPage::OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient) //
    : m_dashboardsCollection(restClient),                                                     //
      m_previewCache{std::move(restClient), {}},                                              //
      m_sortFilterModel(defaultFilterParams()),                                               //
      m_sortFilterTreeModel(makeSortFilterTreeModelParamsThatMatchListModel())                //
{
    m_dashboardsCollection.onChanged = [this] { forceRecalculateSortingAndFilteringOfListModel(); };
#ifndef __EMSCRIPTEN__
    addDashboard(".");
#endif
}

OpenDashboardPage::~OpenDashboardPage() = default;

void OpenDashboardPage::addDashboard(std::string_view path) { m_dashboardsCollection.addSource(path); }

void OpenDashboardPage::drawSaveAsDialog(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    ImGui::SetNextWindowSize({600, 300}, ImGuiCond_Once);
    auto popup = IMW::ModalPopup("saveAsDialog", nullptr, 0);
    if (!popup) {
        return;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Name:");
    ImGui::SameLine();
    auto                                         desc = optionalDashboard != nullptr && optionalDashboard->isInitialised ? optionalDashboard->description : nullptr;
    static std::string                           name;
    static std::shared_ptr<DashboardStorageInfo> storageInfo;
    if (ImGui::IsWindowAppearing() && desc != nullptr) {
        name        = desc->name;
        storageInfo = !desc->storageInfo->isInMemoryDashboardStorage() || m_dashboardsCollection.sources().empty() ? desc->storageInfo : m_dashboardsCollection.sources().front();
    }
    ImGui::InputText("##name", &name);

    ImGui::TextUnformatted("Source:");
    ImGui::SameLine();

    {
        IMW::Group group;
        for (const auto& s : m_dashboardsCollection.sources()) {
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
        newDesc->filename    = storageInfo->path.starts_with("http://") || storageInfo->path.starts_with("https://") ? name : name + DashboardDescription::fileExtension;
        m_dashboardsCollection.addDashboard(newDesc);

        if (optionalDashboard != nullptr && optionalDashboard->isInitialised) {
            optionalDashboard->setNewDescription(newDesc);

            if (optionalDashboardPage) {
                std::tie(optionalDashboard->layoutType, optionalDashboard->windowLayout) = optionalDashboardPage->saveLayoutConfiguration();
            }

            optionalDashboard->save();
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

    for (const auto& source : m_dashboardsCollection.sources()) {
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
        m_dashboardsCollection.removeSource(sourceToRemove);
    }

    if (!isFirstItem) {
        wrapIfTooLarge(IMW::CalcButtonSize("Add new source").x);
    }
    if (ImGui::Button("Add new source")) {
        ImGui::OpenPopup(addSourcePopupId);
    }
}

void OpenDashboardPage::drawDateFilter() {
    if (const auto committed = m_dateFilterRow.draw("dateFilterRow", kLastUsedText)) {
        // only one Before and one After filter can be active at a time
        auto params = std::move(m_sortFilterModel).takeParams();
        if (committed->direction == components::DateFilterRow::Direction::Before) {
            std::erase_if(params.filters.filterObjects, sortModelFilterIsType<BeforeFilter>);
            params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(dashboards(), BeforeFilter{committed->date}));
        } else {
            std::erase_if(params.filters.filterObjects, sortModelFilterIsType<AfterFilter>);
            params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(dashboards(), AfterFilter{committed->date}));
        }
        useSortFilterParams(std::move(params));
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
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(dashboards(), FavoriteFilter{m_pendingFavoritesFilter}));
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
    if (auto tag = m_tagFilterRow.draw("tagFilterRow", "Tag: ")) {
        auto params = std::move(m_sortFilterModel).takeParams();
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(dashboards(), TagFilter{std::move(*tag)}));
        useSortFilterParams(std::move(params));
    }
}

void OpenDashboardPage::drawKeyValueFilter() {
    if (auto committed = m_keyValueFilterRow.draw("keyValueFilterRow")) {
        auto params = std::move(m_sortFilterModel).takeParams();
        params.filters.filterObjects.emplace_back(std::make_unique<DashboardFilter>(dashboards(), KeyValueFilter{std::move(committed->key), std::move(committed->value)}));
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
    m_customTreeKeyEditor.draw("##customTreeKeys");
    ImGui::Unindent();

    if (auto nonEmptyKeys = m_customTreeKeyEditor.nonEmptyEntries(); nonEmptyKeys != m_customTreeKeys) {
        m_customTreeKeys = std::move(nonEmptyKeys);
        forceRecalculateSortingAndFilteringOfTreeModel();
    }
}

void OpenDashboardPage::drawActiveFilterTags() {
    if (const auto removedIndex = components::drawFilterTags("##dashboardFilterTags", m_sortFilterModel.filters().filterObjects, filterTagLabel); removedIndex.has_value()) {
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
    params.numViewedItems = dashboards().size();
    if (const auto* comparisonOperator = std::get_if<std::unique_ptr<components::SortFilterModelSortByComparisonStrategy>>(&params.sortStrategy); comparisonOperator != nullptr && *comparisonOperator == nullptr) {
        params.sortStrategy = std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::lastUsed);
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

void OpenDashboardPage::drawSearchInput() {
    const bool sortingBySearch = m_sortFilterModel.scoreEvaluator() != nullptr;
    switch (m_searchInput.draw("##dashboardSearch", "Search dashboards...", sortingBySearch)) {
    case components::SearchSortInput::Event::none: break;
    case components::SearchSortInput::Event::wantsDefaultSort: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::lastUsed)); break;
    case components::SearchSortInput::Event::wantsRelevanceSort: setDashboardsSort(makeRelevanceSortStrategy()); break;
    }
}

components::SortFilterModelSortStrategy OpenDashboardPage::makeRelevanceSortStrategy() {
    auto scoreFunction = [dashboards = &this->dashboards(), searchString = &m_searchInput.text](std::size_t index) { //
        return static_cast<float>(components::filterTypename((*dashboards)[index]->name, *searchString).score);
    };
    return components::makeSimpleSortFilterModelSortByScoreStrategy(std::move(scoreFunction));
}

void OpenDashboardPage::drawSortByCombo() {
    enum SortOptionIndex : std::size_t { mostRecentlyUsed, leastRecentlyUsed, byName, byNameReversed, bySource, bySourceReversed, byRelevance };
    const std::array<components::SortOption, 7> sortOptions{{
        {"by most recently used"},
        {"by least recently used"},
        {"alphabetically by name"},
        {"reverse alphabetically by name"},
        {"alphabetically by source URI"},
        {"reverse alphabetically by source URI"},
        {"by relevance of names to the search", !m_searchInput.text.empty()},
    }};

    std::size_t currentIndex = mostRecentlyUsed;
    if (m_sortFilterModel.scoreEvaluator() != nullptr) {
        currentIndex = byRelevance;
    } else if (const auto* comparator = m_sortFilterModel.comparisonOperator()) {
        const auto* dashboardComparator = static_cast<const DashboardComparator*>(comparator);
        switch (dashboardComparator->kind) {
        case DashboardSortKind::lastUsed: currentIndex = dashboardComparator->reversed ? leastRecentlyUsed : mostRecentlyUsed; break;
        case DashboardSortKind::name: currentIndex = dashboardComparator->reversed ? byNameReversed : byName; break;
        case DashboardSortKind::source: currentIndex = dashboardComparator->reversed ? bySourceReversed : bySource; break;
        }
    }

    const auto chosen = components::drawSortByCombo("##sortByCombo", "           Sort dashboards ", sortOptions, currentIndex);
    if (!chosen.has_value()) {
        return;
    }
    switch (*chosen) {
    case mostRecentlyUsed: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::lastUsed, false)); break;
    case leastRecentlyUsed: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::lastUsed, true)); break;
    case byName: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::name, false)); break;
    case byNameReversed: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::name, true)); break;
    case bySource: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::source, false)); break;
    case bySourceReversed: setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), DashboardSortKind::source, true)); break;
    case byRelevance: setDashboardsSort(makeRelevanceSortStrategy()); break;
    }
}

OpenDashboardPage::ViewResult OpenDashboardPage::drawDashboardTable(const Dashboard* optionalDashboard, ImVec2 size) {
    m_sortFilterModel.work();

    const auto shownItems  = m_sortFilterModel.items();
    const auto dashboardAt = [this](const components::SortFilterModel::ScoredIndex& item) -> const std::shared_ptr<const DashboardDescription>& { return dashboards()[item.second]; };

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
            if (ImGui::Selectable("##dashboardRow", m_selectedDashboard == description, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0.f, rowHeight})) {
                m_selectedDashboard = description;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                result.dashboardToLoad = description;
            }

            if (optionalDashboard != nullptr && optionalDashboard->isInitialised && optionalDashboard->description && description->name == optionalDashboard->description->name && description->storageInfo == optionalDashboard->description->storageInfo) {
                // this dashboard is the loaded dashboard
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
                const DashboardPreview* preview = m_previewCache.findOrStartLoading(description);
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
                drawCenteredText(formatDate(*description->lastUsed).c_str());
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
        setDashboardsSort(std::make_unique<DashboardComparator>(dashboards(), kind, reversed));
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
        if (const auto selectedIt = std::ranges::find(dashboards(), m_selectedDashboard); selectedIt != dashboards().end()) {
            selectedIndex = static_cast<std::size_t>(selectedIt - dashboards().begin());
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
            const auto&   description = dashboards()[itemIndex];
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
                drawRightAlignedDate(formatDate(*description->lastUsed).c_str());
            } else {
                drawRightAlignedDate("never");
            }
        };

        const auto drawBranchNode = [this, &selectedPath](const components::SortFilterTreeModelNode& node) {
            if (m_treeSetAllOpen.has_value()) {
                ImGui::SetNextItemOpen(*m_treeSetAllOpen, ImGuiCond_Always);
            }
            if (node.path.size() + 1UZ < selectedPath.size()   //
                && node.name == selectedPath[node.path.size()] //
                && std::ranges::equal(node.path, std::span{selectedPath}.first(node.path.size()))) {
                // this is on the path to the selected node, so keep it open
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
    if (const auto matchAllFilters = components::drawMatchAnyOrAllFiltersCombo("##matchModeCombo", m_sortFilterModel.requiresAllFilters())) {
        auto params                = std::move(m_sortFilterModel).takeParams();
        params.filters.requiresAll = *matchAllFilters;
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
        if (const auto* preview = m_previewCache.findOrStartLoading(optionalDashboard->description)) {
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

    const float sidebarWidth     = std::max(components::DateFilterRow::rowWidth(kLastUsedText), std::min(ImGui::GetContentRegionAvail().x * sidebarRatio, sidebarMaxWidth));
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
    ImGui::SetNextWindowSize({600, ImGui::GetFrameHeight() * 4.F}, ImGuiCond_Once); // 4 line heights: window top bar, path entry, buttons, and some extra spacing
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
    if (dashboards().size() > index) {
        return {dashboards().at(index)};
    }
    return {};
}

} // namespace DigitizerUi
