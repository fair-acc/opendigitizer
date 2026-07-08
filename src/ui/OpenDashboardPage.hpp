#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Dashboard.hpp"
#include "components/DashboardPreview.hpp"
#include "components/SortFilterModel.hpp"
#include "components/SortFilterTreeModel.hpp"

namespace opencmw::client {
class RestClient;
}

namespace DigitizerUi {
class DashboardPage;

enum class FavoritesFilter {
    ShowUnfavorited,
    ShowFavorited,
};

class OpenDashboardPage {
public:
    explicit OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient);
    ~OpenDashboardPage();

    std::function<void()>                                                   requestCloseDashboard;
    std::function<void(const std::shared_ptr<const DashboardDescription>&)> requestLoadDashboard;

    void draw(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);

    void addDashboard(std::string_view path);
    void addDashboard(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const auto& n);

    std::shared_ptr<const DashboardDescription> get(const size_t index);

private:
    enum class DashboardAction {
        none,
        favoriteChanged,
        sortByName,
        sortBySource,
        sortByLastUsed,
    };

    struct ViewResult {
        std::shared_ptr<const DashboardDescription> dashboardToLoad;
        DashboardAction                             dashboardAction = DashboardAction::none;
        // we draw an overlay + put an input blocker over the content of the table or tree, but
        // in the case of the table we have a header row that should not be considered
        float contentAreaVerticalOffset = 0.f;
    };

    void                     drawCurrentDashboardPanel(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);
    void                     drawSearchInput();
    void                     drawMatchAnyOrAllFiltersCombo();
    void                     drawSortByCombo();
    void                     drawActiveFilterTags();
    [[nodiscard]] ViewResult drawDashboardTable(Dashboard* optionalDashboard, ImVec2 size);
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

    void unsubscribeSource(const std::shared_ptr<DashboardStorageInfo>& source);
    void pushBackDashboard(std::shared_ptr<const DashboardDescription> dashboard);

    [[nodiscard]] std::optional<FavoritesFilter> hasActiveFavoritesFilter();
    void                                         useSortFilterParams(components::SortFilterModelParams&& params);
    void                                         setDashboardsSort(components::SortFilterModelSortStrategy strategy);
    components::SortFilterModelParams            defaultFilterParams();
    components::SortFilterTreeModelParams        makeSortFilterTreeModelParamsThatMatchListModel();
    [[nodiscard]] std::vector<std::string>       dashboardTreePath(std::size_t index) const;
    [[nodiscard]] std::vector<std::string>       customDashboardTreePath(std::size_t index) const;
    void                                         forceRecalculateSortingAndFilteringOfTreeModel();
    void                                         forceRecalculateSortingAndFilteringOfListModel();
    components::SortFilterModelSortStrategy      makeRelevanceSortStrategy();

    /// Tries to get a simplified description of how the dashboard should look. Can return null if the dashboard
    /// cannot be loaded, is invalid, or it is waiting to distribute the work of loading previews across frames
    [[nodiscard]] const DashboardPreview* findPreviewOrElseTryFetchAndParseDashboard(const std::shared_ptr<const DashboardDescription>& description);

    enum class ListViewMode {
        Table,
        FileTree,
        CustomTree,
    };

    enum class DateFilterDirection { Before, After };

    struct PreviewEntry {
        enum class State { Loading, Ready, Failed };
        State            state = State::Loading;
        DashboardPreview preview;
    };

    std::vector<std::shared_ptr<const DashboardDescription>> m_dashboards;
    std::vector<std::shared_ptr<DashboardStorageInfo>>       m_storageInfos;
    std::shared_ptr<const DashboardDescription>              m_selectedDashboard;
    std::shared_ptr<opencmw::client::RestClient>             m_restClient;

    components::SortFilterModel     m_sortFilterModel;
    components::SortFilterTreeModel m_sortFilterTreeModel;

    std::unordered_map<std::shared_ptr<const DashboardDescription>, PreviewEntry> m_previews;

    // ephemeral UI state
    ListViewMode                                       m_viewMode = ListViewMode::Table;
    std::vector<std::string>                           m_customTreeKeys; // observed by the getItemPath function we give to the tree model
    std::chrono::time_point<std::chrono::system_clock> m_pendingDateFilterDate;
    DateFilterDirection                                m_pendingDateFilterDirection;
    FavoritesFilter                                    m_pendingFavoritesFilter = FavoritesFilter::ShowFavorited;
    std::string                                        m_pendingTagFilterInput;
    std::string                                        m_pendingKeyValueFilterKeyInput;
    std::string                                        m_pendingKeyValueFilterValueInput;
    std::string                                        m_searchString;
    std::vector<std::string>                           m_customTreeKeyInputs{std::string{}}; // like m_customTreeKeys but it may contain empty items, used for the editor in the UI
    bool                                               m_focusLastCustomTreeKeyInput = false;
    bool                                               m_scrollToSelected            = false;
    bool                                               m_scrollToTop                 = false;
    std::optional<bool>                                m_treeSetAllOpen;
    DashboardStorageInfo*                              m_storageInfoHovered = nullptr;
};

} // namespace DigitizerUi

#endif
