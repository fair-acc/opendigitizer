#ifndef OPENDASHBOARDPAGE_H
#define OPENDASHBOARDPAGE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Dashboard.hpp"
#include "DashboardsCollection.hpp"
#include "components/DashboardPreview.hpp"
#include "components/EditableStringList.hpp"
#include "components/SearchAndFilterComponents.hpp"
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

struct DashboardPreviewCache {
    struct Entry {
        enum class State { Loading, Ready, Failed };
        State            state = State::Loading;
        DashboardPreview preview;
    };

    std::shared_ptr<opencmw::client::RestClient> restClient;

    std::unordered_map<std::shared_ptr<const DashboardDescription>, Entry> _entries;

    /// Tries to get a simplified description of how the dashboard should look. Can return null if the dashboard
    /// cannot be loaded, is invalid, or it is waiting to distribute the work of loading previews across frames
    [[nodiscard]] const DashboardPreview* findOrStartLoading(const std::shared_ptr<const DashboardDescription>& description) {
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

class OpenDashboardPage {
public:
    explicit OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient);
    ~OpenDashboardPage();

    std::function<void()>                                                   requestCloseDashboard;
    std::function<void(const std::shared_ptr<const DashboardDescription>&)> requestLoadDashboard;

    void draw(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage);

    void addDashboard(std::string_view path);

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

    [[nodiscard]] const std::vector<std::shared_ptr<const DashboardDescription>>& dashboards() const { return m_dashboardsCollection.dashboards(); }

    enum class ListViewMode {
        Table,
        FileTree,
        CustomTree,
    };

    DashboardsCollection                        m_dashboardsCollection;
    DashboardPreviewCache                       m_previewCache;
    std::shared_ptr<const DashboardDescription> m_selectedDashboard;

    components::SortFilterModel     m_sortFilterModel;
    components::SortFilterTreeModel m_sortFilterTreeModel;

    // ephemeral UI state
    ListViewMode                   m_viewMode = ListViewMode::Table;
    std::vector<std::string>       m_customTreeKeys; // observed by the getItemPath function we give to the tree model
    components::DateFilterRow      m_dateFilterRow;
    FavoritesFilter                m_pendingFavoritesFilter = FavoritesFilter::ShowFavorited;
    components::TagFilterRow       m_tagFilterRow;
    components::KeyValueFilterRow  m_keyValueFilterRow;
    components::SearchSortInput    m_searchInput;
    components::EditableStringList m_customTreeKeyEditor; // like m_customTreeKeys but it may contain empty items, used for the editor in the UI
    bool                           m_scrollToSelected = false;
    bool                           m_scrollToTop      = false;
    std::optional<bool>            m_treeSetAllOpen;
    DashboardStorageInfo*          m_storageInfoHovered = nullptr;
};

} // namespace DigitizerUi

#endif
