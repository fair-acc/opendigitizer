#include "SignalSelector.hpp"

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Message.hpp>

#include "../blocks/RemoteSource.hpp"
#include "FAIR/DeviceNameHelper.hpp"
#include "StyledStandardWidgets.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <optional>

using namespace std::string_literals;

namespace DigitizerUi {

void QueryFilters::draw() {
    std::optional<std::size_t> removed;
    for (std::size_t index = 0UZ; index < elements.size(); ++index) {
        QueryFilter& filter = elements[index];
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 3);
        if (auto combo = DigitizerUi::ComboBox(filter.keyId.c_str(), QueryFilter::fieldNames[filter.selectedIndex], 0)) {
            for (std::size_t i = 0; i < QueryFilter::fieldNames.size(); i++) {
                const bool isSelected = filter.selectedIndex == i;
                if (DigitizerUi::Selectable(QueryFilter::fieldNames[i], isSelected)) {
                    if (std::ranges::any_of(elements, [&i, &filter](const QueryFilter& other) { return other.keyId != filter.keyId && other.selectedIndex == i; })) {
                        if (auto popup = IMW::ModalPopup("Wrong Entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::Text("Key already selected. Please select a different one");
                            if (DigitizerUi::TextButton("Ok")) {
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
        if (DigitizerUi::Button(filter.deleteButtonId.c_str())) {
            removed = index;
        }
    }
    if (removed) {
        elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed();
    }
}

void SignalList::update() {
    opencmw::service::dns::QueryEntry queryEntry;
    refl::util::for_each(refl::reflect(queryEntry).members, [&](auto member) {
        auto it = std::ranges::find_if(filters.elements, [&member](const QueryFilter& f) { return f.selectedField() == refl::descriptor::get_display_name(member); });
        if (it != filters.elements.end() && !it->filterText.empty()) {
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
        std::unique_lock l{signalsMutex};
        dnsClient.querySignalsAsync([this](const std::vector<opencmw::service::dns::Entry>& entries) {
            signals = entries;
            if (updateSignalsCallback) {
                updateSignalsCallback(signals);
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "Error loading signals: " << e.what() << std::endl;
    }
}

SignalSelector::SignalSelector(UiGraphModel& graphModel) {
    m_graphModel = std::addressof(graphModel);

    m_signalList.updateSignalsCallback = [&](const std::vector<opencmw::service::dns::Entry>& signals) {
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
                    m_signals.push_back(internal);
                }

                for (auto i = 0; i < 8000; ++i) {
                    // simulate a lot of devices for the UI
                    SignalData dup  = sig;
                    dup.signalName  = "TEST signal" + std::to_string(i);
                    dup.accelerator = "TEST accelerator" + std::to_string(i % 10);
                    m_signals.push_back(dup);
                }
            }

            m_signals.push_back(sig);
        }

        m_forceRefresh = true;
        buildIndex();
    };

    buildIndex();
    m_signalList.update();
}

void SignalSelector::drawElement(const SignalData& entry, std::size_t idx, const ImGuiSelectionBasicStorage& selection) {
    IMW::ChangeId rowId{int(idx)};

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool itemSelected = selection.Contains(static_cast<ImGuiID>(idx));
    ImGui::SetNextItemSelectionUserData(static_cast<ImGuiSelectionUserData>(idx));
    DigitizerUi::Selectable(entry.device.c_str(), itemSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
    IMW::StyleColor rowText(ImGuiCol_Text, SelectableTextColor(itemSelected));
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

void SignalSelector::startSearching(const std::string& _searchString, std::vector<FilterTag*> _filters) {
    m_filteredItems.clear();
    m_searchString     = _searchString;
    m_nextItemToFilter = 0UZ;

    if (!_filters.empty()) {
        m_categorySearch          = CategorySearch{};
        m_categorySearch->filters = std::move(_filters);

        std::ranges::sort(m_categorySearch->filters, [](const auto* left, const auto* right) { return *left < *right; });
        auto mainCategory      = m_categorySearch->filters.front()->category;
        auto mainCategoryIndex = categoryIndices[mainCategory];

        for (const auto& filter : m_categorySearch->filters) {
            if (filter->category != mainCategory) {
                break;
            }
            m_categorySearch->signalsToProcess.push_back((*mainCategoryIndex)[filter->title]);
        }
    } else {
        m_categorySearch.reset();
    }
}

bool SignalSelector::signalMatchesSearchString(const SignalData& signal) const {
    return signal.signalName.find(m_searchString) != std::string::npos || //
           signal.comment.find(m_searchString) != std::string::npos;
}

bool SignalSelector::signalMatchesActiveFilters(const SignalData& signal) const {
    assert(m_categorySearch.has_value());
    enum MatchState { Unspecified = 0, NoMatches = 1, HasMatches = 2 };
    std::array<MatchState, CategoriesCount> stateMatches{};
    std::ranges::fill(stateMatches, Unspecified);

    for (const auto filter : m_categorySearch->filters) {
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
    m_querySignalFilters.draw();

    bool filtersChanged = m_forceRefresh;
    m_forceRefresh      = false;

    if (FilterTag* item = m_filterCombos.draw()) {
        bool wasActive = item->isActive;
        item->isActive = !wasActive;

        if (wasActive) {
            m_selectedFilters.removeLabel(item);
        } else {
            m_selectedFilters.labels.push_back({item->title, item, colorForCategory(item->category)});
        }
        filtersChanged = true;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    ImGui::InputText("##textFilter", &m_shownSearchString);
    if (m_shownSearchString != m_searchString) {
        filtersChanged = true;
    }

    if (FilterTag* removedLabel = m_selectedFilters.draw()) {
        removedLabel->isActive = false;
        filtersChanged         = true;
    }

    if (filtersChanged) {
        auto filters = m_selectedFilters.labels | std::views::transform([](const auto& filter) { return filter.data; }) | std::ranges::to<std::vector>();
        startSearching(m_shownSearchString, std::move(filters));
        while (loadMoreItems()) {
        }
    }
    ImGui::Separator();
    ImGui::SetNextWindowSize(ImGui::GetContentRegionAvail(), ImGuiCond_Once);
    static ImGuiSelectionBasicStorage selection;
    {
        IMW::Child signals("Signals", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), 0, 0);
        if (auto table = DigitizerUi::IMW::Table("Signals", 6, static_cast<ImGuiTableFlags>(ImGuiTableFlags_BordersInnerV), ImVec2(0.0f, 0.0f), 0.0f)) {
            // allow multi-selection
            ImGuiMultiSelectFlags flags = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect1d | ImGuiMultiSelectFlags_ClearOnClickVoid;
            auto*                 ms_io = ImGui::BeginMultiSelect(flags, selection.Size, static_cast<int>(m_filteredItems.size()));
            selection.ApplyRequests(ms_io);

            ImGui::TableHeader("SignalsHeader");
            ImGui::TableSetupColumn("Device");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Quantity");
            ImGui::TableSetupColumn("Unit");
            ImGui::TableSetupColumn("DAQ-M");
            ImGui::TableSetupColumn("Comment");
            ImGui::TableHeadersRow();

            // clip list for better performance
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_filteredItems.size()));
            if (ms_io->RangeSrcItem != -1) {
                clipper.IncludeItemByIndex(static_cast<int>(ms_io->RangeSrcItem)); // do not clip RangeSrcItem
            }

            while (clipper.Step()) {
                for (std::size_t n = static_cast<std::size_t>(clipper.DisplayStart); n < static_cast<std::size_t>(clipper.DisplayEnd); ++n) {
                    drawElement(*m_filteredItems[n], n, selection);
                }
            }

            ms_io = ImGui::EndMultiSelect();
            selection.ApplyRequests(ms_io);
        }
    }

    if (DigitizerUi::TextButton("Add Signal")) {
        std::vector<SignalData> selectedEntries;
        void*                   it = nullptr;
        ImGuiID                 id = 0;
        while (selection.GetNextSelectedItem(&it, &id)) {
            selectedEntries.push_back(*m_filteredItems[id]);
        }

        close();
        return selectedEntries;
    }
    ImGui::SameLine();
    if (DigitizerUi::TextButton("Refresh")) {
        m_signalList.update();
    }

    return {};
}

std::vector<SignalData> SignalSelector::drawAndReturnSelected() {
    auto parentSize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(parentSize - ImVec2(32, 32), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Once);
    auto x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + 32);

    if (auto menu = IMW::ModalPopup(m_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        float windowWidth = ImGui::GetWindowWidth();
        float buttonPosX  = windowWidth - 2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Close").x;
        ImGui::SetCursorPosX(buttonPosX);
        if (DigitizerUi::TextButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
            return {};
        }

        return drawSignalSelector();
    }

    return {};
}

void SignalSelector::buildIndex() {
    m_nextItemToFilter = 0UZ;
    m_searchString.clear();
    m_filteredItems.clear();
    m_categorySearch.reset();

    for (SignalData& signal : m_signals) {
        auto* signalPtr = std::addressof(signal);
        deviceIndex[signal.device].push_back(signalPtr);
        frontendIndex[signal.frontend].push_back(signalPtr);
        signalNameIndex[signal.signalName].push_back(signalPtr);
        subDevicePropertyIndex[signal.subDeviceProperty].push_back(signalPtr);
        quantityIndex[signal.quantity].push_back(signalPtr);
        sampleRateIndex[signal.sampleRate].push_back(signalPtr);
        unitIndex[signal.unit].push_back(signalPtr);
        acceleratorIndex[signal.accelerator].push_back(signalPtr);
        deviceClassIndex[signal.deviceClass].push_back(signalPtr);
    }

    auto itemsForIndex = [this](Category category) {
        const auto&            index = *categoryIndices[+category];
        std::vector<FilterTag> result;
        result.resize(index.size());
        std::ranges::transform(index, result.begin(), [category](const auto& kvp) { return FilterTag{+category, kvp.first, false}; });
        return result;
    };

    m_filterCombos.combos = {
        {"##comboDomain", "Domain", colorForCategory(+Category::Domain), itemsForIndex(Category::Domain)},                //
        {"##comboDeviceType", "Dev. type", colorForCategory(+Category::DeviceType), itemsForIndex(Category::DeviceType)}, //
        {"##comboDAQ", "DAQ-M.", colorForCategory(+Category::DAQ_M), {}},                                                 //
        {"##comboStatus", "Status", colorForCategory(+Category::Status), {}},                                             //
        {"##comboQuantity", "Quantity", colorForCategory(+Category::Quantity), itemsForIndex(Category::Quantity)}         //
    };
}

bool SignalSelector::loadMoreItems() {
    if (m_categorySearch) {
        if (m_categorySearch->signalsToProcess.empty()) {
            return false;
        }

        const auto& currentList = m_categorySearch->signalsToProcess.front();
        if (m_nextItemToFilter >= currentList.size()) {
            // Current list done, removing it and moving to the next one
            m_categorySearch->signalsToProcess.pop_front();
            m_nextItemToFilter = 0UZ;
            return true;
        }

        const auto& signal = *currentList[m_nextItemToFilter];
        if (signalMatchesSearchString(signal) && signalMatchesActiveFilters(signal)) {
            m_filteredItems.push_back(currentList[m_nextItemToFilter]);
        }

        m_nextItemToFilter++;
        return true;

    } else if (!m_searchString.empty()) {
        // We list everything that matches the search string,
        // no extra filters have been defined
        if (m_nextItemToFilter >= m_signals.size()) {
            return false;
        }

        if (signalMatchesSearchString(m_signals[m_nextItemToFilter])) {
            m_filteredItems.push_back(&m_signals[m_nextItemToFilter]);
        }

        m_nextItemToFilter++;
        return true;
    } else {
        // just show everything, no need for loadMoreItems()
        for (size_t i = 0; i < m_signals.size(); ++i) {
            m_filteredItems.push_back(&m_signals[i]);
        }
        return false;
    }
}

} // namespace DigitizerUi
