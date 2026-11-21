#include "common/Imw.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "common/Events.hpp"
#include "common/LookAndFeel.hpp"

#include "DashboardPage.hpp"
#include "OpenDashboardPage.hpp"

#include "OAuthSession.hpp"
#include "components/Dialog.hpp"
#include "components/NewBlockSelectorFuzzySearch.hpp"
#include "components/StyledWidgets.hpp"
#include "components/VirtualScrollTable.hpp"
#include "settings.hpp"

#include <opencmw.hpp>

#include <ClientCommon.hpp>
#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>

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

namespace gr::od {

struct DashboardsCollection {
    std::function<void()> onChanged;

    std::vector<std::shared_ptr<DashboardDescription>> _dashboards;
    std::vector<std::shared_ptr<DashboardStorageInfo>> _sources;
    std::shared_ptr<opencmw::client::RestClient>       _restClient;

    explicit DashboardsCollection(std::shared_ptr<opencmw::client::RestClient> restClient) : _restClient{std::move(restClient)} {}

    void addSource(std::string_view path) {
        _sources.push_back(DashboardStorageInfo::get(path));
        auto& storageInfo = _sources.back();

        if (storageInfo->kind() == StorageKind::Remote) {
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
            // notified when the dashboards list changes

            // also request the list to be sent immediately
            command.command = opencmw::mdp::Command::Get;
            _restClient->request(command);
#ifndef OD_DISABLE_DEMO_FLOWGRAPHS
        } else if (storageInfo->kind() == StorageKind::Example) {
            auto fs  = cmrc::sample_dashboards::get_filesystem();
            auto dir = fs.iterate_directory("assets/sampleDashboards/");
            for (auto d : dir) {
                if (d.is_file() && d.filename().ends_with(".grc")) {
                    loadDashboard(storageInfo, d.filename().substr(0, d.filename().size() - 4));
                }
            }
#endif
        } else {
#ifndef __EMSCRIPTEN__
            namespace fs = std::filesystem;
            const fs::path rootPath{path};
            if (!fs::is_directory(rootPath)) {
                return;
            }

            for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied); it != fs::recursive_directory_iterator(); ++it) {
                if (it->is_directory() && it->path().filename().native().starts_with(".")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file() && it->path().extension() == DashboardDescription::kFileExtension) {
                    loadDashboard(storageInfo, it->path().lexically_relative(rootPath).native());
                }
            }
#endif
        }
    }

    void removeSource(const std::shared_ptr<DashboardStorageInfo>& source) {
        std::erase_if(_dashboards, [&source](const auto& dashboard) { return dashboard->storageInfo == source; });
        if (source->kind() == StorageKind::Remote) {
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

    void addDashboard(std::shared_ptr<DashboardDescription> dashboard) {
        _dashboards.push_back(std::move(dashboard));
        if (onChanged) {
            onChanged();
        }
    }

    void loadDashboard(const std::shared_ptr<DashboardStorageInfo>& storageInfo, const std::string& filename) {
        DashboardDescription::loadAndThen(_restClient, storageInfo, filename, [this, storageInfo](std::shared_ptr<DashboardDescription>&& desc) {
            if (desc) {
                auto it = std::ranges::find_if(_dashboards, [&](const auto& d) { return d->storageInfo.get() == storageInfo.get() && d->name == desc->name; });
                if (it == _dashboards.end()) {
                    addDashboard(std::move(desc));
                }
            }
        });
    }

    [[nodiscard]] const std::vector<std::shared_ptr<DashboardDescription>>& dashboards() const { return _dashboards; }
    [[nodiscard]] const std::vector<std::shared_ptr<DashboardStorageInfo>>& sources() const { return _sources; }
};

namespace {

constexpr const char*                  kAddSourcePopupId = "Include path in dashboard search##addSourcePopup";
[[maybe_unused]] constexpr const char* kOauthPopupId     = "OAuth/RBAC"; // unused in emscripten builds

constexpr const char* kIconSave           = "\u{f0c7}";
constexpr const char* kIconSaveAs         = "\u{f56e}";
constexpr const char* kIconClose          = "\u{f00d}";
constexpr const char* kIconFilter         = "\u{f0b0}";
constexpr const char* kIconLoad           = "\u{f07c}";
constexpr const char* kIconNewWindow      = "\u{f35d}";
constexpr const char* kIconEmptyDashboard = "\u{f15b}";
constexpr const char* kIconStar           = "\u{f005}";
constexpr const char* kIconPlus           = "\u{f067}";
constexpr const char* kIconArrowUp        = "\u{f062}";

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

    imw::Disabled disabled(!enabled);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
    const bool pressed = button(kIconPlus, squareButtonSize);
    ImGui::PopFont();
    return pressed;
}

// a star button: not highlighted like a button or selectable, it only fills or empties the star
[[nodiscard]] bool drawFavoriteButton(bool isFavorite) {
    const ImVec2 squareButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    ImGui::PushFont(isFavorite ? LookAndFeel::instance().fontIconsSolid : LookAndFeel::instance().fontIcons, squareButtonSize.y / 2.f);
    imw::StyleColor background(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    imw::StyleColor hoveredBackground(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    imw::StyleColor activeBackground(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    const bool      pressed = ImGui::Button(kIconStar, squareButtonSize);
    ImGui::PopFont();
    return pressed;
}

/// frame-height selectable with rounded corners, respecting the style options
[[nodiscard]] bool frameHeightSelectable(const char* label, bool selected) {
    const ImGuiStyle& style    = ImGui::GetStyle();
    const char*       textEnd  = ImGui::FindRenderedTextEnd(label);
    const ImVec2      textSize = ImGui::CalcTextSize(label, textEnd);
    const bool        clicked  = ImGui::InvisibleButton(label, ImVec2{textSize.x + (2.f * style.FramePadding.x), ImGui::GetFrameHeight()}, ImGuiButtonFlags_PressedOnClick);
    const bool        hovered  = ImGui::IsItemHovered();
    const bool        held     = ImGui::IsItemActive();
    ImDrawList*       drawList = ImGui::GetWindowDrawList();
    if (selected || hovered || held) {
        drawList->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(selectableBackgroundColor(selected, hovered, held)), style.FrameRounding);
    }
    drawList->AddText(ImGui::GetItemRectMin() + style.FramePadding, ImGui::GetColorU32(selectableTextColor(selected)), label, textEnd);
    return clicked;
}

void drawRowLabel(const char* label) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
}

[[nodiscard]] bool hoverRevealedTrashButton(bool revealed, ImVec2 size) {
    constexpr const char* kIconTrash = "\u{f2ed}";

    imw::StyleFloatVar alpha(ImGuiStyleVar_Alpha, revealed ? ImGui::GetStyle().Alpha : 0.F);
    ImGui::PushFont(LookAndFeel::instance().fontIcons, size.y / 2.F);
    const bool pressed = button(kIconTrash, size);
    ImGui::PopFont();
    return pressed;
}

bool doIconButton(const char* tooltip, const char* icon, ImVec2 size) {
    bool pressed = false;
    {
        imw::Font iconFont(LookAndFeel::instance().fontIconsSolidLarge);
        pressed = button(std::format("{}###{}", icon, tooltip).c_str(), size);
    }
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

using DashboardList = std::vector<std::shared_ptr<DashboardDescription>>;

bool dashboardLess(const DashboardDescription& lhs, const DashboardDescription& rhs, DashboardSortKind kind) {
    switch (kind) {
    case DashboardSortKind::lastUsed: return lhs.lastUsed > rhs.lastUsed;
    case DashboardSortKind::name: return lhs.name < rhs.name;
    case DashboardSortKind::source: return lhs.storageInfo->path < rhs.storageInfo->path;
    }
    std::unreachable();
}

bool dashboardIndexLess(const DashboardList& dashboards, const DashboardSort& sort, std::size_t lhs, std::size_t rhs) {
    if (sort.reversed) {
        std::swap(lhs, rhs);
    }
    return dashboardLess(*dashboards[lhs], *dashboards[rhs], sort.kind);
}

bool isDashboardSourceEnabled(const DashboardList& dashboards, std::size_t index) { return index < dashboards.size() && dashboards[index]->storageInfo->isEnabled; }

void saveWithLiveLayout(Dashboard& dashboard, const DashboardPage* page) {
    const auto [layoutType, windowLayout] = page ? page->saveLayoutConfiguration() : std::pair{dashboard.layoutType, dashboard.windowLayout};
    dashboard.save(layoutType, windowLayout);
}
} // namespace

bool DashboardFilter::matches(const DashboardDescription& dashboard) const {
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
        data);
}

std::string DashboardFilter::label() const {
    return std::visit(gr::meta::overloaded{
                          [](const FavoriteFilter& favoriteFilter) -> std::string { return favoriteFilter.shown == FavoritesFilter::ShowFavorited ? "favorited" : "not favorited"; },
                          [](const BeforeFilter& beforeFilter) { return std::format("used before {}", formatDate(beforeFilter.date)); },
                          [](const AfterFilter& afterFilter) { return std::format("used after {}", formatDate(afterFilter.date)); },
                          [](const TagFilter& tagFilter) { return std::format("tag: {}", tagFilter.tag); },
                          [](const KeyValueFilter& keyValueFilter) { return std::format("{} = {}", keyValueFilter.key, keyValueFilter.value); },
                      },
        data);
}

bool OpenDashboardPage::matchesFilters(std::size_t index) const {
    if (!isDashboardSourceEnabled(dashboards(), index)) {
        return false;
    }
    if (_filters.empty()) {
        return true;
    }
    const auto matches = [&dashboard = *dashboards()[index]](const DashboardFilter& filter) { return filter.matches(dashboard); };
    return _requireAllFilters ? std::ranges::all_of(_filters, matches) : std::ranges::any_of(_filters, matches);
}

components::SortFilterModelParams OpenDashboardPage::makeSortFilterModelParams() {
    components::SortFilterModelParams params{.numViewedItems = dashboards().size(), .filter = [this](std::size_t index) { return matchesFilters(index); }, .less = {}, .score = {}};
    if (_sort.byRelevance) {
        params.score = [this](std::size_t index) { return static_cast<float>(components::filterTypename(dashboards()[index]->name, _searchInput.text).score); };
    } else {
        params.less = [this, sort = _sort](std::size_t lhs, std::size_t rhs) { return dashboardIndexLess(dashboards(), sort, lhs, rhs); };
    }
    return params;
}

components::SortFilterTreeModelParams OpenDashboardPage::makeSortFilterTreeModelParamsThatMatchListModel() {
    components::SortFilterTreeModelParams params{.numItems = dashboards().size(), .getItemPathFunction = {}, .filter = [this](std::size_t index) { return matchesFilters(index); }, .less = {}, .score = {}};
    if (_viewMode == ListViewMode::CustomTree) {
        params.getItemPathFunction = [this](std::size_t index) { return customDashboardTreePath(index); };
    } else {
        params.getItemPathFunction = [this](std::size_t index) { return dashboardTreePath(index); };
    }
    if (_sort.byRelevance) {
        // do the fuzzy search + scoring for every tree node, leaves and branches/folders
        params.score = [this](const components::SortFilterTreeModelNode& node) { return static_cast<float>(components::filterTypename(node.name, _searchInput.text).score); };
    } else {
        // branches sort before leaves, and by name
        params.less = [this, sort = _sort](const components::SortFilterTreeModelNode& lhs, const components::SortFilterTreeModelNode& rhs) {
            if (lhs.isBranch() != rhs.isBranch()) {
                return lhs.isBranch();
            }
            if (lhs.isBranch()) {
                return lhs.name < rhs.name;
            }
            return dashboardIndexLess(dashboards(), sort, *lhs.index, *rhs.index);
        };
    }
    return params;
}

std::vector<std::string> OpenDashboardPage::dashboardTreePath(std::size_t index) const {
    const DashboardDescription& dashboard = *dashboards()[index];
    std::vector<std::string>    path{dashboard.storageInfo->path};
    std::filesystem::path       parentPath = std::filesystem::path(dashboard.filename).parent_path();
#ifndef NDEBUG
    // test data stays in the testing directory, but shows as a root-level `testdata/`: no unfolding of many tree nodes
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
    for (const std::string& key : _customTreeKeys) {
        const auto valueIt = dashboard.keyValueTags.find(key);
        if (valueIt == dashboard.keyValueTags.end()) {
            return {};
        }
        path.push_back(valueIt->second);
    }
    path.push_back(dashboard.name);
    return path;
}

void OpenDashboardPage::forceRecalculateSortingAndFilteringOfTreeModel() { _sortFilterTreeModel = components::SortFilterTreeModel(makeSortFilterTreeModelParamsThatMatchListModel()); }

OpenDashboardPage::OpenDashboardPage(std::shared_ptr<opencmw::client::RestClient> restClient) //
    : _dashboardsCollection(std::make_unique<DashboardsCollection>(restClient)),              //
      _previewCache{std::move(restClient), {}},                                               //
      _sortFilterModel(makeSortFilterModelParams()),                                          //
      _sortFilterTreeModel(makeSortFilterTreeModelParamsThatMatchListModel())                 //
{
    _dashboardsCollection->onChanged = [this] { forceRecalculateSortingAndFilteringOfListModel(); };
#ifndef __EMSCRIPTEN__
    addDashboard(".");
#endif
}

OpenDashboardPage::~OpenDashboardPage() = default;

const std::vector<std::shared_ptr<DashboardDescription>>& OpenDashboardPage::dashboards() const { return _dashboardsCollection->dashboards(); }

void OpenDashboardPage::addDashboard(std::string_view path) { _dashboardsCollection->addSource(path); }

void OpenDashboardPage::drawSaveAsDialog(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    ImGui::SetNextWindowSize(LookAndFeel::instance().openDashboardPageStyle.saveAsPopupSize, ImGuiCond_Once);
    auto popup = imw::ModalPopup("saveAsDialog", nullptr, 0);
    if (!popup) {
        return;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Name:");
    ImGui::SameLine();
    auto                                   desc        = optionalDashboard != nullptr && optionalDashboard->isInitialised ? optionalDashboard->description : nullptr;
    std::string&                           name        = _saveAsName;
    std::shared_ptr<DashboardStorageInfo>& storageInfo = _saveAsStorage;
    if (ImGui::IsWindowAppearing() && desc != nullptr) {
        name        = desc->name;
        storageInfo = !desc->storageInfo->isInMemoryDashboardStorage() || _dashboardsCollection->sources().empty() ? desc->storageInfo : _dashboardsCollection->sources().front();
    }
    ImGui::InputText("##name", &name);

    ImGui::TextUnformatted("Source:");
    ImGui::SameLine();

    {
        imw::Group group;
        for (const auto& s : _dashboardsCollection->sources()) {
            bool enabled = s == storageInfo;
            if (checkbox(s->path.c_str(), &enabled)) {
                storageInfo = s;
            }
        }
        if (textButton("Add new")) {
            ImGui::OpenPopup(kAddSourcePopupId);
        }
    }

    drawAddSourcePopup();

    bool okEnabled = !name.empty() && !storageInfo->isInMemoryDashboardStorage();
    if (components::dialogButtons(okEnabled) == components::DialogButton::Ok && desc != nullptr) {
        auto newDesc         = std::make_shared<DashboardDescription>(*desc);
        newDesc->name        = name;
        newDesc->storageInfo = storageInfo;
        newDesc->filename    = storageInfo->kind() == StorageKind::Remote ? name : name + DashboardDescription::kFileExtension;
        _dashboardsCollection->addDashboard(newDesc);

        if (optionalDashboard != nullptr && optionalDashboard->isInitialised) {
            optionalDashboard->setNewDescription(newDesc);

            saveWithLiveLayout(*optionalDashboard, optionalDashboardPage);
        }
    }
}

void OpenDashboardPage::drawSourcesSection() {
    ImGui::SeparatorText("Available Sources");

    imw::Font font(LookAndFeel::instance().fontSmall());

    const ImVec2 trashButtonSize{ImGui::GetFrameHeight(), ImGui::GetFrameHeight()};

    DashboardStorageInfo*                 newHovered = nullptr;
    std::shared_ptr<DashboardStorageInfo> sourceToRemove;
    bool                                  isFirstItem = true;

    const auto drawSource = [this, trashButtonSize, &sourceToRemove](const std::shared_ptr<DashboardStorageInfo>& source, const std::string& displayName) {
        imw::Group sourceGroup;
        if (checkbox(displayName.c_str(), &source->isEnabled)) {
            forceRecalculateSortingAndFilteringOfListModel(); // we have a filter that removes things that are not enabled
        }
        ImGui::SameLine();

        if (hoverRevealedTrashButton(_storageInfoHovered == source.get(), trashButtonSize)) {
            sourceToRemove = source;
        }
    };

    for (const auto& source : _dashboardsCollection->sources()) {
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

        imw::ChangeStrId id(displayPath.c_str());

        const float itemWidth = imw::calcCheckboxSize(displayPath.c_str()).preferred.x + ImGui::GetStyle().ItemSpacing.x + trashButtonSize.x;
        if (!isFirstItem) {
            wrapIfTooLarge(itemWidth);
        }
        isFirstItem = false;

        drawSource(source, displayPath);

        if (ImGui::IsItemHovered()) {
            newHovered = source.get();
        }
    }
    _storageInfoHovered = newHovered;

    if (sourceToRemove) {
        _dashboardsCollection->removeSource(sourceToRemove);
    }

    if (!isFirstItem) {
        wrapIfTooLarge(calcTextButtonSize("Add new source").x);
    }
    if (textButton("Add new source")) {
        ImGui::OpenPopup(kAddSourcePopupId);
    }
}

void OpenDashboardPage::drawDateFilter() {
    if (const auto committed = _dateFilterRow.draw("dateFilterRow", kLastUsedText)) {
        // only one Before and one After filter can be active at a time
        if (committed->direction == components::DateFilterRow::Direction::Before) {
            replaceFilter(BeforeFilter{committed->date});
        } else {
            replaceFilter(AfterFilter{committed->date});
        }
    }
}

void OpenDashboardPage::drawFavoritesFilter() {
    imw::Font        font(LookAndFeel::instance().fontSmall());
    imw::ChangeStrId rowId("favoritesFilterRow");

    // the plus button is only enabled while pressing it would change the active filter
    if (drawPlusButton(hasActiveFavoritesFilter() != _pendingFavoritesFilter)) {
        // only one favourites filter can be active at a time
        replaceFilter(FavoriteFilter{_pendingFavoritesFilter});
    }
    drawRowLabel("Items that are");

    const auto favoritesFilterToString = [](FavoritesFilter filter) { return filter == FavoritesFilter::ShowFavorited ? "favorited" : "not favorited"; };

    ImGui::SetNextItemWidth(imw::calcComboSize("##favoritesFilterCombo", favoritesFilterToString(FavoritesFilter::ShowUnfavorited), ImGuiComboFlags_None).preferred.x);
    if (auto comboBox = ComboBox{"##favoritesFilterCombo", favoritesFilterToString(_pendingFavoritesFilter), ImGuiComboFlags_None}) {
        if (selectable(favoritesFilterToString(FavoritesFilter::ShowFavorited))) {
            _pendingFavoritesFilter = FavoritesFilter::ShowFavorited;
        }
        if (selectable(favoritesFilterToString(FavoritesFilter::ShowUnfavorited))) {
            _pendingFavoritesFilter = FavoritesFilter::ShowUnfavorited;
        }
    }
}

void OpenDashboardPage::drawTagFilter() {
    if (auto tag = _tagFilterRow.draw("tagFilterRow", "Tag: ")) {
        _filters.emplace_back(TagFilter{std::move(*tag)});
        forceRecalculateSortingAndFilteringOfListModel();
    }
}

void OpenDashboardPage::drawKeyValueFilter() {
    if (auto committed = _keyValueFilterRow.draw("keyValueFilterRow")) {
        _filters.emplace_back(KeyValueFilter{std::move(committed->key), std::move(committed->value)});
        forceRecalculateSortingAndFilteringOfListModel();
    }
}

void OpenDashboardPage::drawViewOptionsSection() {
    imw::Font font(LookAndFeel::instance().fontSmall());

    const auto drawModeSelectable = [this](const char* label, ListViewMode mode) {
        const ImVec2 padding   = ImGui::GetStyle().FramePadding * 2.f;
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        // use SelectableTextAlign to position text at a distance (padding) from the left edge, requires some math to convert to percentage of available space
        const float   leftoverWidth = std::max(1.f, ImGui::GetContentRegionAvail().x - labelSize.x);
        imw::StyleVar textAlign(ImGuiStyleVar_SelectableTextAlign, ImVec2{std::min(1.f, padding.x / leftoverWidth), 0.5f});
        if (selectable(label, _viewMode == mode, ImGuiSelectableFlags_None, ImVec2{0.f, labelSize.y + 2.f * padding.y}) && _viewMode != mode) {
            const bool customTreeChanged = (_viewMode == ListViewMode::CustomTree) != (mode == ListViewMode::CustomTree);
            _viewMode                    = mode;
            _scrollToSelected            = true;
            if (customTreeChanged) {
                forceRecalculateSortingAndFilteringOfTreeModel(); // the path function depends on the view mode
            }
        }
    };
    drawModeSelectable("Table", ListViewMode::Table);
    drawModeSelectable("File Tree", ListViewMode::FileTree);
    drawModeSelectable("Custom Tree", ListViewMode::CustomTree);
    if (_viewMode == ListViewMode::CustomTree) {
        drawCustomTreeKeyInputs();
    }
}

void OpenDashboardPage::drawCustomTreeKeyInputs() {
    ImGui::Indent();
    _customTreeKeyEditor.draw("##customTreeKeys");
    ImGui::Unindent();

    if (auto nonEmptyKeys = _customTreeKeyEditor.nonEmptyEntries(); nonEmptyKeys != _customTreeKeys) {
        _customTreeKeys = std::move(nonEmptyKeys);
        forceRecalculateSortingAndFilteringOfTreeModel();
    }
}

void OpenDashboardPage::drawActiveFilterTags() {
    const auto labels = _filters | std::views::transform(&DashboardFilter::label) | std::ranges::to<std::vector>();
    if (const auto removedIndex = components::drawFilterTags("##dashboardFilterTags", labels); removedIndex.has_value()) {
        _filters.erase(_filters.begin() + static_cast<std::ptrdiff_t>(*removedIndex));
        forceRecalculateSortingAndFilteringOfListModel();
    }
}

std::optional<FavoritesFilter> OpenDashboardPage::hasActiveFavoritesFilter() {
    for (const auto& filter : _filters) {
        if (const auto* favoriteFilter = std::get_if<FavoriteFilter>(&filter.data)) {
            return favoriteFilter->shown;
        }
    }
    return std::nullopt;
}

void OpenDashboardPage::forceRecalculateSortingAndFilteringOfListModel() {
    _sortFilterModel = components::SortFilterModel(makeSortFilterModelParams());
    forceRecalculateSortingAndFilteringOfTreeModel();
}

template<typename Kind>
void OpenDashboardPage::replaceFilter(Kind filter) {
    std::erase_if(_filters, [](const DashboardFilter& existing) { return std::holds_alternative<Kind>(existing.data); });
    _filters.push_back({std::move(filter)});
    forceRecalculateSortingAndFilteringOfListModel();
}

void OpenDashboardPage::setDashboardsSort(DashboardSort sort) {
    _sort = sort;
    forceRecalculateSortingAndFilteringOfListModel();
}

void OpenDashboardPage::drawSearchInput() {
    const bool  sortingBySearch = _sort.byRelevance;
    const bool  compact         = gAppState.compact;
    const float toggleSize      = [] {
        imw::Font font(LookAndFeel::instance().fontBig());
        return ImGui::GetFrameHeight();
    }();
    const auto event = _searchInput.draw("##dashboardSearch", "Search dashboards...", sortingBySearch, compact ? toggleSize + ImGui::GetStyle().ItemSpacing.x : 0.f);
    if (compact) {
        ImGui::SameLine();
        imw::StyleColor shown(ImGuiCol_Button, ImGui::GetStyleColorVec4(_filtersShown ? ImGuiCol_ButtonActive : ImGuiCol_Button));
        if (doIconButton("Filter and view options", kIconFilter, ImVec2{toggleSize, toggleSize})) {
            _filtersShown = !_filtersShown;
        }
    }
    switch (event) {
    case components::SearchSortInput::Event::none: break;
    case components::SearchSortInput::Event::wantsDefaultSort: setDashboardsSort({}); break;
    case components::SearchSortInput::Event::wantsRelevanceSort: setDashboardsSort({.byRelevance = true}); break;
    }
}

void OpenDashboardPage::drawSortByCombo(const char* rowLabel) {
    enum SortOptionIndex : std::size_t { mostRecentlyUsed, leastRecentlyUsed, byName, byNameReversed, bySource, bySourceReversed, byRelevance };
    const std::array<components::SortOption, 7> sortOptions{{
        {"by most recently used"},
        {"by least recently used"},
        {"alphabetically by name"},
        {"reverse alphabetically by name"},
        {"alphabetically by source URI"},
        {"reverse alphabetically by source URI"},
        {"by relevance of names to the search", !_searchInput.text.empty()},
    }};

    std::size_t currentIndex = mostRecentlyUsed;
    if (_sort.byRelevance) {
        currentIndex = byRelevance;
    } else {
        switch (_sort.kind) {
        case DashboardSortKind::lastUsed: currentIndex = _sort.reversed ? leastRecentlyUsed : mostRecentlyUsed; break;
        case DashboardSortKind::name: currentIndex = _sort.reversed ? byNameReversed : byName; break;
        case DashboardSortKind::source: currentIndex = _sort.reversed ? bySourceReversed : bySource; break;
        }
    }

    const auto chosen = components::drawSortByCombo("##sortByCombo", rowLabel, sortOptions, currentIndex);
    if (!chosen.has_value()) {
        return;
    }
    switch (*chosen) {
    case mostRecentlyUsed: setDashboardsSort({.kind = DashboardSortKind::lastUsed, .reversed = false}); break;
    case leastRecentlyUsed: setDashboardsSort({.kind = DashboardSortKind::lastUsed, .reversed = true}); break;
    case byName: setDashboardsSort({.kind = DashboardSortKind::name, .reversed = false}); break;
    case byNameReversed: setDashboardsSort({.kind = DashboardSortKind::name, .reversed = true}); break;
    case bySource: setDashboardsSort({.kind = DashboardSortKind::source, .reversed = false}); break;
    case bySourceReversed: setDashboardsSort({.kind = DashboardSortKind::source, .reversed = true}); break;
    case byRelevance: setDashboardsSort({.byRelevance = true}); break;
    }
}

OpenDashboardPage::ViewResult OpenDashboardPage::drawDashboardTable(const Dashboard* optionalDashboard, ImVec2 size) {
    _sortFilterModel.work();

    const auto shownItems  = _sortFilterModel.items();
    const auto dashboardAt = [this](const components::SortFilterModel::ScoredIndex& item) -> const std::shared_ptr<DashboardDescription>& { return dashboards()[item.second]; };

    ViewResult result;

    const float starButtonWidth = ImGui::GetFrameHeight();

    const float cellPaddingY  = ImGui::GetStyle().CellPadding.y;
    const float rowHeight     = std::max(ImGui::GetFrameHeight(), ImGui::GetTextLineHeightWithSpacing() * 3.f);
    const float previewHeight = rowHeight - 2.f * cellPaddingY;
    const float previewWidth  = previewHeight * DashboardPreview::kPreferredAspectRatio;

    const auto drawCenteredText = [rowHeight](const char* text) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextUnformatted(text);
    };

    const float elementHeight = rowHeight + 2.f * cellPaddingY;

    std::optional<std::size_t> scrollToRow;
    if (_scrollToTop) {
        scrollToRow = 0UZ;
    } else if (_scrollToSelected && _selectedDashboard != nullptr) {
        if (const auto selectedIt = std::ranges::find(shownItems, _selectedDashboard, dashboardAt); selectedIt != shownItems.end()) {
            scrollToRow = static_cast<std::size_t>(std::distance(shownItems.begin(), selectedIt));
        }
    }
    _scrollToTop      = false;
    _scrollToSelected = false;

    const bool                         withSource                  = !(gAppState.compact && gAppState.portrait);
    constexpr static auto              kColumnHeaderLabels         = std::to_array<std::string>({"", "", "Dashboard", "Source", kLastUsedText});
    constexpr static auto              kColumnHeaderLabelsNoSource = std::to_array<std::string>({"", "", "Dashboard", kLastUsedText});
    const std::array                   fixedColumnWidths{starButtonWidth + 2.f * ImGui::GetStyle().CellPadding.x, previewWidth + 2.f * ImGui::GetStyle().CellPadding.x, 0.f, 0.f, 0.f};
    const std::span<const std::string> columns = withSource ? std::span<const std::string>(kColumnHeaderLabels) : std::span<const std::string>(kColumnHeaderLabelsNoSource);

    constexpr std::size_t kNameColumn    = 2UZ;
    constexpr std::size_t kSourceColumn  = 3UZ;
    const std::size_t     lastUsedColumn = withSource ? 4UZ : 3UZ;

    using SortMarker = components::VirtualScrollTableParams::SortMarker;
    std::optional<SortMarker> sortMarker;
    if (!_sort.byRelevance) {
        const auto flipIfReversed = [reversed = _sort.reversed](ImGuiSortDirection direction) {
            if (!reversed) {
                return direction;
            }
            return direction == ImGuiSortDirection_Ascending ? ImGuiSortDirection_Descending : ImGuiSortDirection_Ascending;
        };
        switch (_sort.kind) {
        case DashboardSortKind::lastUsed: sortMarker = SortMarker{.columnIndex = lastUsedColumn, .direction = flipIfReversed(ImGuiSortDirection_Descending)}; break;
        case DashboardSortKind::name: sortMarker = SortMarker{.columnIndex = kNameColumn, .direction = flipIfReversed(ImGuiSortDirection_Ascending)}; break;
        case DashboardSortKind::source:
            if (withSource) {
                sortMarker = SortMarker{.columnIndex = kSourceColumn, .direction = flipIfReversed(ImGuiSortDirection_Ascending)};
            }
            break;
        }
    }

    components::VirtualScrollTable table(
        {
            .numElements       = shownItems.size(),
            .elementHeight     = elementHeight,
            .columns           = columns,
            .fixedColumnWidths = std::span<const float>(fixedColumnWidths).first(columns.size()),
            .scrollToElement   = scrollToRow,
            .columnHeaderFont  = LookAndFeel::instance().fontSmall(),
            .sortMarker        = sortMarker,
        },
        ImVec2{0.f, size.y});

    while (auto visibleRange = table.step()) {
        for (std::size_t row = visibleRange->first; row < visibleRange->second; ++row) {
            const auto& description = dashboardAt(shownItems[row]);

            imw::ChangeStrId outerId(description->storageInfo->path.c_str());
            imw::ChangeStrId innerId(description->name.c_str());

            table.beginRow();
            ImGui::TableNextColumn();

            const ImVec2 rowCursor     = ImGui::GetCursorPos();
            const float  starCellWidth = ImGui::GetContentRegionAvail().x;
            const bool   rowSelected   = _selectedDashboard == description;
            if (selectable("##dashboardRow", rowSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, {0.f, rowHeight})) {
                _selectedDashboard = description;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                result.dashboardToLoad = description;
            }

            if (optionalDashboard != nullptr && optionalDashboard->isInitialised && optionalDashboard->description && description->name == optionalDashboard->description->name && description->storageInfo == optionalDashboard->description->storageInfo) {
                const float rounding = 0.f; // selectable does not support rounded corners yet: rounded, the corners would show pixel artefacts when selected
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_HeaderActive), rounding, 0, LookAndFeel::instance().openDashboardPageStyle.loadedDashboardIndicatorBorderThickness);
            }

            ImGui::SetCursorPos({rowCursor.x + std::max(0.f, (starCellWidth - starButtonWidth) * 0.5f), rowCursor.y + std::max(0.f, (rowHeight - ImGui::GetFrameHeight()) * 0.5f)});
            if (drawFavoriteButton(description->isFavorite)) {
                description->isFavorite = !description->isFavorite;
                if (hasActiveFavoritesFilter()) {
                    result.dashboardAction = DashboardAction::favoriteChanged;
                }
            }
            imw::StyleColor rowText(ImGuiCol_Text, selectableTextColor(rowSelected)); // after the star button, which keeps the normal text colour on its own background

            ImGui::TableNextColumn();
            {
                const ImVec2 previewSize{previewWidth, previewHeight};
                const ImVec2 cellTop = ImGui::GetCursorScreenPos();
                const ImVec2 previewTop{cellTop.x + std::max(0.f, (ImGui::GetContentRegionAvail().x - previewSize.x) * 0.5f), cellTop.y + std::max(0.f, (rowHeight - previewSize.y) * 0.5f)};
                ImGui::SetCursorScreenPos(previewTop);

                // potentially IO: without list virtualisation, the first frame would request and parse every dashboard at once
                const DashboardPreview* preview = _previewCache.findOrStartLoading(description);
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
                    ImGui::GetWindowDrawList()->AddRectFilled(previewTop, {previewTop.x + previewSize.x, previewTop.y + previewSize.y}, ImGui::GetColorU32(ImGuiCol_FrameBg), LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewRounding);
                }
            }

            ImGui::TableNextColumn();
            drawCenteredText(description->name.c_str());

            if (withSource) {
                ImGui::TableNextColumn();
                drawCenteredText(description->storageInfo->path.c_str());
            }

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
    } else if (withSource && table.clickedColumn == kSourceColumn) {
        result.dashboardAction = DashboardAction::sortBySource;
    } else if (table.clickedColumn == lastUsedColumn) {
        result.dashboardAction = DashboardAction::sortByLastUsed;
    }

    return result;
}

void OpenDashboardPage::applyDashboardAction(DashboardAction change) {
    // clicking the column of the already active sort reverses its direction
    const auto sortByColumn = [this](DashboardSortKind kind) { setDashboardsSort({.kind = kind, .reversed = !_sort.byRelevance && _sort.kind == kind && !_sort.reversed}); };
    switch (change) {
    case DashboardAction::none: break;
    case DashboardAction::favoriteChanged: forceRecalculateSortingAndFilteringOfListModel(); break;
    case DashboardAction::sortByName: sortByColumn(DashboardSortKind::name); break;
    case DashboardAction::sortBySource: sortByColumn(DashboardSortKind::source); break;
    case DashboardAction::sortByLastUsed: sortByColumn(DashboardSortKind::lastUsed); break;
    }
}

OpenDashboardPage::ViewResult OpenDashboardPage::drawDashboardFileTree(ImVec2 size) {
    _sortFilterTreeModel.work();

    std::optional<std::size_t> selectedIndex;
    std::vector<std::string>   selectedPath;
    if (_scrollToSelected && _selectedDashboard != nullptr) {
        if (const auto selectedIt = std::ranges::find(dashboards(), _selectedDashboard); selectedIt != dashboards().end()) {
            selectedIndex = static_cast<std::size_t>(selectedIt - dashboards().begin());
            selectedPath  = _viewMode == ListViewMode::CustomTree ? customDashboardTreePath(*selectedIndex) : dashboardTreePath(*selectedIndex);
        }
    }

    ViewResult result;

    {
        imw::Child treeChild("##dashboardFileTree", size, 0, ImGuiWindowFlags_HorizontalScrollbar);
        if (_scrollToTop) {
            ImGui::SetScrollY(0.f);
        }

        // draw date, might be unreadable but the user can scroll horizontally to it
        const float dateColumnWidth      = ImGui::CalcTextSize("00/00/0000").x;
        const auto  drawRightAlignedDate = [dateColumnWidth](const char* dateText) {
            ImGui::SameLine();
            const float  visibleRightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - ImGui::GetStyle().ScrollbarSize - ImGui::GetStyle().WindowPadding.x;
            const ImVec2 cursor           = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2{std::max(cursor.x + ImGui::GetStyle().ItemSpacing.x, visibleRightEdge - dateColumnWidth), cursor.y});
            imw::StyleColor dimmedDate(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(dateText);
        };

        const auto drawLeafRow = [this, &result, selectedIndex, &drawRightAlignedDate](std::size_t itemIndex) {
            const auto&   description = dashboards()[itemIndex];
            imw::ChangeId leafId(static_cast<int>(itemIndex));

            if (drawFavoriteButton(description->isFavorite)) {
                description->isFavorite = !description->isFavorite;
                if (hasActiveFavoritesFilter()) {
                    result.dashboardAction = DashboardAction::favoriteChanged;
                }
            }
            ImGui::SameLine();

            const bool isSelected = _selectedDashboard == description;
            if (frameHeightSelectable(description->name.c_str(), isSelected)) {
                _selectedDashboard = description;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    result.dashboardToLoad = description;
                }
            }
            if (_scrollToSelected && selectedIndex == itemIndex) {
                ImGui::SetScrollHereY(0.5f);
            }

            if (description->lastUsed) {
                drawRightAlignedDate(formatDate(*description->lastUsed).c_str());
            } else {
                drawRightAlignedDate("never");
            }
        };

        const auto drawBranchNode = [this, &selectedPath](const components::SortFilterTreeModelNode& node) {
            if (_treeSetAllOpen.has_value()) {
                ImGui::SetNextItemOpen(*_treeSetAllOpen, ImGuiCond_Always);
            }
            if (node.path.size() + 1UZ < selectedPath.size()   //
                && node.name == selectedPath[node.path.size()] //
                && std::ranges::equal(node.path, std::span{selectedPath}.first(node.path.size()))) {
                // this is on the path to the selected node, so keep it open
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            return treeNodeEx(node.name.data(), ImGuiTreeNodeFlags_SpanAvailWidth);
        };

        const auto beginDraw = [&drawBranchNode, &drawLeafRow](const components::SortFilterTreeModelNode& node) {
            if (node.isBranch()) {
                return drawBranchNode(node);
            }
            drawLeafRow(*node.index);
            return false;
        };
        const auto endDraw = [](const components::SortFilterTreeModelNode&) { ImGui::TreePop(); };
        _sortFilterTreeModel.visitAllItemsDepthFirst(beginDraw, endDraw);
    }

    _scrollToTop      = false;
    _scrollToSelected = false;
    _treeSetAllOpen.reset();

    return result;
}

void OpenDashboardPage::drawMatchAnyOrAllFiltersCombo() {
    if (const auto matchAllFilters = components::drawMatchAnyOrAllFiltersCombo("##matchModeCombo", _requireAllFilters); matchAllFilters.has_value()) {
        _requireAllFilters = *matchAllFilters;
        forceRecalculateSortingAndFilteringOfListModel();
    }
}

void OpenDashboardPage::drawViewOverlayButtons(ImVec2 viewTopLeft, ImVec2 viewSize) {
    // overlay buttons are small, the default ImGui size with little padding, where the text nearly clips: some horizontal padding keeps them unobtrusive
    imw::StyleVar framePadding(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x * 2.f, ImGui::GetStyle().FramePadding.y});

    const float  frameHeight = ImGui::GetFrameHeight();
    const ImVec2 squareButtonSize{frameHeight, frameHeight};
    const float  spacing = ImGui::GetStyle().ItemSpacing.x;

    float overlayWidth = squareButtonSize.x;
    if (_viewMode != ListViewMode::Table) {
        overlayWidth += calcTextButtonSize("collapse all").x + calcTextButtonSize("expand all").x + 2.f * spacing;
    }

    const ImVec2 oldCursorScreenPosition = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2{viewTopLeft.x + viewSize.x - overlayWidth - ImGui::GetStyle().ScrollbarSize - spacing, viewTopLeft.y + viewSize.y - frameHeight - spacing});

    // with a background you can see text scrolling behind the buttons, which is kind of noisy looking
    imw::StyleColor overlayBg(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    {
        imw::Child overlay("##viewOverlayButtons", ImVec2{overlayWidth, frameHeight}, 0, ImGuiWindowFlags_NoScrollbar);
        if (_viewMode != ListViewMode::Table) {
            if (textButton("collapse all")) {
                _treeSetAllOpen = false;
            }
            ImGui::SameLine();
            if (textButton("expand all")) {
                _treeSetAllOpen = true;
            }
            ImGui::SameLine();
        }
        ImGui::PushFont(LookAndFeel::instance().fontIconsSolid, frameHeight / 2.f);
        imw::StyleVar noHorizontalPadding(ImGuiStyleVar_FramePadding, ImVec2{0.f, ImGui::GetStyle().FramePadding.y}); // the doubled padding is wider than the square leaves for the icon
        if (button(kIconArrowUp, squareButtonSize)) {
            _scrollToTop = true;
        }
        ImGui::PopFont();
    }

    ImGui::SetCursorScreenPos(oldCursorScreenPosition);
}

void OpenDashboardPage::drawOAuthPopup() {
#ifndef __EMSCRIPTEN__
    ImGui::SetNextWindowSize({LookAndFeel::instance().openDashboardPageStyle.oauthPopupWidth, 0.f}, ImGuiCond_Once);
    if (auto popup = imw::ModalPopup(kOauthPopupId, nullptr, 0)) {
        auto& session = OAuthSession::instance();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Scope:");
        ImGui::SameLine();
        ImGui::InputText("##scope", &_oauthScope);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Client ID:");
        ImGui::SameLine();
        ImGui::InputText("##clientid", &_oauthClientId);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Endpoint:");
        ImGui::SameLine();
        ImGui::InputText("##endpoint", &_oauthEndpoint);

        if (textButton("Sign in")) {
            session.signIn(_oauthScope, _oauthClientId, _oauthEndpoint);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Roles: %s", session.availableRoles().empty() ? "N/A" : session.availableRoles().c_str());

        ImGui::Separator();
        if (textButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
    }
#endif
}

void OpenDashboardPage::drawCurrentDashboardPanel(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    const float panelPadding     = LookAndFeel::instance().openDashboardPageStyle.currentDashboardPanelPadding;
    const float panelItemSpacing = LookAndFeel::instance().openDashboardPageStyle.currentDashboardPanelSpacing;

    const bool hasDashboardAndDescription = optionalDashboard && optionalDashboard->description;

    float titleHeight    = 0.f;
    float subtitleHeight = 0.f;
    {
        imw::Font titleFont(LookAndFeel::instance().fontBigger());
        titleHeight = ImGui::GetTextLineHeightWithSpacing();
    }
    {
        imw::Font subtitleFont(LookAndFeel::instance().fontBig());
        subtitleHeight = ImGui::GetTextLineHeightWithSpacing();
    }

    const char* title      = hasDashboardAndDescription ? optionalDashboard->description->name.c_str() : "No dashboard currently open";
    float       titleWidth = 0.f;
    {
        imw::Font titleFont(LookAndFeel::instance().fontBigger());
        titleWidth = ImGui::CalcTextSize(title).x;
    }

    const float  buttonSide    = LookAndFeel::instance().mainWindowIconButtonSize();
    const float  buttonsWidth  = 3.f * buttonSide + 2.f * panelItemSpacing;
    const ImVec2 panelStart    = ImGui::GetCursorScreenPos();
    const float  panelWidth    = ImGui::GetContentRegionAvail().x;
    const float  previewHeight = std::max(titleHeight + subtitleHeight, ImGui::GetFrameHeight() * 2.f);
    const float  previewWidth  = previewHeight * DashboardPreview::kPreferredAspectRatio;
    const bool   buttonsBelow  = panelPadding + previewWidth + panelItemSpacing + titleWidth + panelItemSpacing + buttonsWidth + panelPadding > panelWidth;
    const float  panelHeight   = previewHeight + 2.f * panelPadding + (buttonsBelow ? panelItemSpacing + buttonSide : 0.f);

    ImGui::GetWindowDrawList()->AddRectFilled(panelStart, panelStart + ImVec2{panelWidth, panelHeight}, ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().currentDashboardPanelBg), ImGui::GetStyle().ChildRounding);

    bool         previewDrawn = false;
    const ImVec2 previewStart = panelStart + ImVec2{panelPadding, panelPadding};
    const ImVec2 previewEnd   = previewStart + ImVec2{previewWidth, previewHeight};
    if (hasDashboardAndDescription) {
        if (const auto* preview = _previewCache.findOrStartLoading(optionalDashboard->description)) {
            preview->draw(previewStart, previewEnd);
            previewDrawn = true;
        }
    }
    if (!previewDrawn) {
        ImGui::GetWindowDrawList()->AddRectFilled(previewStart, previewEnd, ImGui::GetColorU32(ImGuiCol_FrameBg), LookAndFeel::instance().openDashboardPageStyle.dashboardPreviewRounding);
    }

    {
        const float  textBlockHeight = hasDashboardAndDescription ? titleHeight + subtitleHeight : titleHeight;
        const ImVec2 textStart{previewEnd.x + panelItemSpacing, previewStart.y + std::max(0.f, (previewHeight - textBlockHeight) / 2.f)};
        {
            imw::Font titleFont(LookAndFeel::instance().fontBigger());
            ImGui::SetCursorScreenPos(textStart);
            ImGui::TextUnformatted(title);
        }
        if (hasDashboardAndDescription) {
            imw::Font subtitleFont(LookAndFeel::instance().fontBig());
            ImGui::SetCursorScreenPos(textStart + ImVec2{0, titleHeight});
            ImGui::TextUnformatted("Currently open");
        }
    }

    const ImVec2 buttonSize{buttonSide, buttonSide};
    const ImVec2 buttonStart{panelStart.x + panelWidth - panelPadding - buttonsWidth, buttonsBelow ? previewEnd.y + panelItemSpacing : previewStart.y + 0.5f * (previewHeight - buttonSide)};

    {
        const bool dashboardLoaded = optionalDashboard != nullptr && optionalDashboard->isInitialised;

        imw::Disabled disabled(!dashboardLoaded);
        {
            imw::Disabled inMemoryDisabled(dashboardLoaded && optionalDashboard->description->storageInfo->isInMemoryDashboardStorage());
            ImGui::SetCursorScreenPos(buttonStart);
            if (doIconButton("Save", kIconSave, buttonSize) && dashboardLoaded) {
                saveWithLiveLayout(*optionalDashboard, optionalDashboardPage);
            }
        }

        ImGui::SetCursorScreenPos(buttonStart + ImVec2{buttonSize.x + panelItemSpacing, 0});
        if (doIconButton("Save as", kIconSaveAs, buttonSize)) {
            ImGui::OpenPopup("saveAsDialog");
        }

        ImGui::SetCursorScreenPos(buttonStart + ImVec2{(buttonSize.x + panelItemSpacing) * 2.f, 0});
        if (doIconButton("Close", kIconClose, buttonSize)) {
            requestCloseDashboard();
        }
    }

    drawSaveAsDialog(optionalDashboard, optionalDashboardPage);

    // we did everything else with custom draw so we have to register the actual space this took up with the layout
    ImGui::SetCursorScreenPos(panelStart);
    ImGui::Dummy(ImVec2{panelWidth, panelHeight});
}

void OpenDashboardPage::draw(Dashboard* optionalDashboard, DashboardPage* optionalDashboardPage) {
    const float leftPadding = ImGui::GetStyle().ItemSpacing.x;
    ImGui::Indent(leftPadding);
    drawCurrentDashboardPanel(optionalDashboard, optionalDashboardPage);
    ImGui::Spacing();

    const auto buttonHeight = LookAndFeel::instance().mainWindowIconButtonSize();

    drawSearchInput();
    ImGui::Spacing();

    const float sidebarRatio    = LookAndFeel::instance().openDashboardPageStyle.sidebarWidthRatio;
    const float sidebarMaxWidth = LookAndFeel::instance().openDashboardPageStyle.sidebarMaxWidth;

    const bool  compact          = gAppState.compact;
    const bool  compactPortrait  = compact && gAppState.portrait;
    const float sidebarWidth     = compact ? 0.f : std::max(components::DateFilterRow::rowWidth(kLastUsedText), std::min(ImGui::GetContentRegionAvail().x * sidebarRatio, sidebarMaxWidth));
    const float mainContentWidth = compact ? ImGui::GetContentRegionAvail().x : ImGui::GetContentRegionAvail().x - sidebarWidth - ImGui::GetStyle().ItemSpacing.x;

    if (compact && _filtersShown) {
        ImGui::SetNextWindowSizeConstraints(ImVec2{0.f, 0.f}, ImVec2{mainContentWidth, ImGui::GetContentRegionAvail().y * LookAndFeel::instance().openDashboardPageStyle.compactFiltersMaxHeightRatio});
        imw::Child filters("##dashboardFilters", ImVec2{mainContentWidth, 0.f}, ImGuiChildFlags_AutoResizeY, 0);
        drawFilterAndViewOptions();
    }

    std::shared_ptr<DashboardDescription> dashboardToLoad;

    {
        imw::Child mainContent("##dashboardMainContent", ImVec2{mainContentWidth, 0.f}, 0, 0);

        {
            imw::Font font(LookAndFeel::instance().fontSmall());

            drawActiveFilterTags();

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Show dashboards which match");
            ImGui::SameLine();
            drawMatchAnyOrAllFiltersCombo();
            if (!compactPortrait) {
                ImGui::SameLine();
            }
            drawSortByCombo(compactPortrait ? "Sort dashboards " : "           Sort dashboards ");
        }
        ImGui::Spacing();

        const float  tableHeight = ImGui::GetContentRegionAvail().y - (buttonHeight + ImGui::GetStyle().ItemSpacing.y);
        const ImVec2 viewSize{mainContentWidth, tableHeight};
        const ImVec2 viewTopLeft = ImGui::GetCursorScreenPos();
        ViewResult   viewResult;
        switch (_viewMode) {
        case ListViewMode::Table: viewResult = drawDashboardTable(optionalDashboard, viewSize); break;
        case ListViewMode::FileTree:
        case ListViewMode::CustomTree: viewResult = drawDashboardFileTree(viewSize); break;
        }
        // okay, iteration and drawing is done, it is safe to change the models
        applyDashboardAction(viewResult.dashboardAction);
        dashboardToLoad = std::move(viewResult.dashboardToLoad);

        if (_viewMode == ListViewMode::Table ? !_sortFilterModel.isComplete() : !_sortFilterTreeModel.isComplete()) {
            const float progress                = _viewMode == ListViewMode::Table ? _sortFilterModel.progress() : _sortFilterTreeModel.progress();
            const float progressBarHeight       = ImGui::GetFrameHeight();
            const auto  oldCursorScreenPosition = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(viewTopLeft + ImVec2{0, viewSize.y - progressBarHeight});
            ImGui::ProgressBar(progress, ImVec2{viewSize.x, progressBarHeight});
            ImGui::SetCursorScreenPos(oldCursorScreenPosition);
        }

        drawViewOverlayButtons(viewTopLeft, viewSize);

        const ImVec2 buttonSize{buttonHeight, buttonHeight};
        {
            imw::Disabled disabled(_selectedDashboard == nullptr);
            if (doIconButton("Load", kIconLoad, buttonSize) && _selectedDashboard != nullptr) {
                // it is okay to override dashboardToLoad because there is only one interaction per frame
                dashboardToLoad = _selectedDashboard;
            }
        }
        ImGui::SameLine();

        float openButtonsTotalWidth = 2.f * buttonSize.x + ImGui::GetStyle().ItemSpacing.x;
#ifndef __EMSCRIPTEN__
        constexpr const char* kOpenOAuthPopupLabel = "OAuth/RBAC";
        if (gAppState.prototypeMode) {
            openButtonsTotalWidth += ImGui::GetStyle().ItemSpacing.x + calcTextButtonSize(kOpenOAuthPopupLabel).x;
        }
#endif
        ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2{std::max(0.f, ImGui::GetContentRegionAvail().x - openButtonsTotalWidth), 0});

        if (doIconButton("Open empty dashboard", kIconEmptyDashboard, buttonSize)) {
            requestLoadDashboard(nullptr);
        }
        ImGui::SameLine();
        doIconButton("Open a new Digitizer Window", kIconNewWindow, buttonSize);

#ifndef __EMSCRIPTEN__
        if (gAppState.prototypeMode) {
            ImGui::SameLine();
            if (textButton(kOpenOAuthPopupLabel, ImVec2{0.f, buttonHeight})) {
                ImGui::OpenPopup(kOauthPopupId);
            }
        }
        drawOAuthPopup();
#endif
    }

    if (!compact) {
        ImGui::SameLine();
        imw::Child sidebar("##dashboardSidebar", ImVec2{0.f, 0.f}, 0, 0);
        drawFilterAndViewOptions();
    }

    ImGui::Unindent(leftPadding);

    if (dashboardToLoad) {
        requestLoadDashboard(dashboardToLoad);
    }
}

void OpenDashboardPage::drawFilterAndViewOptions() {
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

void OpenDashboardPage::drawAddSourcePopup() {
    ImGui::SetNextWindowSize({LookAndFeel::instance().openDashboardPageStyle.addSourcePopupWidth, ImGui::GetFrameHeight() * 4.F}, ImGuiCond_Once); // 4 line heights: window top bar, path entry, buttons, and some extra spacing
    if (auto popup = imw::ModalPopup(kAddSourcePopupId, nullptr, 0)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Path:");
        ImGui::SameLine();
        std::string& path = _addSourcePath;
        if (ImGui::IsWindowAppearing()) {
            path = {};
        }
        ImGui::InputText("##sourcePath", &path);

#ifdef __EMSCRIPTEN__
        // on emscripten we cannot use local sources
        const bool okEnabled = isRemotePath(path);
#else
        const bool okEnabled = !path.empty();
#endif
        if (components::dialogButtons(okEnabled) == components::DialogButton::Ok) {
            addDashboard(path);
        }
    }
}

std::shared_ptr<DashboardDescription> OpenDashboardPage::get(const size_t index) {
    if (dashboards().size() > index) {
        return {dashboards().at(index)};
    }
    return {};
}

} // namespace gr::od
