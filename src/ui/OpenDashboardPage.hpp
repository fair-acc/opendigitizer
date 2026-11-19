#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "Dashboard.hpp"
#include "components/DashboardPreview.hpp"
#include "components/EditableStringList.hpp"
#include "components/SearchAndFilterComponents.hpp"
#include "components/SortFilterModel.hpp"
#include "components/SortFilterTreeModel.hpp"

namespace opencmw::client {
class RestClient;
}

namespace gr::od {
struct DashboardPage;
struct DashboardsCollection;

enum class FavoritesFilter {
    ShowUnfavorited,
    ShowFavorited,
};

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

struct DashboardFilter {
    std::variant<FavoriteFilter, BeforeFilter, AfterFilter, TagFilter, KeyValueFilter> data;

    [[nodiscard]] bool        matches(const DashboardDescription& dashboard) const;
    [[nodiscard]] std::string label() const;
};

enum class DashboardSortKind {
    lastUsed, // most recently used first, never used last
    name,     // lexicographically by dashboard name
    source,   // lexicographically by source URI
};

struct DashboardSort {
    DashboardSortKind kind        = DashboardSortKind::lastUsed;
    bool              reversed    = false;
    bool              byRelevance = false;
};

struct DashboardPreviewCache {
    struct Entry {
        enum class State { Loading, Ready, Failed };
        State            state = State::Loading;
        DashboardPreview preview;
    };

    std::shared_ptr<opencmw::client::RestClient> restClient;

    std::unordered_map<std::shared_ptr<DashboardDescription>, Entry> _entries;

    /// a simplified description of the dashboard's look; nullptr when the dashboard cannot load, is invalid, or waits for its turn as preview loading is spread across frames
    [[nodiscard]] const DashboardPreview* findOrStartLoading(const std::shared_ptr<DashboardDescription>& description) {
        if (const auto it = _entries.find(description); it != _entries.end()) {
            return it->second.state == Entry::State::Ready ? &it->second.preview : nullptr;
        }
        _entries.try_emplace(description);

        DashboardDescription::loadFlowgraphAndThen(
            restClient, description->storageInfo, description->filename,
            [this, description](std::string flowgraphYaml) {
                Entry& entry = _entries[description];
                if (auto preview = DashboardPreview::fromFlowgraphYaml(flowgraphYaml)) {
                    entry.preview = std::move(*preview);
                    entry.state   = Entry::State::Ready;
                } else {
                    entry.state = Entry::State::Failed;
                }
            },
            [this, description] { _entries[description].state = Entry::State::Failed; });
        return nullptr;
    }
};

struct OpenDashboardPage {
    enum class DashboardAction {
        none,
        favoriteChanged,
        sortByName,
        sortBySource,
        sortByLastUsed,
    };

    struct ViewResult {
        std::shared_ptr<DashboardDescription> dashboardToLoad;
        DashboardAction                       dashboardAction = DashboardAction::none;
        // an overlay and an input blocker cover the table or tree content, but not the table's header row
        float contentAreaVerticalOffset = 0.f;
    };

    enum class ListViewMode {
        Table,
        FileTree,
        CustomTree,
    };

    std::function<void()>                                             requestCloseDashboard;
    std::function<void(const std::shared_ptr<DashboardDescription>&)> requestLoadDashboard;

    std::unique_ptr<DashboardsCollection> _dashboardsCollection;
    DashboardPreviewCache                 _previewCache;
    std::shared_ptr<DashboardDescription> _selectedDashboard;

    std::vector<DashboardFilter>    _filters;
    bool                            _requireAllFilters = true;
    DashboardSort                   _sort;
    components::SortFilterModel     _sortFilterModel;
    components::SortFilterTreeModel _sortFilterTreeModel;

    ListViewMode                          _viewMode = ListViewMode::Table;
    std::vector<std::string>              _customTreeKeys; // observed by the getItemPath function we give to the tree model
    components::DateFilterRow             _dateFilterRow;
    FavoritesFilter                       _pendingFavoritesFilter = FavoritesFilter::ShowFavorited;
    components::TagFilterRow              _tagFilterRow;
    components::KeyValueFilterRow         _keyValueFilterRow;
    components::SearchSortInput           _searchInput;
    components::EditableStringList        _customTreeKeyEditor; // like _customTreeKeys but it may contain empty items, used for the editor in the UI
    bool                                  _scrollToSelected = false;
    bool                                  _scrollToTop      = false;
    bool                                  _filtersShown     = false;
    std::optional<bool>                   _treeSetAllOpen;
    DashboardStorageInfo*                 _storageInfoHovered = nullptr;
    std::string                           _saveAsName;
    std::shared_ptr<DashboardStorageInfo> _saveAsStorage;
    std::string                           _addSourcePath;
    std::string                           _oauthScope    = "openid";
    std::string                           _oauthClientId = "testclientid";
    std::string                           _oauthEndpoint = "mdp://127.0.0.1:12340/oauth";

    explicit OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient);
    ~OpenDashboardPage();

    void draw(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);

    void addDashboard(std::string_view path);

    std::shared_ptr<DashboardDescription> get(const size_t index);

    void                     drawCurrentDashboardPanel(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);
    void                     drawSearchInput();
    void                     drawMatchAnyOrAllFiltersCombo();
    void                     drawSortByCombo(const char* rowLabel);
    void                     drawActiveFilterTags();
    [[nodiscard]] ViewResult drawDashboardTable(const Dashboard* optionalDashboard, ImVec2 size);
    [[nodiscard]] ViewResult drawDashboardFileTree(ImVec2 size);
    void                     drawViewOverlayButtons(ImVec2 viewTopLeft, ImVec2 viewSize);
    void                     applyDashboardAction(DashboardAction change);
    void                     drawDateFilter();
    void                     drawFavoritesFilter();
    void                     drawTagFilter();
    void                     drawKeyValueFilter();
    void                     drawSourcesSection();
    void                     drawViewOptionsSection();
    void                     drawCustomTreeKeyInputs();
    void                     drawSaveAsDialog(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);
    void                     drawOAuthPopup();
    void                     drawAddSourcePopup();
    void                     drawFilterAndViewOptions();

    [[nodiscard]] std::optional<FavoritesFilter> hasActiveFavoritesFilter();
    template<typename Kind>
    void                                   replaceFilter(Kind filter);
    void                                   setDashboardsSort(DashboardSort sort);
    [[nodiscard]] bool                     matchesFilters(std::size_t index) const;
    components::SortFilterModelParams      makeSortFilterModelParams();
    components::SortFilterTreeModelParams  makeSortFilterTreeModelParamsThatMatchListModel();
    [[nodiscard]] std::vector<std::string> dashboardTreePath(std::size_t index) const;
    [[nodiscard]] std::vector<std::string> customDashboardTreePath(std::size_t index) const;
    void                                   forceRecalculateSortingAndFilteringOfTreeModel();
    void                                   forceRecalculateSortingAndFilteringOfListModel();

    [[nodiscard]] const std::vector<std::shared_ptr<DashboardDescription>>& dashboards() const;
};

} // namespace gr::od

#endif
