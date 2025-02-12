#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include "Dialog.hpp"
#include "FAIR/DeviceNameHelper.hpp"
#include "FilterComboBoxes.hpp"
#include "SelectedLabelsView.hpp"

#include "../Flowgraph.hpp"
#include "../RemoteSignalSources.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace DigitizerUi {

class SignalSelector {
private:
    std::string                 m_windowName = "Add Device Signals";
    QueryFilterElementList      m_querySignalFilters;
    SignalList                  m_signalList{m_querySignalFilters};
    std::function<void(Block*)> m_addSignalCallback;

    enum class Category { Domain = 0, DeviceType = 1, DAQ_M = 2, Status = 3, Quantity = 4 };
    static constexpr std::size_t CategoriesCount = 5;
    friend constexpr std::size_t operator+(Category category) { return static_cast<std::size_t>(category); }

    struct FilterData {
        Category    category;
        std::string title;
        bool        isActive;

        auto operator<=>(const FilterData& other) const = default;
    };

    struct CategoryData {
        std::string             id;
        std::string             label;
        std::array<ImColor, 2>  color;
        std::vector<FilterData> items;
    };

    SelectedLabelsView<FilterData*> m_selectedFilters;
    FilterComboBoxes<CategoryData>  m_filterCombos;

    bool        m_forceRefresh                = false;
    bool        m_addRemoteSignal             = false;
    bool        m_addRemoteSignalDialogOpened = false;
    std::string m_addRemoteSignalUri;

    opencmw::service::dns::QueryEntry m_queryFilter;

    static constexpr std::array             colorsForLight = {ImColor(163, 217, 255), ImColor(189, 146, 221), ImColor(229, 99, 153), ImColor(238, 207, 109), ImColor(44, 165, 141)};
    static constexpr std::array             colorsForDark  = {ImColor(164, 130, 19), ImColor(0, 107, 184), ImColor(98, 44, 140), ImColor(157, 27, 81), ImColor(39, 145, 124)};
    static constexpr std::array<ImColor, 2> colorForCategory(Category category) {
        return {
            //
            colorsForLight[+category], //
            colorsForDark[+category]   //
        };
    }

    struct SignalData {
        std::string device;
        std::string frontend;
        std::string comment;
        std::string signalName;
        std::string subDeviceProperty;
        std::string quantity;
        std::string sampleRate;
        std::string unit;
        std::string accelerator;
        std::string deviceClass;
        std::string hostname;
        std::string protocol;
        std::string serviceName;
        int         port = -1;
    };

    static const std::string& categoryFieldForSignal(Category category, const SignalData& signal) {
        switch (category) {
        case Category::Domain: return signal.accelerator;
        case Category::DAQ_M: return signal.frontend;
        case Category::Quantity: return signal.quantity;
        case Category::Status: return signal.sampleRate;
        case Category::DeviceType: return signal.deviceClass;
        default: throw gr::exception(fmt::format("unknown category {} for Signal device: {}", magic_enum::enum_name(category), signal.device));
        }
    }

    std::vector<SignalData> m_signals;

    using SignalsIndexMap = std::map<std::string, std::vector<SignalData*>>;
    SignalsIndexMap deviceIndex;
    SignalsIndexMap frontendIndex;
    SignalsIndexMap signalNameIndex;
    SignalsIndexMap subDevicePropertyIndex;
    SignalsIndexMap quantityIndex;
    SignalsIndexMap sampleRateIndex;
    SignalsIndexMap unitIndex;
    SignalsIndexMap acceleratorIndex;
    SignalsIndexMap deviceClassIndex;

    std::array<SignalsIndexMap*, CategoriesCount> categoryIndices{&acceleratorIndex, &deviceClassIndex, &subDevicePropertyIndex, &unitIndex, &quantityIndex};

    // Filtering state
    std::string              m_shownSearchString;
    std::string              m_searchString;
    std::vector<SignalData*> m_filteredItems;

    struct CategorySearch {
        // SignalsIndexMap*                      mainFilterCategoryIndex = nullptr;
        std::deque<std::vector<SignalData*>> signalsToProcess;
        std::vector<FilterData*>             filters;
    };
    std::optional<CategorySearch> m_categorySearch;
    std::size_t                   m_nextItemToFilter = 0UZ;

    void startSearching(const std::string& _searchString, std::vector<FilterData*> _filters) {
        m_filteredItems.clear();
        m_searchString     = _searchString;
        m_nextItemToFilter = 0UZ;

        if (!_filters.empty()) {
            m_categorySearch          = CategorySearch{};
            m_categorySearch->filters = std::move(_filters);

            std::ranges::sort(m_categorySearch->filters, [](const auto* left, const auto* right) { return *left < *right; });
            auto mainCategory      = m_categorySearch->filters.front()->category;
            auto mainCategoryIndex = categoryIndices[+mainCategory];

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

    bool signalMatchesSearchString(const SignalData& signal) const {
        return signal.signalName.find(m_searchString) != std::string::npos || //
               signal.comment.find(m_searchString) != std::string::npos;
    }

    bool signalMatchesActiveFilters(const SignalData& signal) const {
        assert(m_categorySearch.has_value());
        enum MatchState { Unspecified = 0, NoMatches = 1, HasMatches = 2 };
        std::array<MatchState, CategoriesCount> stateMatches{};
        std::ranges::fill(stateMatches, Unspecified);

        for (const auto filter : m_categorySearch->filters) {
            if (stateMatches[+filter->category] == Unspecified) {
                stateMatches[+filter->category] = NoMatches;
            }

            if (stateMatches[+filter->category] == NoMatches) {
                if (filter->title == categoryFieldForSignal(filter->category, signal)) {
                    stateMatches[+filter->category] = HasMatches;
                }
            }
        }

        return std::ranges::find(stateMatches, NoMatches) == stateMatches.end();
    }

    bool loadMoreItems() {
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

    void buildIndex() {
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
            const auto&             index = *categoryIndices[+category];
            std::vector<FilterData> result;
            result.resize(index.size());
            std::ranges::transform(index, result.begin(), [category](const auto& kvp) { return FilterData{category, kvp.first, false}; });
            return result;
        };

        m_filterCombos.setData({
            {"##comboDomain", "Domain", colorForCategory(Category::Domain), itemsForIndex(Category::Domain)},                //
            {"##comboDeviceType", "Dev. type", colorForCategory(Category::DeviceType), itemsForIndex(Category::DeviceType)}, //
            {"##comboDAQ", "DAQ-M.", colorForCategory(Category::DAQ_M), {}},                                                 //
            {"##comboStatus", "Status", colorForCategory(Category::Status), {}},                                             //
            {"##comboQuantity", "Quantity", colorForCategory(Category::Quantity), itemsForIndex(Category::Quantity)}         //
        });
    }

public:
    SignalSelector() {
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
    }

    void open() { ImGui::OpenPopup(m_windowName.c_str()); }

    void drawRemoteSignalsInput(FlowGraph* fg) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("URI:");
        ImGui::SameLine();
        if (m_addRemoteSignalDialogOpened) {
            ImGui::SetKeyboardFocusHere();
            m_addRemoteSignalDialogOpened = false;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##uri", &m_addRemoteSignalUri);

        if (ImGui::Button("Add")) {
            m_addRemoteSignal = false;
            fg->addRemoteSource(m_addRemoteSignalUri);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_addRemoteSignal = false;
        }
    }

    void drawSourceBlocksChooser(FlowGraph* /*fg*/) {
        struct Cat {
            std::string                   name;
            std::vector<BlockDefinition*> types;
        };
        static std::vector<Cat> cats;
        cats.clear();
        cats.push_back({"Remote signals", {}});
        for (const auto& t : BlockRegistry::instance().types()) {
            if (t.second->isSource && !t.second->category.empty()) {
                auto it = std::find_if(cats.begin(), cats.end(), [&](const auto& c) { return c.name == t.second->category; });
                if (it == cats.end()) {
                    cats.push_back({t.second->category, {t.second.get()}});
                } else {
                    it->types.push_back(t.second.get());
                }
            }
        }
    }

    void drawElement(const SignalData& entry, std::size_t idx, FlowGraph* /*fg*/, const ImGuiSelectionBasicStorage& selection) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool itemSelected = selection.Contains(static_cast<ImGuiID>(idx));
        ImGui::SetNextItemSelectionUserData(static_cast<ImS64>(idx));
        ImGui::Selectable(entry.device.c_str(), itemSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
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

    void drawSignalSelector(FlowGraph* fg) {
        m_querySignalFilters.drawFilters();

        bool filtersChanged = m_forceRefresh;
        m_forceRefresh      = false;

        if (auto comboSelectedItem = m_filterCombos.draw()) {
            auto* item      = *comboSelectedItem;
            bool  wasActive = item->isActive;
            item->isActive  = !wasActive;

            if (wasActive) {
                m_selectedFilters.removeLabel(item);
            } else {
                m_selectedFilters.addLabel({item->title, item, colorForCategory(item->category)});
            }
            filtersChanged = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

        ImGui::InputText("##textFilter", &m_shownSearchString);
        if (m_shownSearchString != m_searchString) {
            filtersChanged = true;
        }

        if (auto removedLabel = m_selectedFilters.draw()) {
            (*removedLabel)->isActive = false;
            filtersChanged            = true;
        }

        if (filtersChanged) {
            std::vector<FilterData*> filters;
            filters.resize(m_selectedFilters.labels().size());
            std::ranges::transform(m_selectedFilters.labels(), filters.begin(), [](const auto& filter) { return filter.data; });
            startSearching(m_shownSearchString, filters);
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
                        drawElement(*m_filteredItems[n], n, fg, selection);
                    }
                }

                ms_io = ImGui::EndMultiSelect();
                selection.ApplyRequests(ms_io);
            }
        }

        if (ImGui::Button("Add Signal")) {
            std::vector<SignalData> entries;
            void*                   it = nullptr;
            ImGuiID                 id = 0;
            while (selection.GetNextSelectedItem(&it, &id)) {
                entries.push_back(*m_filteredItems[id]);
            }

            for (const auto& e : entries) {
                const auto uri   = opencmw::URI<>::UriFactory().scheme(e.protocol).hostName(e.hostname).port(static_cast<uint16_t>(e.port)).path(e.serviceName).addQueryParameter("channelNameFilter", e.signalName).build();
                auto*      block = fg->addRemoteSource(uri.str());
                if (m_addSignalCallback) {
                    m_addSignalCallback(block);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            m_signalList.update();
        }
    }

    void draw(FlowGraph* fg) {
        auto parentSize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(parentSize - ImVec2(32, 32), ImGuiCond_Once);
        if (auto menu = IMW::ModalPopup(m_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            float windowWidth = ImGui::GetWindowWidth();
            float buttonPosX  = windowWidth - 2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Close").x;
            ImGui::SetCursorPosX(buttonPosX);
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                return;
            }

            // fg->addBlock(sel->createBlock({}));

            drawSignalSelector(fg);
        }
    }

    void setAddSignalCallback(std::function<void(Block*)> callback) { m_addSignalCallback = callback; }
};
} // namespace DigitizerUi

#endif
