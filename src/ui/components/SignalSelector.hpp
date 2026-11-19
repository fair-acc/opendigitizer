#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_

#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <refl.hpp>

#include <ClientCommon.hpp>
#include <RestClient.hpp>

#include <gnuradio-4.0/Message.hpp>

#include "../common/ImguiWrap.hpp"
#include <misc/cpp/imgui_stdlib.h>

#include "FilterComboBoxes.hpp"
#include "SelectedLabelsView.hpp"

#include "services/dns_client.hpp"
#include "services/dns_types.hpp"
#include "settings.hpp"

namespace DigitizerUi {

class UiGraphModel;

struct QueryFilter {
    static constexpr std::array fieldNames = [] {
        constexpr auto                                   descriptor = refl::reflect<opencmw::service::dns::QueryEntry>();
        std::array<const char*, descriptor.members.size> names;
        std::size_t                                      index = 0;
        refl::util::for_each(descriptor.members, [&](auto member) { names[index++] = member.name.c_str(); });
        return names;
    }();

    std::string keyId;
    std::string valueId;
    std::string deleteButtonId;
    std::size_t selectedIndex = 1;
    std::string filterText;

    [[nodiscard]] std::string selectedField() const { return fieldNames[selectedIndex]; }
};

struct QueryFilters {
    std::vector<QueryFilter> elements;
    std::function<void()>    onChange;

    void draw(); // a combo box for the field, the filter text and a delete button per filter
    void changed() {
        if (onChange) {
            onChange();
        }
    }
};

class SignalList {
    Digitizer::Settings&           settings      = Digitizer::Settings::instance();
    opencmw::client::ClientContext clientContext = [this]() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY), opencmw::client::VerifyServerCertificates(settings.checkCertificates)));
        return opencmw::client::ClientContext{std::move(clients)};
    }();
    opencmw::service::dns::DnsClient dnsClient{clientContext, settings.serviceUrl().path("/dns").build()};

    QueryFilters& filters;

    std::vector<opencmw::service::dns::Entry> signals;
    std::mutex                                signalsMutex;

public:
    std::function<void(const std::vector<opencmw::service::dns::Entry>&)> updateSignalsCallback;

    explicit SignalList(QueryFilters& queryFilters) : filters(queryFilters) {
        filters.onChange = [this] { update(); };
    }
    ~SignalList() { filters.onChange = {}; }

    void update();
};

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
    std::string   m_windowName = "Add Device Signals";
    QueryFilters  m_querySignalFilters;
    SignalList    m_signalList{m_querySignalFilters};
    UiGraphModel* m_graphModel = nullptr;

    enum class Category { Domain = 0, DeviceType = 1, DAQ_M = 2, Status = 3, Quantity = 4 };
    static constexpr std::size_t CategoriesCount = 5;
    friend constexpr std::size_t operator+(Category category) { return static_cast<std::size_t>(category); }

    SelectedLabelsView m_selectedFilters;
    FilterComboBoxes   m_filterCombos;

    bool m_forceRefresh = false;
    // bool        m_addRemoteSignalDialogOpened = false;
    std::string m_addRemoteSignalUri;

    opencmw::service::dns::QueryEntry m_queryFilter;

    static constexpr std::array             colorsForLight = {ImColor(163, 217, 255), ImColor(189, 146, 221), ImColor(229, 99, 153), ImColor(238, 207, 109), ImColor(44, 165, 141)};
    static constexpr std::array             colorsForDark  = {ImColor(164, 130, 19), ImColor(0, 107, 184), ImColor(98, 44, 140), ImColor(157, 27, 81), ImColor(39, 145, 124)};
    static constexpr std::array<ImColor, 2> colorForCategory(std::size_t category) { return {colorsForLight[category], colorsForDark[category]}; }

    static const std::string& categoryFieldForSignal(std::size_t category, const SignalData& signal) {
        switch (static_cast<Category>(category)) {
        case Category::Domain: return signal.accelerator;
        case Category::DAQ_M: return signal.frontend;
        case Category::Quantity: return signal.quantity;
        case Category::Status: return signal.sampleRate;
        case Category::DeviceType: return signal.deviceClass;
        default: throw gr::exception(std::format("unknown category {} for Signal device: {}", gr::meta::enumName(static_cast<Category>(category)).value_or("?"), signal.device));
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

    std::array<SignalsIndexMap*, CategoriesCount> categoryIndices{&acceleratorIndex, &deviceClassIndex, &frontendIndex, &sampleRateIndex, &quantityIndex};

    // Filtering state
    std::string              m_shownSearchString;
    std::string              m_searchString;
    std::vector<SignalData*> m_filteredItems;

    struct CategorySearch {
        std::deque<std::vector<SignalData*>> signalsToProcess;
        std::vector<FilterTag*>              filters;
    };
    std::optional<CategorySearch> m_categorySearch;
    std::size_t                   m_nextItemToFilter = 0UZ;

    void startSearching(const std::string& _searchString, std::vector<FilterTag*> _filters);

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
