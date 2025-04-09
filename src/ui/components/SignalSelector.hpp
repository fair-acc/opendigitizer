#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include <gnuradio-4.0/Message.hpp>

#include "../common/ImguiWrap.hpp"
#include <misc/cpp/imgui_stdlib.h>

#include "../RemoteSignalSources.hpp"
#include "FilterComboBoxes.hpp"
#include "SelectedLabelsView.hpp"

namespace DigitizerUi {

class UiGraphModel;

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

    std::string uri() const { return opencmw::URI<>::UriFactory().scheme(protocol).hostName(hostname).port(static_cast<uint16_t>(port)).path(serviceName).addQueryParameter("channelNameFilter", signalName).build().str(); }
};

class SignalSelector {
private:
    std::string            m_windowName = "Add Device Signals";
    QueryFilterElementList m_querySignalFilters;
    SignalList             m_signalList{m_querySignalFilters};
    UiGraphModel*          m_graphModel = nullptr;

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
        std::deque<std::vector<SignalData*>> signalsToProcess;
        std::vector<FilterData*>             filters;
    };
    std::optional<CategorySearch> m_categorySearch;
    std::size_t                   m_nextItemToFilter = 0UZ;

    void startSearching(const std::string& _searchString, std::vector<FilterData*> _filters);

    bool signalMatchesSearchString(const SignalData& signal) const;

    bool signalMatchesActiveFilters(const SignalData& signal) const;

    bool loadMoreItems();

    void buildIndex();

    [[nodiscard]] std::vector<SignalData> drawSignalSelector();

public:
    explicit SignalSelector(UiGraphModel& graphModel);

    void open() { ImGui::OpenPopup(m_windowName.c_str()); }
    void close() { ImGui::CloseCurrentPopup(); }

    void drawElement(const SignalData& entry, std::size_t idx, const ImGuiSelectionBasicStorage& selection);

    [[nodiscard]] std::vector<SignalData> drawAndReturnSelected();
};
} // namespace DigitizerUi

#endif
