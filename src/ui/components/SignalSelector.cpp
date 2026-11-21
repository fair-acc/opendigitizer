#include "SignalSelector.hpp"

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Message.hpp>

#include "../blocks/RemoteSource.hpp"
#include "FAIR/DeviceNameHelper.hpp"
#include "StyledWidgets.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <optional>

using namespace std::string_literals;

namespace gr::od {

void QueryFilters::draw() {
    std::optional<std::size_t> removed;
    for (std::size_t index = 0UZ; index < elements.size(); ++index) {
        QueryFilter& filter = elements[index];
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 3);
        if (auto combo = ComboBox(filter.keyId.c_str(), QueryFilter::kFieldNames[filter.selectedIndex], 0)) {
            for (std::size_t i = 0; i < QueryFilter::kFieldNames.size(); i++) {
                const bool isSelected = filter.selectedIndex == i;
                if (selectable(QueryFilter::kFieldNames[i], isSelected)) {
                    if (std::ranges::any_of(elements, [&i, &filter](const QueryFilter& other) { return other.keyId != filter.keyId && other.selectedIndex == i; })) {
                        if (auto popup = imw::ModalPopup("Wrong Entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::Text("Key already selected. Please select a different one");
                            if (textButton("Ok")) {
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    } else {
                        filter.selectedIndex = i;
                        changed();
                    }
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
        if (ImGui::InputText(filter.valueId.c_str(), &filter.filterText)) {
            changed();
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() - ImGui::GetStyle().FramePadding.x * 2);
        if (button(filter.deleteButtonId.c_str())) {
            removed = index;
        }
    }
    if (removed.has_value()) {
        elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed();
    }
}

void SignalList::update() {
    opencmw::service::dns::QueryEntry queryEntry;
    ::refl::util::for_each(::refl::reflect(queryEntry).members, [&](auto member) {
        auto it = std::ranges::find_if(_filters.elements, [&member](const QueryFilter& f) { return f.selectedField() == ::refl::descriptor::get_display_name(member); });
        if (it != _filters.elements.end() && !it->filterText.empty()) {
            using TField         = std::remove_cvref_t<decltype(member(queryEntry))>;
            const auto& strValue = it->filterText;
            if constexpr (std::is_integral_v<TField>) {
                TField val{};
                std::from_chars(strValue.data(), strValue.data() + strValue.size(), val);
                member(queryEntry) = val;
            } else if constexpr (std::is_floating_point_v<TField>) {
                member(queryEntry) = static_cast<TField>(std::strtod(strValue.c_str(), nullptr));
            } else {
                member(queryEntry) = strValue;
            }
        }
    });
    try {
        std::unique_lock l{_signalsMutex};
        _dnsClient.querySignalsAsync([this](const std::vector<opencmw::service::dns::Entry>& entries) {
            _signals = entries;
            if (updateSignalsCallback) {
                updateSignalsCallback(_signals);
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "Error loading signals: " << e.what() << std::endl;
    }
}

SignalSelector::SignalSelector(UiGraphModel& graphModel) {
    _graphModel = std::addressof(graphModel);

    _signalList.updateSignalsCallback = [&](const std::vector<opencmw::service::dns::Entry>& signals) {
        for (const auto& s : signals) {
            SignalData sig;
            sig.signalName  = s.signal_name;
            sig.serviceName = s.service_name;
            sig.protocol    = s.protocol;
            sig.port        = s.port;
            sig.hostname    = s.hostname;
            sig.unit        = s.signal_unit;
            sig.sampleRate  = std::to_string(s.signal_rate);

            if (::getenv("OPENDIGITIZER_LOAD_TEST_SIGNALS")) {
                const auto info       = fair::getDeviceInfo(s.signal_name);
                sig.device            = s.signal_name;
                sig.deviceClass       = "TEST deviceClass";
                sig.subDeviceProperty = "TEST subdevice";
                sig.accelerator       = "TEST accelerator";
                sig.comment           = info.deviceFunction;
                sig.device            = info.name;
                sig.frontend          = "TEST frontend";
                sig.quantity          = "1";

                {
                    SignalData internal = sig;
                    internal.signalName = "test";
                    internal.device     = "internal " + internal.device;
                    _signals.push_back(internal);
                }

                for (auto i = 0; i < 8000; ++i) {
                    // simulate a lot of devices for the UI
                    SignalData dup  = sig;
                    dup.signalName  = "TEST signal" + std::to_string(i);
                    dup.accelerator = "TEST accelerator" + std::to_string(i % 10);
                    _signals.push_back(dup);
                }
            }

            _signals.push_back(sig);
        }

        _forceRefresh = true;
        buildIndex();
    };

    buildIndex();
    _signalList.update();
}

void SignalSelector::drawElement(const SignalData& entry, std::size_t idx, const ImGuiSelectionBasicStorage& selection) {
    imw::ChangeId rowId{int(idx)};

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool itemSelected = selection.Contains(static_cast<ImGuiID>(idx));
    ImGui::SetNextItemSelectionUserData(static_cast<ImGuiSelectionUserData>(idx));
    selectable(entry.device.c_str(), itemSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
    imw::StyleColor rowText(ImGuiCol_Text, selectableTextColor(itemSelected));
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.signalName.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.quantity.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.unit.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.frontend.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(entry.comment.c_str());
}

void SignalSelector::startSearching(const std::string& searchString, std::vector<FilterTag*> filters) {
    _filteredItems.clear();
    _searchString     = searchString;
    _nextItemToFilter = 0UZ;

    if (!filters.empty()) {
        _categorySearch          = CategorySearch{};
        _categorySearch->filters = std::move(filters);

        std::ranges::sort(_categorySearch->filters, [](const auto* left, const auto* right) { return *left < *right; });
        auto mainCategory      = _categorySearch->filters.front()->category;
        auto mainCategoryIndex = _categoryIndices[mainCategory];

        for (const auto& filter : _categorySearch->filters) {
            if (filter->category != mainCategory) {
                break;
            }
            _categorySearch->signalsToProcess.push_back((*mainCategoryIndex)[filter->title]);
        }
    } else {
        _categorySearch.reset();
    }
}

bool SignalSelector::signalMatchesSearchString(const SignalData& signal) const {
    return signal.signalName.contains(_searchString) || //
           signal.comment.contains(_searchString);
}

bool SignalSelector::signalMatchesActiveFilters(const SignalData& signal) const {
    assert(_categorySearch.has_value());
    enum MatchState { Unspecified = 0, NoMatches = 1, HasMatches = 2 };
    std::array<MatchState, kCategoriesCount> stateMatches{};
    std::ranges::fill(stateMatches, Unspecified);

    for (const auto filter : _categorySearch->filters) {
        if (stateMatches[filter->category] == Unspecified) {
            stateMatches[filter->category] = NoMatches;
        }

        if (stateMatches[filter->category] == NoMatches) {
            if (filter->title == categoryFieldForSignal(filter->category, signal)) {
                stateMatches[filter->category] = HasMatches;
            }
        }
    }

    return std::ranges::find(stateMatches, NoMatches) == stateMatches.end();
}

std::vector<SignalData> SignalSelector::drawSignalSelector() {
    _querySignalFilters.draw();

    bool filtersChanged = _forceRefresh;
    _forceRefresh       = false;

    if (FilterTag* item = _filterCombos.draw()) {
        bool wasActive = item->isActive;
        item->isActive = !wasActive;

        if (wasActive) {
            _selectedFilters.removeLabel(item);
        } else {
            _selectedFilters.labels.emplace_back(item->title, item, colorForCategory(item->category));
        }
        filtersChanged = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    ImGui::InputText("##textFilter", &_shownSearchString);
    if (_shownSearchString != _searchString) {
        filtersChanged = true;
    }

    if (FilterTag* removedLabel = _selectedFilters.draw()) {
        removedLabel->isActive = false;
        filtersChanged         = true;
    }

    if (filtersChanged) {
        auto filters = _selectedFilters.labels | std::views::transform([](const auto& filter) { return filter.data; }) | std::ranges::to<std::vector>();
        startSearching(_shownSearchString, std::move(filters));
        while (loadMoreItems()) {
        }
    }
    ImGui::Separator();
    ImGui::SetNextWindowSize(ImGui::GetContentRegionAvail(), ImGuiCond_Once);
    static ImGuiSelectionBasicStorage selection;
    {
        imw::Child signals("Signals", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), 0, 0);
        if (auto table = imw::Table("Signals", 6, static_cast<ImGuiTableFlags>(ImGuiTableFlags_BordersInnerV), ImVec2(0.0f, 0.0f), 0.0f)) {
            ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d | ImGuiMultiSelectFlags_ClearOnClickVoid;
            auto*                 io    = ImGui::BeginMultiSelect(flags, selection.Size, static_cast<int>(_filteredItems.size()));
            selection.ApplyRequests(io);

            ImGui::TableHeader("SignalsHeader");
            ImGui::TableSetupColumn("Device");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Quantity");
            ImGui::TableSetupColumn("Unit");
            ImGui::TableSetupColumn("DAQ-M");
            ImGui::TableSetupColumn("Comment");
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(_filteredItems.size()));
            if (io->RangeSrcItem != -1) {
                clipper.IncludeItemByIndex(static_cast<int>(io->RangeSrcItem)); // do not clip RangeSrcItem
            }

            while (clipper.Step()) {
                for (std::size_t n = static_cast<std::size_t>(clipper.DisplayStart); n < static_cast<std::size_t>(clipper.DisplayEnd); ++n) {
                    drawElement(*_filteredItems[n], n, selection);
                }
            }

            io = ImGui::EndMultiSelect();
            selection.ApplyRequests(io);
        }
    }

    if (textButton("Add Signal")) {
        std::vector<SignalData> selectedEntries;
        void*                   it = nullptr;
        ImGuiID                 id = 0;
        while (selection.GetNextSelectedItem(&it, &id)) {
            selectedEntries.push_back(*_filteredItems[id]);
        }

        close();
        return selectedEntries;
    }
    ImGui::SameLine();
    if (textButton("Refresh")) {
        _signalList.update();
    }

    return {};
}

std::vector<SignalData> SignalSelector::drawAndReturnSelected() {
    auto parentSize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(parentSize - ImVec2(32, 32), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
    auto x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + 32);

    if (auto menu = imw::ModalPopup(_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        float windowWidth = ImGui::GetWindowWidth();
        float buttonPosX  = windowWidth - 2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Close").x;
        ImGui::SetCursorPosX(buttonPosX);
        if (textButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            return {};
        }

        return drawSignalSelector();
    }

    return {};
}

void SignalSelector::buildIndex() {
    _nextItemToFilter = 0UZ;
    _searchString.clear();
    _filteredItems.clear();
    _categorySearch.reset();

    for (SignalData& signal : _signals) {
        auto* signalPtr = std::addressof(signal);
        _deviceIndex[signal.device].push_back(signalPtr);
        _frontendIndex[signal.frontend].push_back(signalPtr);
        _signalNameIndex[signal.signalName].push_back(signalPtr);
        _subDevicePropertyIndex[signal.subDeviceProperty].push_back(signalPtr);
        _quantityIndex[signal.quantity].push_back(signalPtr);
        _sampleRateIndex[signal.sampleRate].push_back(signalPtr);
        _unitIndex[signal.unit].push_back(signalPtr);
        _acceleratorIndex[signal.accelerator].push_back(signalPtr);
        _deviceClassIndex[signal.deviceClass].push_back(signalPtr);
    }

    auto itemsForIndex = [this](Category category) {
        const auto&            index = *_categoryIndices[+category];
        std::vector<FilterTag> result;
        result.resize(index.size());
        std::ranges::transform(index, result.begin(), [category](const auto& kvp) { return FilterTag{+category, kvp.first, false}; });
        return result;
    };

    _filterCombos.combos = {
        {"##comboDomain", "Domain", colorForCategory(+Category::Domain), itemsForIndex(Category::Domain)},                //
        {"##comboDeviceType", "Dev. type", colorForCategory(+Category::DeviceType), itemsForIndex(Category::DeviceType)}, //
        {"##comboDAQ", "DAQ-M.", colorForCategory(+Category::DaqM), {}},                                                  //
        {"##comboStatus", "Status", colorForCategory(+Category::Status), {}},                                             //
        {"##comboQuantity", "Quantity", colorForCategory(+Category::Quantity), itemsForIndex(Category::Quantity)}         //
    };
}

bool SignalSelector::loadMoreItems() {
    if (_categorySearch) {
        if (_categorySearch->signalsToProcess.empty()) {
            return false;
        }

        const auto& currentList = _categorySearch->signalsToProcess.front();
        if (_nextItemToFilter >= currentList.size()) {
            _categorySearch->signalsToProcess.pop_front();
            _nextItemToFilter = 0UZ;
            return true;
        }

        const auto& signal = *currentList[_nextItemToFilter];
        if (signalMatchesSearchString(signal) && signalMatchesActiveFilters(signal)) {
            _filteredItems.push_back(currentList[_nextItemToFilter]);
        }

        _nextItemToFilter++;
        return true;

    } else if (!_searchString.empty()) {
        if (_nextItemToFilter >= _signals.size()) {
            return false;
        }

        if (signalMatchesSearchString(_signals[_nextItemToFilter])) {
            _filteredItems.push_back(&_signals[_nextItemToFilter]);
        }

        _nextItemToFilter++;
        return true;
    } else {
        for (size_t i = 0; i < _signals.size(); ++i) {
            _filteredItems.push_back(&_signals[i]);
        }
        return false;
    }
}

} // namespace gr::od
